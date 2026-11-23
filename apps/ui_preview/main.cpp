#include "components.h"

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
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QSettings>
#include <QStackedWidget>
#include <QStringList>
#include <QStyleFactory>
#include <QTableWidget>
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

DeliveryFixture fixture(int mode)
{
    if (mode == 1) {
        return {
            QStringLiteral("reference_device"),
            QStringLiteral("Эталонный имитатор"),
            QStringLiteral("REFERENCE_DEVICE"),
            {QStringLiteral("ref-sim · источник параметров")},
            {QStringLiteral("Просмотр параметров"),
             QStringLiteral("Инженерная проверка на симуляторе")}
        };
    }
    return {
        QStringLiteral("ktma"),
        QStringLiteral("КТМА"),
        QStringLiteral("УБСИ"),
        {QStringLiteral("AKIP 1160 · питание"),
         QStringLiteral("ISD HTTP · коммутация"),
         QStringLiteral("V7 VISA · измерение"),
         QStringLiteral("KTMA UDP · адаптер")},
        {QStringLiteral("Мониторинг сигналов"),
         QStringLiteral("Управление оборудованием"),
         QStringLiteral("Производственные операции"),
         QStringLiteral("Редактирование сценариев")}
    };
}

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
    UiPreviewWindow()
    {
        setWindowTitle(QStringLiteral("MilTechStation · UI Preview"));
        setMinimumSize(1120, 720);
        resize(1510, 900);
        setAnimated(false);

        auto* shell = new QWidget;
        shell->setObjectName(QStringLiteral("shell"));
        auto* root = new QVBoxLayout(shell);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto* header = new QFrame;
        header->setObjectName(QStringLiteral("header"));
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(24, 15, 24, 15);
        headerLayout->setSpacing(17);
        headerLayout->addWidget(label(QStringLiteral("MilTechStation"), "appName"));
        headerLayout->addStretch();
        context_ = label(QStringLiteral("КТМА"), "muted");
        headerLayout->addWidget(context_);
        modeSelector_ = new QComboBox;
        modeSelector_->setObjectName(QStringLiteral("ui.variantSelector"));
        modeSelector_->addItem(QStringLiteral("Полная · КТМА"));
        modeSelector_->addItem(QStringLiteral("Полная · эталонный имитатор"));
        modeSelector_->addItem(QStringLiteral("ТУ · УБСИ"));
        modeSelector_->setAccessibleName(QStringLiteral("Вариант интерфейса"));
        headerLayout->addWidget(modeSelector_);
        headerLayout->addWidget(label(QStringLiteral("UI PREVIEW · БЕЗ ОБОРУДОВАНИЯ"), "demo"));
        root->addWidget(header);

        auto* workspace = new QHBoxLayout;
        workspace->setContentsMargins(0, 0, 0, 0);
        workspace->setSpacing(0);
        auto* sidebar = new QFrame;
        sidebar->setObjectName(QStringLiteral("sidebar"));
        sidebar->setFixedWidth(210);
        navLayout_ = new QVBoxLayout(sidebar);
        navLayout_->setContentsMargins(12, 17, 12, 14);
        navLayout_->setSpacing(6);
        workspace->addWidget(sidebar);
        stack_ = new QStackedWidget;
        workspace->addWidget(stack_, 1);
        root->addLayout(workspace, 1);

        auto* footer = new QFrame;
        footer->setObjectName(QStringLiteral("footer"));
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

        parameterDock_ = new QDockWidget(QStringLiteral("Параметры"), host);
        parameterDock_->setObjectName(QStringLiteral("uiPreview.parameters"));
        parameterDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea
                                        | Qt::BottomDockWidgetArea);
        parameterDock_->setMinimumWidth(260);
        parameterPanel_ = new miltech::ui::ParameterPanel(reference);
        parameterDock_->setWidget(parameterPanel_);
        host->addDockWidget(Qt::RightDockWidgetArea, parameterDock_);

        eventDock_ = new QDockWidget(QStringLiteral("Журнал событий"), host);
        eventDock_->setObjectName(QStringLiteral("uiPreview.events"));
        eventDock_->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea
                                    | Qt::RightDockWidgetArea);
        eventLog_ = new miltech::ui::EventLogPanel;
        eventDock_->setWidget(eventLog_);
        host->addDockWidget(Qt::BottomDockWidgetArea, eventDock_);
        eventDock_->setMinimumHeight(145);
    }

    QString workspaceKey() const
    {
        return QStringLiteral("layouts/%1/v2").arg(fixture(modeIndex_).id);
    }

    void saveWorkspace()
    {
        if (modeIndex_ == 2 || currentPage_ != 1 || !dockHost_) return;
        QSettings settings(QStringLiteral("MilTechStation"),
                           QStringLiteral("UI-Preview"));
        settings.setValue(workspaceKey(), dockHost_->saveState(kLayoutVersion));
    }

    void restoreWorkspace()
    {
        if (!dockHost_) return;
        parameterDock_->show();
        eventDock_->show();
        QSettings settings(QStringLiteral("MilTechStation"),
                           QStringLiteral("UI-Preview"));
        const QByteArray state = settings.value(workspaceKey()).toByteArray();
        if (!state.isEmpty() && !dockHost_->restoreState(state, kLayoutVersion))
            eventLog_->appendUiEvent(QStringLiteral("Предыдущая компоновка несовместима; используется текущая"));
    }

    void resetWorkspaceLayout()
    {
        if (!dockHost_ || !parameterDock_ || !eventDock_) return;
        QSettings settings(QStringLiteral("MilTechStation"),
                           QStringLiteral("UI-Preview"));
        settings.remove(workspaceKey());
        dockHost_->removeDockWidget(parameterDock_);
        dockHost_->removeDockWidget(eventDock_);
        parameterDock_->setFloating(false);
        eventDock_->setFloating(false);
        dockHost_->addDockWidget(Qt::RightDockWidgetArea, parameterDock_);
        dockHost_->addDockWidget(Qt::BottomDockWidgetArea, eventDock_);
        parameterDock_->show();
        eventDock_->show();
        if (eventLog_)
            eventLog_->appendUiEvent(QStringLiteral("Восстановлена исходная компоновка"));
    }

    void setPage(int index)
    {
        if (currentPage_ == 1 && modeIndex_ != 2) saveWorkspace();
        if (index < 0 || index >= stack_->count()) return;
        currentPage_ = index;
        stack_->setCurrentIndex(index);
        for (int n = 0; n < navButtons_.size(); ++n) {
            navButtons_.at(n)->setProperty("navActive", n == index);
            navButtons_.at(n)->setChecked(n == index);
            navButtons_.at(n)->style()->unpolish(navButtons_.at(n));
            navButtons_.at(n)->style()->polish(navButtons_.at(n));
        }
        const bool fullWorkspace = modeIndex_ != 2 && index == 1;
        if (fullWorkspace) restoreWorkspace();
        else {
            if (parameterDock_) parameterDock_->hide();
            if (eventDock_) eventDock_->hide();
        }
        if (eventLog_)
            eventLog_->appendUiEvent(
                QStringLiteral("Раздел: %1").arg(navButtons_.at(index)->text()));
    }

    void addNavigation(const QString& title, QWidget* screen)
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
        stack_->addWidget(screen);
        connect(button, &QPushButton::clicked, this, [this, index] {
            setPage(index);
        });
    }

    void resetNavigation()
    {
        navButtons_.clear();
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
    }

    void switchMode(int index)
    {
        saveWorkspace();
        modeIndex_ = index;
        currentPage_ = -1;
        resetNavigation();
        const bool tu = modeIndex_ == 2;
        const DeliveryFixture spec = fixture(index);
        context_->setText(tu ? QStringLiteral("ТУ / УБСИ") : spec.name);
        navLayout_->addWidget(label(tu ? QStringLiteral("ПРОЦЕДУРА")
                                       : QStringLiteral("РАЗДЕЛЫ"), "eyebrow"));
        if (tu) {
            addNavigation(QStringLiteral("1  Подготовка"), makeTuPrepare());
            addNavigation(QStringLiteral("2  Испытание"), makeTuRun());
            addNavigation(QStringLiteral("3  Результат"), makeTuResult());
        } else {
            addNavigation(QStringLiteral("Обзор"), makeOverview(spec));
            addNavigation(QStringLiteral("Рабочая область"), makeWorkspace(spec));
            addNavigation(QStringLiteral("Оборудование"), makeEquipment(spec));
            addNavigation(QStringLiteral("Сценарии"), makeScenarios(spec));
            addNavigation(QStringLiteral("Результаты"), makeResults());
        }
        navLayout_->addStretch();
        navLayout_->addWidget(label(QStringLiteral("Alt+1…5 · навигация"), "muted"));
        if (eventLog_)
            eventLog_->appendUiEvent(
                QStringLiteral("Выбран интерфейс: %1").arg(spec.name));
        setPage(0);
    }

    QWidget* makeOverview(const DeliveryFixture& spec)
    {
        Page p = page(QStringLiteral("СТАНЦИЯ / ОБЗОР"), spec.name,
                      QStringLiteral("Контекст поставки и точка входа в инженерную работу"));
        auto* metrics = new QGridLayout;
        metrics->setSpacing(12);
        metrics->addWidget(card(QStringLiteral("ИЗДЕЛИЕ"), spec.dut,
                                QStringLiteral("Тип из UI-фикстуры")), 0, 0);
        metrics->addWidget(card(QStringLiteral("СТЕНД"), QStringLiteral("Не опрошен"),
                                QStringLiteral("Связь с оборудованием отсутствует")), 0, 1);
        metrics->addWidget(card(QStringLiteral("АКТИВНЫЙ ЗАПУСК"), QStringLiteral("Нет"),
                                QStringLiteral("Сценарии не подключены")), 0, 2);
        p.body->addLayout(metrics);
        p.body->addWidget(label(QStringLiteral("Рабочие возможности"), "section"));
        for (const QString& capability : spec.capabilities) {
            p.body->addWidget(item(capability, QStringLiteral("Доступность определяется поставкой; в прототипе действия отключены")));
        }
        auto* open = new QPushButton(QStringLiteral("Открыть рабочую область  →"));
        open->setAccessibleName(QStringLiteral("Перейти в рабочую область"));
        connect(open, &QPushButton::clicked, this, [this] { setPage(1); });
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
        dockHost_->setMinimumSize(520, 420);
        createDocks(dockHost_, spec.id == QStringLiteral("reference_device"));

        Page p = page(QStringLiteral("ИНЖЕНЕРНАЯ СРЕДА"),
                      QStringLiteral("Рабочая область"),
                      QStringLiteral("Параметры, графики и схема представления · только UI-демонстрация"));

        auto* topRow = new QHBoxLayout;
        topRow->addWidget(label(
            QStringLiteral("ПОСТАВКА · %1  /  ФИЗИЧЕСКИЙ СТЕНД НЕ ПОДКЛЮЧЁН").arg(spec.name),
            "eyebrow"), 1);
        auto* reset = new QPushButton(QStringLiteral("Исходная компоновка"));
        reset->setObjectName(QStringLiteral("ui.resetLayout"));
        reset->setAccessibleName(QStringLiteral("Восстановить исходное расположение инженерных панелей"));
        connect(reset, &QPushButton::clicked, this, [this] { resetWorkspaceLayout(); });
        topRow->addWidget(reset);
        p.body->addLayout(topRow);

        auto* chart = new miltech::ui::TrendPanel;
        p.body->addWidget(chart);
        connect(chart->syntheticToggle(), &QCheckBox::toggled,
                parameterPanel_, &miltech::ui::ParameterPanel::setSyntheticVisible);
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

        p.body->addWidget(new miltech::ui::SignalTopology(
            spec.id == QStringLiteral("reference_device")));
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
        auto* metrics = new QGridLayout;
        metrics->setSpacing(12);
        metrics->addWidget(card(QStringLiteral("ФАКТИЧЕСКОЕ"), QStringLiteral("—"),
                                QStringLiteral("Нет измерения")), 0, 0);
        metrics->addWidget(card(QStringLiteral("ДОПУСК"), QStringLiteral("—"),
                                QStringLiteral("Не загружен")), 0, 1);
        p.body->addLayout(metrics);
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

    QComboBox* modeSelector_ = nullptr;
    QLabel* context_ = nullptr;
    QVBoxLayout* navLayout_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QDockWidget* parameterDock_ = nullptr;
    QDockWidget* eventDock_ = nullptr;
    QMainWindow* dockHost_ = nullptr;
    miltech::ui::ParameterPanel* parameterPanel_ = nullptr;
    miltech::ui::EventLogPanel* eventLog_ = nullptr;
    QVector<QPushButton*> navButtons_;
    int modeIndex_ = -1;
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
    UiPreviewWindow window;
    window.show();

    const QStringList args = app.arguments();
    const bool automated = args.contains(QStringLiteral("--ui-smoke"))
                        || args.contains(QStringLiteral("--ui-snapshot-dir"));
    if (automated) {
        // Automated UI checks must not write to the operator's ordinary registry.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QDir::tempPath() + QStringLiteral("/MilTechStationUiAutomation"));
    }

    if (args.contains(QStringLiteral("--ui-smoke"))) {
        auto* selector = window.findChild<QComboBox*>(QStringLiteral("ui.variantSelector"));
        auto* stack = window.findChild<QStackedWidget*>();
        if (!selector || !stack) return 2;
        for (int index = 0; index < 3; ++index) {
            selector->setCurrentIndex(index);
            QApplication::processEvents();
            if (stack->count() != (index == 2 ? 3 : 5)) return 3;
            const auto docks = window.findChildren<QDockWidget*>();
            if (index == 2) {
                if (!docks.isEmpty()
                    || window.findChild<miltech::ui::ParameterPanel*>()
                    || window.findChild<miltech::ui::TrendPanel*>()) return 4;
                continue;
            }
            if (docks.size() != 2) return 5;
            const auto nav = window.findChildren<QPushButton*>(QStringLiteral("navButton"));
            if (nav.size() != 5) return 6;
            nav.at(1)->click(); // Engineering workspace.
            QApplication::processEvents();
            auto* params = window.findChild<miltech::ui::ParameterPanel*>();
            auto* chart = window.findChild<miltech::ui::TrendPanel*>();
            auto* equipment = window.findChild<miltech::ui::EquipmentOverview*>();
            auto* events = window.findChild<miltech::ui::EventLogPanel*>();
            if (!params || !chart || !equipment || !events) return 7;
            if (equipment->equipmentCount() != (index == 0 ? 4 : 1)) return 8;
            if (params->filteredRowCount() != (index == 0 ? 4 : 3)) return 9;
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
            nav.at(0)->click();
            QApplication::processEvents();
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
        for (int mode = 0; mode < 3; ++mode) {
            selector->setCurrentIndex(mode);
            const QString prefix = mode == 0 ? QStringLiteral("ktma")
                                 : mode == 1 ? QStringLiteral("reference")
                                             : QStringLiteral("tu");
            auto nav = window.findChildren<QPushButton*>(QStringLiteral("navButton"));
            const int captured = mode == 2 ? 3 : 3;
            for (int page = 0; page < captured; ++page) {
                nav.at(page)->click();
                for (const QSize size : {QSize(1366, 768), QSize(1600, 900)}) {
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
