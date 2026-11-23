#include "components.h"
#include "platform_pages.h"
#include "hmi_presentation.h"
#include "hmi_delivery.h"
#include "ui_composition.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QScrollArea>
#include <QResizeEvent>
#include <QStyle>
#include <QSettings>
#include <QStackedWidget>
#include <QStringList>
#include <QStyleFactory>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QVector>

namespace {

constexpr int kLayoutVersion = 2;

// UI preview deliberately has no dependency on StationSession, drivers or scenarios.
// These are view-only fixtures, not station telemetry or hardware availability.
struct DeliveryFixture {
    QString id;
    QString name;
    QString dut;
    QStringList equipment;
    QStringList capabilities;
};

QLabel* label(const QString& text, const char* role = "body")
{
    auto* widget = new QLabel(text);
    widget->setWordWrap(true);
    widget->setProperty("role", role);
    return widget;
}

QFrame* card(const QString& title, const QString& content, const QString& detail = {})
{
    auto* frame = new QFrame;
    frame->setObjectName(QStringLiteral("card"));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(18, 17, 18, 17);
    layout->setSpacing(8);
    layout->addWidget(label(title, "eyebrow"));
    layout->addWidget(label(content, "value"));
    if (!detail.isEmpty()) layout->addWidget(label(detail, "muted"));
    return frame;
}

QFrame* item(const QString& title, const QString& description)
{
    auto* frame = new QFrame;
    frame->setObjectName(QStringLiteral("item"));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(5);
    layout->addWidget(label(title, "section"));
    layout->addWidget(label(description, "muted"));
    return frame;
}

QFrame* empty(const QString& title, const QString& detail)
{
    auto* frame = new QFrame;
    frame->setObjectName(QStringLiteral("empty"));
    frame->setMinimumHeight(145);
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(9);
    layout->addStretch();
    layout->addWidget(label(title, "section"), 0, Qt::AlignHCenter);
    auto* d = label(detail, "muted");
    d->setAlignment(Qt::AlignHCenter);
    layout->addWidget(d);
    layout->addStretch();
    return frame;
}

// При недостатке ширины переносим существующие элементы строки друг под друга.
class AdaptiveRow final : public QWidget {
public:
    explicit AdaptiveRow(QWidget* parent = nullptr) : QWidget(parent), row_(new QHBoxLayout(this))
    {
        row_->setContentsMargins(0, 0, 0, 0);
    }
    QBoxLayout* row() const { return row_; }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        row_->setDirection(event->size().width() < 550
                               ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    }

private:
    QBoxLayout* row_;
};

// Перестраиваем только существующие карточки: порядок чтения при этом не меняется.
class AdaptiveCards final : public QWidget {
public:
    explicit AdaptiveCards(QWidget* parent = nullptr) : QWidget(parent), grid_(new QGridLayout(this))
    {
        grid_->setContentsMargins(0, 0, 0, 0);
        grid_->setSpacing(12);
    }

    void addCard(QWidget* card)
    {
        cards_.append(card);
        grid_->addWidget(card, 0, cards_.size() - 1);
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        const int columns = event->size().width() < 620 ? 1 : cards_.size();
        if (columns == columns_) return;
        columns_ = columns;
        for (int i = 0; i < cards_.size(); ++i) {
            grid_->removeWidget(cards_.at(i));
            grid_->addWidget(cards_.at(i), i / columns, i % columns);
        }
    }

private:
    QGridLayout* grid_;
    QVector<QWidget*> cards_;
    int columns_ = 0;
};

struct Page {
    QWidget* widget;
    QVBoxLayout* body;
};

Page page(const QString& eyebrow, const QString& title, const QString& subtitle)
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* bodyWidget = new QWidget;
    auto* layout = new QVBoxLayout(bodyWidget);
    layout->setContentsMargins(23, 22, 23, 22);
    layout->setSpacing(16);
    layout->addWidget(label(eyebrow, "eyebrow"));
    layout->addWidget(label(title, "pageTitle"));
    layout->addWidget(label(subtitle, "muted"));
    scroll->setWidget(bodyWidget);
    return {scroll, layout};
}


} // namespace

class UiPreviewWindow final : public QMainWindow
{
public:
    explicit UiPreviewWindow(const QString& manifestDirectory = QStringLiteral(":/ui/manifests"))
    {
        setWindowTitle(QStringLiteral("MilTechStation · UI Preview"));
        setMinimumSize(800, 600);
        resize(1510, 900);
        setAnimated(false);

        auto* shell = new QWidget;
        shell->setObjectName(QStringLiteral("shell"));
        auto* root = new QVBoxLayout(shell);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto* header = new QFrame;
        header->setObjectName(QStringLiteral("header"));
        header->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(24, 15, 24, 15);
        headerLayout->setSpacing(17);
        auto* identity = new QHBoxLayout;
        identity->addWidget(label(QStringLiteral("MilTechStation"), "appName"));
        identity->addStretch();
        context_ = label(QStringLiteral("КТМА"), "muted");
        identity->addWidget(context_);
        headerLayout->addLayout(identity, 1);
        auto* selection = new QHBoxLayout;
        modeSelector_ = new QComboBox;
        modeSelector_->setObjectName(QStringLiteral("ui.variantSelector"));
        compositions_ = miltech::ui::loadPreviewCompositions(manifestDirectory);
        for (const auto& composition : compositions_)
            modeSelector_->addItem(composition.selectorTitle, composition.id);
        modeSelector_->setAccessibleName(QStringLiteral("Вариант интерфейса"));
        modeSelector_->setMinimumHeight(modeSelector_->sizeHint().height());
        selection->addWidget(modeSelector_);
        selection->addWidget(label(QStringLiteral("UI PREVIEW · БЕЗ ОБОРУДОВАНИЯ"), "demo"));
        headerLayout->addLayout(selection);
        headerLayout_ = headerLayout;
        root->addWidget(header);

        auto* workspace = new QHBoxLayout;
        workspace->setContentsMargins(0, 0, 0, 0);
        workspace->setSpacing(0);
        auto* sidebar = new QFrame;
        sidebar->setObjectName(QStringLiteral("sidebar"));
        navLayout_ = new QVBoxLayout(sidebar);
        navLayout_->setContentsMargins(12, 17, 12, 14);
        navLayout_->setSpacing(6);
        auto* navigationScroll = new QScrollArea;
        navigationScroll->setObjectName(QStringLiteral("ui.navigationScroll"));
        navigationScroll->setWidgetResizable(true);
        navigationScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        navigationScroll->setFixedWidth(290);
        navigationScroll->setWidget(sidebar);
        workspace->addWidget(navigationScroll);
        stack_ = new QStackedWidget;
        stack_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        workspace->addWidget(stack_, 1);
        root->addLayout(workspace, 1);

        auto* footer = new QFrame;
        footer->setObjectName(QStringLiteral("footer"));
        footer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto* footerLayout = new QHBoxLayout(footer);
        footerLayout->setContentsMargins(22, 10, 22, 10);
        footerLayout->addWidget(label(QStringLiteral("ПРОТОТИП UI · НЕТ ДАННЫХ ОТ СТЕНДА"), "warning"));
        footerLayout->addStretch();
        footerLayout->addWidget(label(QStringLiteral("Qt Widgets · интерфейс без runtime"), "muted"));
        root->addWidget(footer);
        setCentralWidget(shell);

        connect(modeSelector_, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int index) { switchMode(index); });
        switchMode(0);
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QMainWindow::resizeEvent(event);
        if (headerLayout_) {
            headerLayout_->setDirection(event->size().width() < 1050
                                            ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
            headerLayout_->invalidate();
            headerLayout_->parentWidget()->setMinimumHeight(headerLayout_->minimumSize().height());
            headerLayout_->parentWidget()->updateGeometry();
        }
    }

    void closeEvent(QCloseEvent* event) override
    {
        saveWorkspace();
        QMainWindow::closeEvent(event);
    }

private:
    void createDocks(QMainWindow* host, bool reference)
    {
        host->setDockOptions(QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);
        host->setAnimated(false);

        if (composition_.hasComponent(QStringLiteral("parameters"))) {
            parameterDock_ = new QDockWidget(QStringLiteral("Параметры"), host);
            parameterDock_->setObjectName(QStringLiteral("uiPreview.parameters"));
            parameterDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea
                                            | Qt::BottomDockWidgetArea);
            parameterDock_->setMinimumWidth(260);
            parameterPanel_ = new miltech::ui::ParameterPanel(reference);
            auto* panelScroll = new QScrollArea;
            panelScroll->setWidgetResizable(true);
            panelScroll->setWidget(parameterPanel_);
            parameterDock_->setWidget(panelScroll);
            host->addDockWidget(Qt::RightDockWidgetArea, parameterDock_);
        }
        if (composition_.hasComponent(QStringLiteral("events"))) {
            eventDock_ = new QDockWidget(QStringLiteral("Журнал событий"), host);
            eventDock_->setObjectName(QStringLiteral("uiPreview.events"));
            eventDock_->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea
                                        | Qt::RightDockWidgetArea);
            eventLog_ = new miltech::ui::EventLogPanel;
            eventDock_->setWidget(eventLog_);
            host->addDockWidget(Qt::BottomDockWidgetArea, eventDock_);
            eventDock_->setMinimumHeight(145);
        }
    }

    QString workspaceKey() const
    {
        return QStringLiteral("layouts/%1/v2").arg(composition_.id);
    }

    void saveWorkspace()
    {
        if (composition_.isTu() || screenIds_.value(currentPage_) != QStringLiteral("workspace") || !dockHost_) return;
        QSettings settings(QStringLiteral("MilTechStation"),
                           QStringLiteral("UI-Preview"));
        settings.setValue(workspaceKey(), dockHost_->saveState(kLayoutVersion));
    }

    void restoreWorkspace()
    {
        if (!dockHost_) return;
        if (parameterDock_) parameterDock_->show();
        if (eventDock_) eventDock_->show();
        QSettings settings(QStringLiteral("MilTechStation"),
                           QStringLiteral("UI-Preview"));
        const QByteArray state = settings.value(workspaceKey()).toByteArray();
        if (!state.isEmpty() && !dockHost_->restoreState(state, kLayoutVersion)
            && eventLog_)
            eventLog_->appendUiEvent(QStringLiteral("Предыдущая компоновка несовместима; используется текущая"));
    }

    void resetWorkspaceLayout()
    {
        if (!dockHost_) return;
        QSettings settings(QStringLiteral("MilTechStation"),
                           QStringLiteral("UI-Preview"));
        settings.remove(workspaceKey());
        if (parameterDock_) {
            dockHost_->removeDockWidget(parameterDock_);
            parameterDock_->setFloating(false);
            dockHost_->addDockWidget(Qt::RightDockWidgetArea, parameterDock_);
            parameterDock_->show();
        }
        if (eventDock_) {
            dockHost_->removeDockWidget(eventDock_);
            eventDock_->setFloating(false);
            dockHost_->addDockWidget(Qt::BottomDockWidgetArea, eventDock_);
            eventDock_->show();
        }
        if (eventLog_)
            eventLog_->appendUiEvent(QStringLiteral("Восстановлена исходная компоновка"));
    }

    void setPage(int index)
    {
        if (screenIds_.value(currentPage_) == QStringLiteral("workspace") && !composition_.isTu()) saveWorkspace();
        if (index < 0 || index >= stack_->count()) return;
        currentPage_ = index;
        stack_->setCurrentIndex(index);
        for (int n = 0; n < navButtons_.size(); ++n) {
            navButtons_.at(n)->setProperty("navActive", n == index);
            navButtons_.at(n)->setChecked(n == index);
            navButtons_.at(n)->style()->unpolish(navButtons_.at(n));
            navButtons_.at(n)->style()->polish(navButtons_.at(n));
        }
        const bool fullWorkspace = !composition_.isTu()
            && screenIds_.value(index) == QStringLiteral("workspace");
        if (fullWorkspace) restoreWorkspace();
        else {
            if (parameterDock_) parameterDock_->hide();
            if (eventDock_) eventDock_->hide();
        }
        if (eventLog_)
            eventLog_->appendUiEvent(
                QStringLiteral("Раздел: %1").arg(navButtons_.at(index)->text()));
    }

    void addNavigation(const QString& id, const QString& title, QWidget* screen)
    {
        const int index = stack_->count();
        auto* button = new QPushButton(title);
        button->setObjectName(QStringLiteral("navButton"));
        button->setCheckable(true);
        button->setAccessibleName(title);
        button->setShortcut(QKeySequence(QStringLiteral("Alt+%1").arg(index + 1)));
        button->setProperty("navActive", false);
        navLayout_->addWidget(button);
        navButtons_.append(button);
        screenIds_.append(id);
        stack_->addWidget(screen);
        connect(button, &QPushButton::clicked, this, [this, index] {
            setPage(index);
        });
    }

    void resetNavigation()
    {
        navButtons_.clear();
        screenIds_.clear();
        while (auto* item = navLayout_->takeAt(0)) {
            if (item->widget()) delete item->widget();
            delete item;
        }
        while (stack_->count() > 0) {
            QWidget* widget = stack_->widget(0);
            stack_->removeWidget(widget);
            delete widget;
        }
        dockHost_ = nullptr;
        parameterDock_ = nullptr;
        eventDock_ = nullptr;
        parameterPanel_ = nullptr;
        eventLog_ = nullptr;
        delete hmiCollection_;
        hmiCollection_ = nullptr;
    }

    QWidget* createModuleScreen(const QString& id, const DeliveryFixture& delivery)
    {
        if (id == QStringLiteral("overview")) return makeOverview(delivery);
        if (id == QStringLiteral("workspace")) return makeWorkspace(delivery);
        if (id == QStringLiteral("hmi") && hmiCollection_)
            return new miltech::ui::HmiDeliveryEditor(hmiCollection_);
        if (id == QStringLiteral("equipment")) return makeEquipment(delivery);
        if (id == QStringLiteral("scenarios")) return makeScenarios(delivery);
        if (id == QStringLiteral("results")) return makeResults();
        if (id == QStringLiteral("configurator"))
            return new miltech::ui::TuConfiguratorPage(delivery.id);
        if (id == QStringLiteral("administration"))
            return new miltech::ui::StationAdministrationPage(delivery.name, delivery.equipment);
        if (id == QStringLiteral("tu.prepare")) return makeTuPrepare();
        if (id == QStringLiteral("tu.run")) return makeTuRun();
        if (id == QStringLiteral("tu.result")) return makeTuResult();
        Page error = page(QStringLiteral("МОДУЛЬ UI"), QStringLiteral("Экран недоступен"),
                          QStringLiteral("Неизвестный идентификатор интерфейсного модуля: %1").arg(id));
        error.body->addStretch();
        return error.widget;
    }

    void switchMode(int index)
    {
        saveWorkspace();
        currentPage_ = -1;
        resetNavigation();

        composition_ = index >= 0 && index < compositions_.size()
            ? compositions_.at(index) : miltech::ui::UiComposition{};
        if (!composition_.valid() && composition_.error.isEmpty())
            composition_.error = QStringLiteral("Нет доступных UI-манифестов");
        const DeliveryFixture delivery{composition_.id, composition_.demoName, composition_.demoDut,
                                       composition_.demoEquipment, composition_.demoCapabilities};
        context_->setText(composition_.valid() ? composition_.title
                                               : QStringLiteral("Ошибка UI-композиции"));

        navLayout_->addWidget(label(composition_.isTu()
                                        ? QStringLiteral("ОПЕРАТОР")
                                        : QStringLiteral("РАЗДЕЛЫ"),
                                    "eyebrow"));

        if (!composition_.valid()) {
            Page error = page(QStringLiteral("UI-МАНИФЕСТ"),
                              QStringLiteral("Конфигурация не загружена"),
                              composition_.error);
            error.body->addStretch();
            addNavigation(QStringLiteral("error"),
                          QStringLiteral("Ошибка конфигурации"), error.widget);
            setPage(0);
            return;
        }

        if (!composition_.isTu()
            && (composition_.hasComponent(QStringLiteral("hmi"))
                || composition_.indexOf(QStringLiteral("hmi")) >= 0)) {
            hmiCollection_ = new miltech::ui::HmiDeliveryCollection(composition_.id, this);
            QString problem;
            if (!hmiCollection_->initialize(composition_.hmiPages, &problem)) {
                delete hmiCollection_;
                hmiCollection_ = nullptr;
                composition_.error = QStringLiteral("Не удалось загрузить HMI поставки: %1")
                                         .arg(problem);
            }
        }
        if (!composition_.valid()) {
            Page error = page(QStringLiteral("HMI-ПОСТАВКА"),
                              QStringLiteral("Ошибка комплекта HMI"), composition_.error);
            error.body->addStretch();
            addNavigation(QStringLiteral("error"), QStringLiteral("Ошибка HMI"), error.widget);
            setPage(0);
            return;
        }
        bool serviceSection = false;
        for (const miltech::ui::ScreenSpec& screen : composition_.screens) {
            if (screen.group == QStringLiteral("service") && !serviceSection) {
                navLayout_->addSpacing(15);
                navLayout_->addWidget(label(QStringLiteral("СЕРВИС"), "eyebrow"));
                serviceSection = true;
            }
            addNavigation(screen.id, screen.title, createModuleScreen(screen.id, delivery));
        }
        navLayout_->addStretch();
        navLayout_->addWidget(label(QStringLiteral("Alt+1…9 · навигация"), "muted"));
        if (eventLog_)
            eventLog_->appendUiEvent(
                QStringLiteral("Загружена UI-композиция: %1").arg(composition_.id));
        setPage(0);
    }

    QWidget* makeOverview(const DeliveryFixture& spec)
    {
        Page p = page(QStringLiteral("СТАНЦИЯ / ОБЗОР"), spec.name,
                      QStringLiteral("Контекст поставки и точка входа в инженерную работу"));
        auto* metrics = new AdaptiveCards;
        metrics->setObjectName(QStringLiteral("ui.overviewCards"));
        metrics->addCard(card(QStringLiteral("ИЗДЕЛИЕ"), spec.dut,
                              QStringLiteral("Тип из UI-фикстуры")));
        metrics->addCard(card(QStringLiteral("СТЕНД"), QStringLiteral("Не опрошен"),
                              QStringLiteral("Связь с оборудованием отсутствует")));
        metrics->addCard(card(QStringLiteral("АКТИВНЫЙ ЗАПУСК"), QStringLiteral("Нет"),
                              QStringLiteral("Сценарии не подключены")));
        p.body->addWidget(metrics);
        p.body->addWidget(label(QStringLiteral("Рабочие возможности"), "section"));
        for (const QString& capability : spec.capabilities) {
            p.body->addWidget(item(capability, QStringLiteral("Доступность определяется поставкой; в прототипе действия отключены")));
        }
        auto* open = new QPushButton(QStringLiteral("Открыть рабочую область  →"));
        open->setAccessibleName(QStringLiteral("Перейти в рабочую область"));
        connect(open, &QPushButton::clicked, this, [this] { setPage(composition_.indexOf(QStringLiteral("workspace"))); });
        p.body->addWidget(open);
        p.body->addStretch();
        return p.widget;
    }

    QWidget* makeWorkspace(const DeliveryFixture& spec)
    {
        // Nested QMainWindow keeps engineering docks INSIDE the workspace:
        // the persistent top bar and application navigation cannot be docked over.
        dockHost_ = new QMainWindow;
        dockHost_->setObjectName(QStringLiteral("ui.workspaceHost"));
        dockHost_->setMinimumSize(0, 0);
        createDocks(dockHost_, composition_.referenceSample);

        Page p = page(QStringLiteral("ИНЖЕНЕРНАЯ СРЕДА"),
                      QStringLiteral("Рабочая область"),
                      QStringLiteral("Параметры, графики и схема представления · только UI-демонстрация"));

        auto* topRow = new AdaptiveRow;
        topRow->row()->addWidget(label(
            QStringLiteral("ПОСТАВКА · %1  /  ФИЗИЧЕСКИЙ СТЕНД НЕ ПОДКЛЮЧЁН").arg(spec.name),
            "eyebrow"), 1);
        auto* reset = new QPushButton(QStringLiteral("Исходная компоновка"));
        reset->setObjectName(QStringLiteral("ui.resetLayout"));
        reset->setAccessibleName(QStringLiteral("Восстановить исходное расположение инженерных панелей"));
        connect(reset, &QPushButton::clicked, this, [this] { resetWorkspaceLayout(); });
        topRow->row()->addWidget(reset);
        p.body->addWidget(topRow);

        if (composition_.hasComponent(QStringLiteral("hmi")) && hmiCollection_)
            p.body->addWidget(new miltech::ui::HmiDeliveryViewer(hmiCollection_));

        if (composition_.hasComponent(QStringLiteral("trend"))) {
            auto* chart = new miltech::ui::TrendPanel;
            p.body->addWidget(chart);
            if (parameterPanel_) {
                connect(chart->syntheticToggle(), &QCheckBox::toggled,
                        parameterPanel_, &miltech::ui::ParameterPanel::setSyntheticVisible);
            }
            connect(chart->syntheticToggle(), &QCheckBox::toggled, this,
                    [this](bool visible) {
                if (eventLog_)
                    eventLog_->appendUiEvent(visible
                        ? QStringLiteral("Включена демонстрация синтетических значений")
                        : QStringLiteral("Показаны пустые состояния данных"));
            });
            connect(chart->waveformSelector(), qOverload<int>(&QComboBox::currentIndexChanged),
                    this, [this](int index) {
                if (eventLog_)
                    eventLog_->appendUiEvent(index == 1
                        ? QStringLiteral("Выбран график счётчика 0–1023 (синтетика)")
                        : QStringLiteral("Выбран демонстрационный тренд"));
            });
        }

        if (composition_.hasComponent(QStringLiteral("topology")))
            p.body->addWidget(new miltech::ui::SignalTopology(
                composition_.referenceSample));
        p.body->addWidget(item(QStringLiteral("Док-панели"),
                                QStringLiteral("Параметры и журнал можно закреплять, закрывать и переносить. "
                                               "«Исходная компоновка» возвращает обе панели.")));
        p.body->addStretch();
        dockHost_->setCentralWidget(p.widget);
        return dockHost_;
    }

    QWidget* makeEquipment(const DeliveryFixture& spec)
    {
        Page p = page(QStringLiteral("СТЕНД / КОМПОНЕНТЫ"),
                      QStringLiteral("Оборудование"),
                      QStringLiteral("Состав — из описания поставки; статусы в прототипе всегда неизвестны"));
        p.body->addWidget(new miltech::ui::EquipmentOverview(spec.equipment));
        p.body->addWidget(item(QStringLiteral("Безопасность представления"),
                               QStringLiteral("Нет связи с адаптерами. Отсутствие данных нельзя считать ни готовностью, ни отказом изделия.")));
        p.body->addStretch();
        return p.widget;
    }

    QWidget* makeScenarios(const DeliveryFixture& spec)
    {
        Page p = page(QStringLiteral("ИНЖЕНЕР / МЕТОДИКИ"),
                      QStringLiteral("Сценарии"),
                      QStringLiteral("Разработка и просмотр сценариев, а не выполнение официального ТУ"));
        p.body->addWidget(item(QStringLiteral("Библиотека сценариев"),
                               QStringLiteral("Будущий каталог с поиском, фильтрами и версионированием")));
        p.body->addWidget(item(QStringLiteral("Визуальный редактор"),
                               QStringLiteral("Шаги, условия, критерии и валидация структуры")));
        p.body->addWidget(item(QStringLiteral("Профиль: %1").arg(spec.name),
                               QStringLiteral("Пакет проекта задаёт видимые методики и допустимые действия")));
        p.body->addWidget(empty(QStringLiteral("Сценарий не выбран"),
                                QStringLiteral("Команды запуска намеренно отсутствуют")));
        p.body->addStretch();
        return p.widget;
    }

    QWidget* makeResults()
    {
        Page p = page(QStringLiteral("АРХИВ / ПРОТОКОЛЫ"),
                      QStringLiteral("Результаты"),
                      QStringLiteral("Поиск по изделию, времени, методике и статусу"));
        p.body->addWidget(empty(QStringLiteral("Данные не загружены"),
                                QStringLiteral("Архив не подключён; результаты не моделируются")));
        p.body->addStretch();
        return p.widget;
    }

    QWidget* makeTuPrepare()
    {
        Page p = page(QStringLiteral("ТУ / 01"),
                      QStringLiteral("Подготовка испытания"),
                      QStringLiteral("Операторский путь для отдельной ТУ-сборки УБСИ"));
        p.body->addWidget(card(QStringLiteral("ИЗДЕЛИЕ"),
                               QStringLiteral("УБСИ"),
                               QStringLiteral("Заводской номер не введён")));
        p.body->addWidget(item(QStringLiteral("Методика"),
                               QStringLiteral("Фиксированная проверка по ТУ; выбор конкретного режима определяется составом ТУ-сборки")));
        p.body->addWidget(item(QStringLiteral("Готовность стенда"),
                               QStringLiteral("Не проверена. Никакие действия с оборудованием не выполняются.")));
        auto* start = new QPushButton(QStringLiteral("Подготовить проверку"));
        start->setEnabled(false);
        start->setAccessibleDescription(QStringLiteral("Неактивно: это автономный UI-прототип"));
        p.body->addWidget(start);
        p.body->addStretch();
        return p.widget;
    }

    QWidget* makeTuRun()
    {
        Page p = page(QStringLiteral("ТУ / 02"),
                      QStringLiteral("Ход испытания"),
                      QStringLiteral("Фиксированная структура операторского экрана"));
        p.body->addWidget(card(QStringLiteral("ТЕКУЩАЯ ОПЕРАЦИЯ"),
                               QStringLiteral("Не начата"),
                               QStringLiteral("Здесь отображается шаг методики и необходимые действия")));
        auto* metrics = new AdaptiveCards;
        metrics->addCard(card(QStringLiteral("ФАКТИЧЕСКОЕ"), QStringLiteral("—"),
                              QStringLiteral("Нет измерения")));
        metrics->addCard(card(QStringLiteral("ДОПУСК"), QStringLiteral("—"),
                              QStringLiteral("Не загружен")));
        p.body->addWidget(metrics);
        p.body->addWidget(empty(QStringLiteral("Протокол выполнения"),
                                QStringLiteral("Шаги и события будут отображаться из UI-модели")));
        p.body->addStretch();
        return p.widget;
    }

    QWidget* makeTuResult()
    {
        Page p = page(QStringLiteral("ТУ / 03"),
                      QStringLiteral("Результат проверки"),
                      QStringLiteral("Статус изделия отделён от ошибки стенда и неполного испытания"));
        p.body->addWidget(card(QStringLiteral("ЗАКЛЮЧЕНИЕ"),
                               QStringLiteral("Не сформировано"),
                               QStringLiteral("Процедура не выполнялась")));
        p.body->addWidget(item(QStringLiteral("Документирование"),
                               QStringLiteral("Итоговые значения, версия методики, действия оператора и протокол")));
        auto* exportButton = new QPushButton(QStringLiteral("Открыть протокол"));
        exportButton->setEnabled(false);
        p.body->addWidget(exportButton);
        p.body->addStretch();
        return p.widget;
    }

    QBoxLayout* headerLayout_ = nullptr;
    QComboBox* modeSelector_ = nullptr;
    QLabel* context_ = nullptr;
    QVBoxLayout* navLayout_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QDockWidget* parameterDock_ = nullptr;
    QDockWidget* eventDock_ = nullptr;
    QMainWindow* dockHost_ = nullptr;
    miltech::ui::ParameterPanel* parameterPanel_ = nullptr;
    miltech::ui::EventLogPanel* eventLog_ = nullptr;
    miltech::ui::HmiDeliveryCollection* hmiCollection_ = nullptr;
    QVector<QPushButton*> navButtons_;
    QStringList screenIds_;
    miltech::ui::UiComposition composition_;
    QVector<miltech::ui::UiComposition> compositions_;
    int currentPage_ = -1;
};

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("MilTechStation UI Preview"));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QFile file(QStringLiteral(":/ui/theme.qss"));
    if (file.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(file.readAll()));
    const QStringList args = app.arguments();
    const bool automated = args.contains(QStringLiteral("--ui-smoke"))
                        || args.contains(QStringLiteral("--ui-snapshot-dir"));
    if (automated) {
        // Automated UI checks must not write to the operator's ordinary registry.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QDir::tempPath() + QStringLiteral("/MilTechStationUiAutomation"));
    }

    UiPreviewWindow window;
    window.show();

    if (args.contains(QStringLiteral("--ui-smoke"))) {
        auto* selector = window.findChild<QComboBox*>(QStringLiteral("ui.variantSelector"));
        auto* stack = window.findChild<QStackedWidget*>();
        if (!selector || !stack) return 2;
        const auto compositions = miltech::ui::loadPreviewCompositions(QStringLiteral(":/ui/manifests"));
        for (const QString& required : {QStringLiteral("ktma"), QStringLiteral("reference_device"), QStringLiteral("tu_ubsi")})
            if (selector->findData(required) < 0) return 25;
        for (int index = 0; index < selector->count(); ++index) {
            selector->setCurrentIndex(index);
            QApplication::processEvents();
            const auto composition = compositions.at(index);
            if (!composition.valid() || stack->count() != composition.screens.size()) return 3;
            const auto docks = window.findChildren<QDockWidget*>();
            if (composition.isTu()) {
                if (!docks.isEmpty()
                    || window.findChild<miltech::ui::ParameterPanel*>()
                    || window.findChild<miltech::ui::TrendPanel*>()
                    || window.findChild<miltech::ui::HmiDesignerPage*>()
                    || window.findChild<miltech::ui::HmiReadOnlyPanel*>()
                    || window.findChild<miltech::ui::TuConfiguratorPage*>()
                    || window.findChild<miltech::ui::StationAdministrationPage*>()) return 4;
                continue;
            }
            const int expectedDocks = int(composition.hasComponent(QStringLiteral("parameters")))
                                    + int(composition.hasComponent(QStringLiteral("events")));
            if (docks.size() != expectedDocks) return 5;
            const auto nav = window.findChildren<QPushButton*>(QStringLiteral("navButton"));
            if (nav.size() != composition.screens.size()) return 6;
            auto* configurator = window.findChild<miltech::ui::TuConfiguratorPage*>();
            auto* administration = window.findChild<miltech::ui::StationAdministrationPage*>();
            if (composition.indexOf(QStringLiteral("configurator")) < 0
                || composition.indexOf(QStringLiteral("administration")) < 0
                || !composition.hasComponent(QStringLiteral("parameters"))
                || !composition.hasComponent(QStringLiteral("events"))
                || !composition.hasComponent(QStringLiteral("trend"))
                || composition.indexOf(QStringLiteral("equipment")) < 0) continue;
            if (!configurator || !administration || !administration->sections()
                || administration->sections()->count() != 4) return 16;
            if (!configurator->packageButton() || configurator->packageButton()->isEnabled()) return 17;
            if (!configurator->draftPreview()->toPlainText().contains(QStringLiteral("backend_validated"))) return 18;
            if (composition.id == QStringLiteral("ktma") && configurator->methodologyChoice()->count() != 3) return 19;
            nav.at(composition.indexOf(QStringLiteral("workspace")))->click();
            QApplication::processEvents();
            auto* params = window.findChild<miltech::ui::ParameterPanel*>();
            auto* chart = window.findChild<miltech::ui::TrendPanel*>();
            auto* equipment = window.findChild<miltech::ui::EquipmentOverview*>();
            auto* events = window.findChild<miltech::ui::EventLogPanel*>();
            if (!params || !chart || !equipment || !events) return 7;
            if (equipment->equipmentCount() != composition.demoEquipment.size()) return 8;
            if (params->filteredRowCount() != (composition.referenceSample ? 3 : 4)) return 9;
            params->searchInput()->setText(QStringLiteral("UNMATCHED_UI_TEST"));
            if (params->filteredRowCount() != 0) return 10;
            params->searchInput()->clear();
            if (params->filteredRowCount() == 0) return 11;
            chart->waveformSelector()->setCurrentIndex(1);
            if (chart->canvas()->waveform() != 1) return 12;
            chart->syntheticToggle()->setChecked(false);
            if (chart->canvas()->syntheticVisible()) return 13;
            chart->syntheticToggle()->setChecked(true);
            if (!chart->canvas()->syntheticVisible()) return 14;
            if (events->entryCount() < 3) return 15;
            if (bool(window.findChild<miltech::ui::SignalTopology*>()) != composition.hasComponent(QStringLiteral("topology"))) return 24;
            if (composition.hasComponent(QStringLiteral("hmi"))) {
                auto* document = window.findChild<miltech::ui::HmiDocument*>();
                auto* designer = window.findChild<miltech::ui::HmiDesignerPage*>();
                auto* viewer = window.findChild<miltech::ui::HmiReadOnlyPanel*>();
                if (!document || !designer || !viewer || document->elements().isEmpty())
                    return 50;
                if (designer->canvas()->renderedElementCount() != document->elements().size()
                    || viewer->canvas()->renderedElementCount() != document->elements().size())
                    return 51;
                auto* button = designer->addButton(QStringLiteral("numeric"));
                if (!button) return 52;
                const int before = document->elements().size();
                button->click();
                if (document->elements().size() != before + 1
                    || viewer->canvas()->renderedElementCount() != before + 1)
                    return 53;
                auto* sourcePicker = designer->bindingChoice();
                auto* bindingField = designer->findChild<QLineEdit*>(QStringLiteral("ui.hmi.binding"));
                auto* applyBinding = designer->findChild<QPushButton*>(QStringLiteral("ui.hmi.apply"));
                auto* bindingHint = designer->findChild<QLabel*>(QStringLiteral("ui.hmi.bindingHint"));
                if (!sourcePicker || !bindingField || !applyBinding || !bindingHint
                    || sourcePicker->count() != 5) return 71;
                sourcePicker->setCurrentIndex(3);
                if (bindingField->text() != QStringLiteral("ui.demo.load")
                    || !bindingHint->text().contains(QStringLiteral("Совместим"))) return 72;
                applyBinding->click();
                if (document->elements().constLast().binding != QStringLiteral("ui.demo.load")) return 73;
                sourcePicker->setCurrentIndex(4);
                if (!bindingHint->text().contains(QStringLiteral("несовместим"))) return 74;
                designer->zoomChoice()->setCurrentIndex(1); // 100%
                if (qAbs(designer->canvas()->transform().m11() - 1.0) > 0.02) return 60;
                designer->zoomChoice()->setCurrentIndex(0); // fit all
                QApplication::processEvents();
                if (designer->canvas()->transform().m11() <= 0.0) return 61;
                designer->previewMode()->setCurrentIndex(1);
                if (designer->canvas()->editing() || designer->canvas()->demoValues()) return 54;
                designer->previewMode()->setCurrentIndex(2);
                if (designer->canvas()->editing() || !designer->canvas()->demoValues()
                    || viewer->canvas()->demoValues()) return 55;
                miltech::ui::HmiDocument restored(composition.id);
                QString reason;
                if (!restored.loadFromJson(document->toJson(), &reason)
                    || restored.elements().size() != document->elements().size()) return 56;
                miltech::ui::HmiDocument wrongProject(QStringLiteral("other_project"));
                if (wrongProject.loadFromJson(document->toJson(), &reason)) return 57;
                const QString addedId = document->elements().constLast().id;
                if (!document->moveElement(addedId, 2000, 2000)) return 58;
                const auto* moved = document->find(addedId);
                if (!moved || moved->geometry.right() >= 960
                    || moved->geometry.bottom() >= 540) return 59;

                auto* pages = window.findChild<miltech::ui::HmiDeliveryCollection*>();
                auto* pageEditor = window.findChild<miltech::ui::HmiDeliveryEditor*>();
                auto* pageViewer = window.findChild<miltech::ui::HmiDeliveryViewer*>();
                if (!pages || !pageEditor || !pageViewer
                    || pages->pageCount() != composition.hmiPages.size()) return 62;
                if (!pages->setCurrentIndex(1)
                    || pageEditor->pageSelector()->currentIndex() != 1
                    || pageViewer->visiblePageIndex() != 1) return 63;
                pageEditor->addPageButton()->click();
                if (pages->pageCount() != composition.hmiPages.size() + 1
                    || pageViewer->visiblePageIndex() != pages->currentIndex()) return 64;
                pageEditor->nameInput()->setText(QStringLiteral("Новый экран · проверка UX"));
                pageEditor->renamePageButton()->click();
                if (pages->pageAt(pages->currentIndex()).title
                    != QStringLiteral("Новый экран · проверка UX")) return 65;
                pageEditor->duplicatePageButton()->click();
                if (pages->pageCount() != composition.hmiPages.size() + 2
                    || !pages->pageAt(pages->currentIndex()).title.contains(QStringLiteral("копия")))
                    return 66;
                miltech::ui::HmiDeliveryCollection restoredPages(composition.id);
                if (!restoredPages.loadFromJson(pages->toJson(), &reason)
                    || restoredPages.pageCount() != pages->pageCount()) return 67;
                const QByteArray beforeInvalid = pages->toJson();
                miltech::ui::HmiDeliveryCollection otherDelivery(QStringLiteral("another_delivery"));
                if (otherDelivery.loadFromJson(beforeInvalid, &reason)) return 68;
                if (!pages->deletePage(pages->currentIndex())
                    || pages->pageCount() != composition.hmiPages.size() + 1) return 69;
                if (pageEditor->pageSelector()->count() != pages->pageCount()
                    || pageViewer->pageSelector()->count() != pages->pageCount()) return 70;
            }
            nav.at(0)->click();
            QApplication::processEvents();
        }
        for (const QSize size : {QSize(800, 600), QSize(1024, 768), QSize(1366, 768), QSize(1600, 900)}) {
            window.resize(size);
            for (int variant = 0; variant < selector->count(); ++variant) {
                selector->setCurrentIndex(variant);
                QApplication::processEvents();
                if (window.size() != size || !window.rect().contains(
                        QRect(selector->mapTo(&window, QPoint()), selector->size()))
                    || selector->height() < selector->sizeHint().height()) {
                    return 41;
                }
                const auto nav = window.findChildren<QPushButton*>(QStringLiteral("navButton"));
                auto* navScroll = window.findChild<QScrollArea*>(QStringLiteral("ui.navigationScroll"));
                if (!navScroll || nav.isEmpty()) return 42;
                for (int n = 0; n < nav.size(); ++n) {
                    navScroll->ensureWidgetVisible(nav.at(n));
                    nav.at(n)->click();
                    QApplication::processEvents();
                    if (stack->currentIndex() != n || stack->width() <= 0 || stack->height() <= 0)
                        return 43;
                    const QRect navRect(nav.at(n)->mapTo(navScroll->viewport(), QPoint()), nav.at(n)->size());
                    if (!navScroll->viewport()->rect().contains(navRect)) return 47;
                }
                const auto composition = compositions.at(variant);
                if (!composition.isTu()) {
                    nav.at(composition.indexOf(QStringLiteral("overview")))->click();
                    QApplication::processEvents();
                    auto* cards = window.findChild<QWidget*>(QStringLiteral("ui.overviewCards"));
                    auto* grid = cards ? qobject_cast<QGridLayout*>(cards->layout()) : nullptr;
                    if (!grid || grid->count() != 3) return 44;
                    const QRect first = grid->itemAt(0)->geometry();
                    const QRect second = grid->itemAt(1)->geometry();
                    if (cards->width() < 620 ? second.top() <= first.top() : second.left() <= first.left())
                        return 45;
                    const int equipmentIndex = composition.indexOf(QStringLiteral("equipment"));
                    if (equipmentIndex >= 0) {
                        nav.at(equipmentIndex)->click();
                        QApplication::processEvents();
                        auto* equipment = window.findChild<miltech::ui::EquipmentOverview*>();
                        auto* layout = equipment ? qobject_cast<QBoxLayout*>(equipment->layout()) : nullptr;
                        if (!layout || (equipment->width() < 650
                            ? layout->direction() != QBoxLayout::TopToBottom
                            : layout->direction() != QBoxLayout::LeftToRight)) return 46;
                    }
                }
            }
        }
        if (miltech::ui::loadPreviewComposition(QStringLiteral(":/missing.json")).valid()) return 26;
        QTemporaryDir testDirectory;
        if (!testDirectory.isValid()) return 27;
        const QDir testDir(testDirectory.path());
        for (const QString& name : QDir(QStringLiteral(":/ui/manifests")).entryList({QStringLiteral("*.json")}, QDir::Files)) {
            if (!QFile::copy(QStringLiteral(":/ui/manifests/") + name, testDir.filePath(name))) return 28;
        }
        if (!QFile::copy(QStringLiteral(":/ui-test/fourth.json"), testDir.filePath(QStringLiteral("fourth.json")))) return 29;
        QFile invalid(testDir.filePath(QStringLiteral("invalid.json")));
        if (!invalid.open(QFile::WriteOnly) || invalid.write("{invalid") < 0) return 30;
        invalid.close();
        if (miltech::ui::loadPreviewComposition(invalid.fileName()).valid()) return 31;
        QFile forbidden(testDir.filePath(QStringLiteral("forbidden.json")));
        if (!forbidden.open(QFile::WriteOnly)) return 36;
        forbidden.write(R"({"schema":1,"id":"bad_tu","title":"Bad TU","kind":"tu","demo":{"selectorTitle":"Bad","name":"Bad","dut":"Bad","sample":"reference","equipment":[],"capabilities":[]},"screens":[{"id":"tu.prepare","title":"Prepare","group":"primary"},{"id":"tu.run","title":"Run","group":"primary"},{"id":"tu.result","title":"Result","group":"primary"},{"id":"configurator","title":"Config","group":"service"}],"workspaceComponents":[]})");
        forbidden.close();
        if (miltech::ui::loadPreviewComposition(forbidden.fileName()).valid()) return 37;
        UiPreviewWindow emptyWindow(testDir.filePath(QStringLiteral("missing-directory")));
        auto* emptyStack = emptyWindow.findChild<QStackedWidget*>();
        if (!emptyStack || emptyStack->count() != 1) return 38;
        UiPreviewWindow testWindow(testDir.path());
        auto* testSelector = testWindow.findChild<QComboBox*>(QStringLiteral("ui.variantSelector"));
        auto* testStack = testWindow.findChild<QStackedWidget*>();
        if (!testSelector || !testStack || testSelector->count() != selector->count() + 1) return 32;
        const int fourth = testSelector->findData(QStringLiteral("test_delivery"));
        if (fourth < 0) return 33;
        testSelector->setCurrentIndex(fourth);
        const auto testNav = testWindow.findChildren<QPushButton*>(QStringLiteral("navButton"));
        if (testNav.size() != 3 || testStack->count() != 3) return 34;
        QPushButton* openWorkspace = nullptr;
        for (auto* button : testWindow.findChildren<QPushButton*>())
            if (button->accessibleName() == QStringLiteral("Перейти в рабочую область")) openWorkspace = button;
        if (!openWorkspace) return 39;
        openWorkspace->click();
        if (testStack->currentIndex() != 2) return 40;
        for (int i = 0; i < testNav.size(); ++i) {
            testNav.at(i)->click();
            if (testStack->currentIndex() != i) return 35;
        }
        return 0;
    }

    const int snapshotArg = args.indexOf(QStringLiteral("--ui-snapshot-dir"));
    if (snapshotArg >= 0) {
        if (snapshotArg + 1 >= args.size()) return 20;
        const QString target = args.at(snapshotArg + 1);
        if (!QDir().mkpath(target)) return 21;
        auto* selector = window.findChild<QComboBox*>(QStringLiteral("ui.variantSelector"));
        if (!selector) return 22;
        for (int mode = 0; mode < selector->count(); ++mode) {
            selector->setCurrentIndex(mode);
            const QString prefix = selector->itemData(mode).toString();
            auto nav = window.findChildren<QPushButton*>(QStringLiteral("navButton"));
            const int captured = nav.size();
            for (int page = 0; page < captured; ++page) {
                nav.at(page)->click();
                for (const QSize size : {QSize(800, 600), QSize(1024, 768), QSize(1366, 768), QSize(1600, 900)}) {
                    window.resize(size);
                    QApplication::processEvents();
                    const QString filename = QStringLiteral("%1/%2-page%3-%4x%5.png")
                        .arg(target, prefix)
                        .arg(page)
                        .arg(size.width())
                        .arg(size.height());
                    if (!window.grab().save(filename, "PNG")) return 23;
                }
            }
        }
        return 0;
    }
    return app.exec();
}
