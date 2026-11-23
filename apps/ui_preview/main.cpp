#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
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

constexpr int kLayoutVersion = 1;

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

QTableWidget* equipmentTable(const QStringList& names)
{
    auto* table = new QTableWidget(names.size(), 3);
    table->setHorizontalHeaderLabels({QStringLiteral("КОМПОНЕНТ"),
                                      QStringLiteral("СОСТОЯНИЕ"),
                                      QStringLiteral("ДЕЙСТВИЕ")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setFocusPolicy(Qt::StrongFocus);
    for (int row = 0; row < names.size(); ++row) {
        table->setItem(row, 0, new QTableWidgetItem(names.at(row)));
        table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("Не опрошен")));
        table->setItem(row, 2, new QTableWidgetItem(QStringLiteral("Недоступно")));
        table->setRowHeight(row, 48);
    }
    const int expectedHeight = static_cast<int>(names.size()) * 48 + 42;
    table->setMinimumHeight(qMax(190, expectedHeight));
    table->setAccessibleName(QStringLiteral("Компоненты оборудования из макета поставки"));
    return table;
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
        setDockOptions(QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);
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

        createDocks();
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
    void createDocks()
    {
        parameterDock_ = new QDockWidget(QStringLiteral("Параметры"), this);
        parameterDock_->setObjectName(QStringLiteral("uiPreview.parameters"));
        parameterDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea
                                        | Qt::BottomDockWidgetArea);
        parameterDock_->setMinimumWidth(210);
        parameterDock_->setWidget(empty(QStringLiteral("Нет измерений"),
                                        QStringLiteral("Панель ожидает интерфейс представления данных")));
        addDockWidget(Qt::RightDockWidgetArea, parameterDock_);

        eventDock_ = new QDockWidget(QStringLiteral("Журнал событий"), this);
        eventDock_->setObjectName(QStringLiteral("uiPreview.events"));
        eventDock_->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea
                                    | Qt::RightDockWidgetArea);
        eventDock_->setWidget(empty(QStringLiteral("Нет событий"),
                                   QStringLiteral("Пока показывается только макет журнала")));
        addDockWidget(Qt::BottomDockWidgetArea, eventDock_);
        parameterDock_->hide();
        eventDock_->hide();
    }

    QString workspaceKey() const
    {
        return QStringLiteral("layouts/%1/v1").arg(fixture(modeIndex_).id);
    }

    void saveWorkspace()
    {
        if (modeIndex_ == 2 || currentPage_ != 1) return;
        QSettings settings(QStringLiteral("MilTechStation"),
                           QStringLiteral("UI-Preview"));
        settings.setValue(workspaceKey(), saveState(kLayoutVersion));
    }

    void restoreWorkspace()
    {
        parameterDock_->show();
        eventDock_->show();
        QSettings settings(QStringLiteral("MilTechStation"),
                           QStringLiteral("UI-Preview"));
        const QByteArray state = settings.value(workspaceKey()).toByteArray();
        if (!state.isEmpty()) restoreState(state, kLayoutVersion);
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
            parameterDock_->hide();
            eventDock_->hide();
        }
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
        Page p = page(QStringLiteral("ИНЖЕНЕРНАЯ СРЕДА"),
                      QStringLiteral("Рабочая область"),
                      QStringLiteral("Док-панели можно перемещать, закреплять и объединять во вкладки"));
        p.body->addWidget(card(QStringLiteral("ПРОФИЛЬ РАБОТЫ"),
                               QStringLiteral("Диагностика"),
                               QStringLiteral("Компоновка хранится отдельно для %1").arg(spec.name)));
        p.body->addWidget(empty(QStringLiteral("Область измерений"),
                                QStringLiteral("Графики и мнемосхемы появятся после подключения UI-моделей")));
        p.body->addWidget(item(QStringLiteral("Работа с панелями"),
                               QStringLiteral("Попробуйте перетащить панели «Параметры» и «Журнал событий». Их позиции сохраняются по поставкам.")));
        p.body->addStretch();
        return p.widget;
    }

    QWidget* makeEquipment(const DeliveryFixture& spec)
    {
        Page p = page(QStringLiteral("СТЕНД / КОМПОНЕНТЫ"),
                      QStringLiteral("Оборудование"),
                      QStringLiteral("Состав — из описания поставки; статусы в прототипе всегда неизвестны"));
        p.body->addWidget(equipmentTable(spec.equipment));
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

    if (app.arguments().contains(QStringLiteral("--ui-smoke"))) {
        auto* selector = window.findChild<QComboBox*>();
        auto* stack = window.findChild<QStackedWidget*>();
        if (!selector || !stack) return 2;
        for (int index = 0; index < 3; ++index) {
            selector->setCurrentIndex(index);
            QApplication::processEvents();
            if (stack->count() != (index == 2 ? 3 : 5)) return 3;
        }
        const auto docks = window.findChildren<QDockWidget*>();
        if (docks.size() != 2 || !docks.at(0)->isHidden()
                              || !docks.at(1)->isHidden()) return 4;
        return 0;
    }
    return app.exec();
}
