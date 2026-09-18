#include "test_page.h"
#include "tu_flow_widget.h"
#include "ubsi_measurement_views.h"
#include "ubsi_ui_model.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

using namespace ubsi::ui;

namespace {

QString appStyle()
{
    return QStringLiteral(R"QSS(
QWidget#operatorTestPage { background:#08131d; color:#eaf4fb; font-family:"Segoe UI"; font-size:13px; }
QWidget { color:#eaf4fb; }
QFrame[panel="true"] { background:#102333; border:1px solid #264257; border-radius:8px; }
QFrame[flatPanel="true"] { background:#0e1e2c; border:0; border-right:1px solid #1a3346; }
QFrame[tuRow="true"] { background:#0e1e2c; border:1px solid #1a3346; border-radius:6px; }
QFrame[tuRowActive="true"] { background:#123b58; border:1px solid #2e7de9; border-left:4px solid #58a5ff; border-radius:6px; }
QLabel[muted="true"] { color:#8ea6b7; }
QLabel[caption="true"] { color:#8ea6b7; font-size:11px; font-weight:700; }
QLabel[tuId="true"] { color:#8ea6b7; font-size:10px; font-weight:700; }
QLabel[tuTitle="true"] { color:#eaf4fb; font-size:11px; }
QPushButton { background:#132a3d; border:1px solid #264257; border-radius:7px; padding:8px 13px; color:#eaf4fb; }
QPushButton:hover { border-color:#58a5ff; background:#17334a; }
QPushButton#primary { background:#2e7de9; border-color:#58a5ff; font-weight:700; }
QPushButton#danger { background:#3a1d24; border-color:#7e3340; color:#ffd8dc; }
QPushButton:disabled { color:#61788a; background:#0e1e2c; border-color:#1a3346; }
QLineEdit,QComboBox { background:#0e1e2c; border:1px solid #264257; border-radius:6px; padding:8px; min-height:20px; }
QTableWidget { background:#0e1e2c; alternate-background-color:#102333; border:1px solid #264257; gridline-color:#1a3346; selection-background-color:#123b58; selection-color:#eaf4fb; }
QHeaderView::section { background:#102333; color:#8ea6b7; border:0; border-bottom:1px solid #264257; padding:8px; font-weight:700; }
QStackedWidget { background:#08131d; }
)QSS");
}

QFrame* makePanel(QWidget* parent = nullptr)
{
    auto* frame = new QFrame(parent);
    frame->setProperty("panel", true);
    return frame;
}

QLabel* caption(const QString& text, QWidget* parent = nullptr)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("caption", true);
    return label;
}

QLabel* muted(const QString& text, QWidget* parent = nullptr)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("muted", true);
    label->setWordWrap(true);
    return label;
}

QLabel* heading(const QString& text, int point, QWidget* parent = nullptr)
{
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setPointSize(point);
    font.setBold(true);
    label->setFont(font);
    return label;
}

bool validOperatorName(const QString& value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[А-ЯЁ][а-яё-]+\\s[А-ЯЁ]\\.[А-ЯЁ]\\.$"));
    return pattern.match(value.trimmed()).hasMatch();
}

QString scenarioForScope(const QString& scope)
{
    if (scope == QStringLiteral("ЯЛК-96")) return QStringLiteral("PROD_YALK");
    if (scope == QStringLiteral("ЯТП")) return QStringLiteral("PROD_YTP");
    if (scope == QStringLiteral("ЯВП-8")) return QStringLiteral("PROD_YVP");
    return QStringLiteral("PROD_FULL");
}

QString scopeDisplay(const QString& scope)
{
    return scope == QStringLiteral("УБСИ ПО ТУ") ? QStringLiteral("Полная УБСИ") : scope;
}

QString scenarioDisplay(const QString& scope)
{
    if (scope == QStringLiteral("ЯВП-8")) return QStringLiteral("ЯВП-8 · V7 / ИСД");
    if (scope == QStringLiteral("ЯТП")) return QStringLiteral("ЯТП · 30 каналов · 0 / 120 / 240 Ом");
    if (scope == QStringLiteral("ЯЛК-96")) return QStringLiteral("ЯЛК-96 · полный контур");
    return QStringLiteral("Полная производственная проверка УБСИ");
}

QString sectionText(Procedure procedure)
{
    switch (procedure) {
    case Procedure::Preparation: return QStringLiteral("Подготовка");
    case Procedure::Power: return QStringLiteral("Питание");
    case Procedure::YalkInitial: return QStringLiteral("Обрыв / исходное состояние");
    case Procedure::YalkAnalog: return QStringLiteral("Аналоговые каналы");
    case Procedure::YalkContact: return QStringLiteral("Контактные сигналы");
    case Procedure::YalkOverload: return QStringLiteral("Перегрузка ±12 В");
    case Procedure::YalkReference: return QStringLiteral("Эталон 6,2 В");
    case Procedure::Ytp: return QStringLiteral("ЯТП");
    case Procedure::Yvp: return QStringLiteral("ЯВП-8");
    case Procedure::Finish: return QStringLiteral("Завершение");
    }
    return {};
}

int runtimePage(Procedure procedure)
{
    switch (procedure) {
    case Procedure::Power: return 0;
    case Procedure::YalkInitial: return 1;
    case Procedure::YalkAnalog: return 2;
    case Procedure::YalkContact: return 3;
    case Procedure::YalkOverload: return 4;
    case Procedure::YalkReference: return 5;
    case Procedure::Ytp: return 6;
    case Procedure::Yvp: return 7;
    case Procedure::Finish: return 8;
    case Procedure::Preparation: return 0;
    }
    return 0;
}

QString formatClock(qint64 milliseconds)
{
    const qint64 seconds = std::max<qint64>(0, milliseconds / 1000);
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    const qint64 remainder = seconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(remainder, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(remainder, 2, 10, QLatin1Char('0'));
}

int stateRank(VerificationState state)
{
    switch (state) {
    case VerificationState::Error: return 6;
    case VerificationState::NeNorma: return 5;
    case VerificationState::Stopped: return 4;
    case VerificationState::Incomplete: return 3;
    case VerificationState::Norma: return 2;
    case VerificationState::Pending: return 1;
    }
    return 0;
}

VerificationState worseState(VerificationState left, VerificationState right)
{
    return stateRank(right) > stateRank(left) ? right : left;
}

QString activeStepForNode(const QString& node, Procedure procedure)
{
    if (node.contains(QStringLiteral("start_stream")) || node == QStringLiteral("yalk_stream"))
        return QStringLiteral("Инициализация потока");
    if (node.contains(QStringLiteral("calibration"))) return QStringLiteral("Калибровка 97 / 99");
    if (node.contains(QStringLiteral("initial"))) return QStringLiteral("Обрыв / исходное состояние");
    if (node == QStringLiteral("yalk_channels")) return QStringLiteral("Аналоговые каналы");
    if (node.contains(QStringLiteral("contact"))) return QStringLiteral("Контактные сигналы");
    if (node.contains(QStringLiteral("overload"))) return QStringLiteral("Перегрузка ±12 В");
    if (node.contains(QStringLiteral("reference"))) return QStringLiteral("Эталон 6,2 В");
    if (node.contains(QStringLiteral("cleanup"))) return QStringLiteral("Безопасное завершение");
    return sectionText(procedure);
}

} // namespace

struct TestPage::Impl
{
    struct ScenarioInfo {
        bool available = false;
        bool diagnostic = false;
        QStringList required;
        QString detail;
    };

    struct EquipmentRow {
        int row = -1;
        bool ready = false;
        bool confirmation = false;
        QString connection;
    };

    struct TuRequirementRow {
        QString id;
        QString title;
        QFrame* frame = nullptr;
        QLabel* status = nullptr;
        VerificationState state = VerificationState::Pending;
        bool active = false;
    };

    explicit Impl(TestPage* owner) : q(owner)
    {
        q->setObjectName(QStringLiteral("operatorTestPage"));
        q->setStyleSheet(appStyle());

        root = new QVBoxLayout(q);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        pages = new QStackedWidget(q);
        root->addWidget(pages, 1);

        buildBridge();
        buildSession();
        buildPreparation();
        buildRuntime();
        buildTu();
        pages->setCurrentWidget(sessionPage);

        timer = new QTimer(q);
        timer->setInterval(250);
        QObject::connect(timer, &QTimer::timeout, q, [this] {
            if (!clock.isValid()) return;
            adapter.run.elapsedMs = clock.elapsed();
            elapsed->setText(formatClock(adapter.run.elapsedMs));
        });
    }

    void buildBridge()
    {
        bridge = new QWidget(q);
        bridge->hide();
        auto* layout = new QHBoxLayout(bridge);
        layout->setContentsMargins(0, 0, 0, 0);

        scopeCombo = new QComboBox(bridge);
        scopeCombo->setObjectName(QStringLiteral("testScope"));
        testCombo = new QComboBox(bridge);
        testCombo->setObjectName(QStringLiteral("testType"));
        modeCombo = new QComboBox(bridge);
        modeCombo->setObjectName(QStringLiteral("testMode"));
        modeCombo->addItem(QStringLiteral("Стенд — реальное оборудование"));

        partial = new QCheckBox(bridge);
        partial->setObjectName(QStringLiteral("allowPartial"));
        includeYvp = new QCheckBox(bridge);
        includeYvp->setObjectName(QStringLiteral("includeYvp"));
        includeYvp->setChecked(true);
        includeOverload = new QCheckBox(bridge);
        includeOverload->setChecked(true);
        includeSurvival = new QCheckBox(bridge);
        includeSurvival->setChecked(true);

        layout->addWidget(scopeCombo);
        layout->addWidget(testCombo);
        layout->addWidget(modeCombo);
        layout->addWidget(partial);
        layout->addWidget(includeYvp);
        layout->addWidget(includeOverload);
        layout->addWidget(includeSurvival);
        root->addWidget(bridge);
    }

    void buildSession()
    {
        sessionPage = new QWidget(pages);
        auto* outer = new QVBoxLayout(sessionPage);
        outer->setContentsMargins(56, 42, 56, 34);
        outer->setSpacing(18);

        auto* brandRow = new QHBoxLayout;
        brandRow->addWidget(caption(QStringLiteral("MILTECHSTATION / КТМА"), sessionPage));
        brandRow->addStretch();
        auto* mode = caption(QStringLiteral("ПРОИЗВОДСТВО"), sessionPage);
        mode->setStyleSheet(QStringLiteral("color:#58a5ff;"));
        brandRow->addWidget(mode);
        outer->addLayout(brandRow);

        outer->addWidget(heading(QStringLiteral("Производственная сессия"), 26, sessionPage));
        outer->addWidget(muted(
            QStringLiteral("Сформируйте очередь зарегистрированных изделий. Оператор, этап и объём относятся ко всей сессии."),
            sessionPage));

        auto* body = new QHBoxLayout;
        body->setSpacing(18);

        auto* registryPanel = makePanel(sessionPage);
        auto* registryLayout = new QVBoxLayout(registryPanel);
        registryLayout->setContentsMargins(16, 16, 16, 16);
        registryLayout->setSpacing(10);
        registryLayout->addWidget(caption(QStringLiteral("ЗАРЕГИСТРИРОВАННЫЕ УБСИ"), registryPanel));
        registrySearch = new QLineEdit(registryPanel);
        registrySearch->setObjectName(QStringLiteral("productionRegistrySearch"));
        registrySearch->setPlaceholderText(QStringLiteral("Поиск по заводскому №"));
        registrySearch->setClearButtonEnabled(true);
        registryLayout->addWidget(registrySearch);
        registry = new QTableWidget(0, 2, registryPanel);
        registry->setObjectName(QStringLiteral("productionRegistryTable"));
        registry->horizontalHeader()->hide();
        registry->verticalHeader()->hide();
        registry->setShowGrid(false);
        registry->setSelectionBehavior(QAbstractItemView::SelectRows);
        registry->setEditTriggers(QAbstractItemView::NoEditTriggers);
        registry->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        registry->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        registry->verticalHeader()->setDefaultSectionSize(42);
        registryLayout->addWidget(registry, 1);
        body->addWidget(registryPanel, 123);

        auto* queuePanel = makePanel(sessionPage);
        auto* queueLayout = new QVBoxLayout(queuePanel);
        queueLayout->setContentsMargins(16, 16, 16, 16);
        queueLayout->setSpacing(10);
        queueLayout->addWidget(caption(QStringLiteral("ОЧЕРЕДЬ ТЕКУЩЕЙ СЕССИИ"), queuePanel));
        queue = new QTableWidget(0, 3, queuePanel);
        queue->setObjectName(QStringLiteral("productionSessionTable"));
        queue->setHorizontalHeaderLabels({QStringLiteral("УБСИ"), QStringLiteral("Объём"), QStringLiteral("Состояние")});
        queue->verticalHeader()->hide();
        queue->setSelectionBehavior(QAbstractItemView::SelectRows);
        queue->setSelectionMode(QAbstractItemView::SingleSelection);
        queue->setEditTriggers(QAbstractItemView::NoEditTriggers);
        queue->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        queue->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        queue->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        queueLayout->addWidget(queue, 1);

        auto* tools = new QHBoxLayout;
        auto* up = new QPushButton(QStringLiteral("↑"), queuePanel);
        auto* down = new QPushButton(QStringLiteral("↓"), queuePanel);
        auto* remove = new QPushButton(QStringLiteral("Убрать"), queuePanel);
        auto* clear = new QPushButton(QStringLiteral("Очистить"), queuePanel);
        tools->addWidget(up);
        tools->addWidget(down);
        tools->addWidget(remove);
        tools->addWidget(clear);
        tools->addStretch();
        queueLayout->addLayout(tools);
        body->addWidget(queuePanel, 92);

        auto* config = makePanel(sessionPage);
        auto* configLayout = new QVBoxLayout(config);
        configLayout->setContentsMargins(16, 16, 16, 16);
        configLayout->setSpacing(8);
        configLayout->addWidget(caption(QStringLiteral("ПАРАМЕТРЫ СЕССИИ"), config));
        configLayout->addWidget(caption(QStringLiteral("ОПЕРАТОР"), config));
        operatorSelector = new QComboBox(config);
        operatorSelector->setObjectName(QStringLiteral("productionOperatorSelector"));
        operatorSelector->addItem(QStringLiteral("Выберите оператора"), QString());
        configLayout->addWidget(operatorSelector);
        auto* addOperator = new QPushButton(QStringLiteral("+ Добавить оператора"), config);
        configLayout->addWidget(addOperator);

        configLayout->addWidget(caption(QStringLiteral("ПРОИЗВОДСТВЕННЫЙ ЭТАП"), config));
        stage = new QComboBox(config);
        stage->setObjectName(QStringLiteral("productionStage"));
        stage->addItem(QStringLiteral("Первичная проверка"), QStringLiteral("Primary"));
        stage->addItem(QStringLiteral("Климатические испытания — нормальные условия"), QStringLiteral("ClimateNormal"));
        stage->addItem(QStringLiteral("Климатические испытания — отрицательная температура"), QStringLiteral("ClimateMinus"));
        stage->addItem(QStringLiteral("Климатические испытания — повышенная температура"), QStringLiteral("ClimatePlus"));
        stage->addItem(QStringLiteral("После заливки — нормальные условия"), QStringLiteral("PottingClimateNormal"));
        stage->addItem(QStringLiteral("После заливки — повышенная температура"), QStringLiteral("PottingClimatePlus"));
        stage->addItem(QStringLiteral("После заливки — отрицательная температура"), QStringLiteral("PottingClimateMinus"));
        configLayout->addWidget(stage);

        configLayout->addWidget(caption(QStringLiteral("ОБЪЁМ ПРОВЕРКИ"), config));
        scopeGroup = new QButtonGroup(q);
        scopeGroup->setExclusive(true);
        const QVector<QPair<QString, QString>> scopes = {
            {QStringLiteral("Полная УБСИ"), QStringLiteral("УБСИ ПО ТУ")},
            {QStringLiteral("ЯЛК-96"), QStringLiteral("ЯЛК-96")},
            {QStringLiteral("ЯТП"), QStringLiteral("ЯТП")},
            {QStringLiteral("ЯВП-8"), QStringLiteral("ЯВП-8")}
        };
        for (int index = 0; index < scopes.size(); ++index) {
            auto* button = new QPushButton(scopes[index].first, config);
            button->setCheckable(true);
            button->setProperty("scope", scopes[index].second);
            button->setMinimumHeight(48);
            scopeGroup->addButton(button, index);
            scopeButtons.push_back(button);
            configLayout->addWidget(button);
        }
        scopeGroup->button(0)->setChecked(true);
        configLayout->addStretch();
        body->addWidget(config, 72);

        outer->addLayout(body, 1);

        auto* action = makePanel(sessionPage);
        auto* actionLayout = new QHBoxLayout(action);
        actionLayout->setContentsMargins(16, 12, 16, 12);
        sessionSummary = muted(
            QStringLiteral("0 изделий · оператор не выбран · Первичная проверка · Полная УБСИ"), action);
        actionLayout->addWidget(sessionSummary, 1);
        enterPreparation = new QPushButton(QStringLiteral("Перейти к подготовке"), action);
        enterPreparation->setObjectName(QStringLiteral("enterPreparation"));
        enterPreparation->setProperty("primary", true);
        enterPreparation->setStyleSheet(QStringLiteral("background:#2e7de9;border-color:#58a5ff;font-weight:700;"));
        enterPreparation->setEnabled(false);
        actionLayout->addWidget(enterPreparation);
        outer->addWidget(action);

        serialEdit = new QLineEdit(sessionPage);
        serialEdit->setObjectName(QStringLiteral("objectSerial"));
        serialEdit->hide();
        operatorEdit = new QLineEdit(sessionPage);
        operatorEdit->setObjectName(QStringLiteral("operatorName"));
        operatorEdit->hide();

        pages->addWidget(sessionPage);

        QObject::connect(registrySearch, &QLineEdit::textChanged, q, [this] { refreshRegistry(); });
        QObject::connect(operatorSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), q,
                         [this](int) {
                             operatorEdit->setText(operatorSelector->currentData().toString());
                             refreshSessionSummary();
                             syncTuOperators();
                         });
        QObject::connect(stage, QOverload<int>::of(&QComboBox::currentIndexChanged), q,
                         [this](int) { refreshSessionSummary(); });
        QObject::connect(scopeGroup, &QButtonGroup::idClicked, q,
                         [this](int id) { selectScope(id); });
        QObject::connect(enterPreparation, &QPushButton::clicked, q, [this] {
            if (queue->rowCount() == 0) return;
            int row = queue->currentRow();
            if (row < 0) row = 0;
            activeRow = row;
            activeSerial = queue->item(row, 0)->text();
            activeOperator = operatorSelector->currentData().toString();
            serialEdit->setText(activeSerial);
            operatorEdit->setText(activeOperator);
            pages->setCurrentWidget(preparationPage);
            q->updateStartAvailability();
        });
        QObject::connect(addOperator, &QPushButton::clicked, q, [this] {
            bool ok = false;
            const QString value = QInputDialog::getText(
                q, QStringLiteral("Новый оператор"), QStringLiteral("ФИО в формате «Толмачёв А.Е.»"),
                QLineEdit::Normal, QString(), &ok).trimmed();
            if (!ok || value.isEmpty()) return;
            if (!validOperatorName(value)) {
                QMessageBox::warning(q, QStringLiteral("Оператор"),
                                     QStringLiteral("Используйте формат: Фамилия И.О."));
                return;
            }
            int index = operatorSelector->findData(value);
            if (index < 0) {
                operatorSelector->addItem(value, value);
                index = operatorSelector->count() - 1;
            }
            operatorSelector->setCurrentIndex(index);
            syncTuOperators();
        });

        const auto moveRow = [this](int delta) {
            const int row = queue->currentRow();
            const int target = row + delta;
            if (row < 0 || target < 0 || target >= queue->rowCount()) return;
            QStringList values;
            for (int column = 0; column < 3; ++column)
                values << (queue->item(row, column) ? queue->item(row, column)->text() : QString());
            queue->removeRow(row);
            queue->insertRow(target);
            for (int column = 0; column < 3; ++column)
                queue->setItem(target, column, new QTableWidgetItem(values[column]));
            queue->selectRow(target);
            refreshSessionSummary();
        };
        QObject::connect(up, &QPushButton::clicked, q, [moveRow] { moveRow(-1); });
        QObject::connect(down, &QPushButton::clicked, q, [moveRow] { moveRow(1); });
        QObject::connect(remove, &QPushButton::clicked, q, [this] {
            const int row = queue->currentRow();
            if (row >= 0) queue->removeRow(row);
            if (queue->rowCount() > 0) queue->selectRow(std::min(row, queue->rowCount() - 1));
            refreshSessionSummary();
        });
        QObject::connect(clear, &QPushButton::clicked, q, [this] {
            queue->setRowCount(0);
            activeRow = -1;
            activeSerial.clear();
            serialEdit->clear();
            refreshSessionSummary();
        });
    }

    void buildPreparation()
    {
        preparationPage = new QWidget(pages);
        auto* layout = new QVBoxLayout(preparationPage);
        layout->setContentsMargins(74, 42, 74, 42);
        layout->setSpacing(16);

        auto* nav = new QHBoxLayout;
        auto* back = new QPushButton(QStringLiteral("← Сессия"), preparationPage);
        nav->addWidget(back);
        nav->addStretch();
        layout->addLayout(nav);

        layout->addWidget(heading(QStringLiteral("Подготовка"), 25, preparationPage));
        layout->addWidget(muted(
            QStringLiteral("Проверьте только оборудование, которое требуется выбранному сценарию."),
            preparationPage));

        auto* panel = makePanel(preparationPage);
        auto* panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(16, 16, 16, 16);
        panelLayout->setSpacing(10);
        panelLayout->addWidget(caption(QStringLiteral("ГОТОВНОСТЬ СТЕНДА"), panel));
        equipmentTable = new QTableWidget(0, 3, panel);
        equipmentTable->setObjectName(QStringLiteral("equipmentTable"));
        equipmentTable->setHorizontalHeaderLabels(
            {QStringLiteral("Оборудование"), QStringLiteral("Состояние"), QStringLiteral("Диагностика")});
        equipmentTable->verticalHeader()->hide();
        equipmentTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        equipmentTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        equipmentTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        equipmentTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        panelLayout->addWidget(equipmentTable, 1);
        layout->addWidget(panel, 1);

        readiness = muted(QStringLiteral("Оборудование ещё не проверено."), preparationPage);
        layout->addWidget(readiness);

        auto* actions = new QHBoxLayout;
        checkButton = new QPushButton(QStringLiteral("Проверить оборудование"), preparationPage);
        startButton = new QPushButton(QStringLiteral("Начать испытание"), preparationPage);
        startButton->setObjectName(QStringLiteral("primary"));
        startButton->setStyleSheet(QStringLiteral("background:#2e7de9;border-color:#58a5ff;font-weight:700;"));
        actions->addWidget(checkButton);
        actions->addStretch();
        actions->addWidget(startButton);
        layout->addLayout(actions);

        pages->addWidget(preparationPage);

        QObject::connect(back, &QPushButton::clicked, q,
                         [this] { pages->setCurrentWidget(sessionPage); });
        QObject::connect(checkButton, &QPushButton::clicked, q, &TestPage::equipmentCheckRequested);
        QObject::connect(startButton, &QPushButton::clicked, q, &TestPage::startSelectedTest);
    }

    void buildRuntime()
    {
        runtimePageWidget = new QWidget(pages);
        auto* layout = new QVBoxLayout(runtimePageWidget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* header = new QFrame(runtimePageWidget);
        header->setFixedHeight(52);
        header->setProperty("panel", true);
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(16, 8, 16, 8);
        runtimeBack = new QPushButton(QStringLiteral("← Сессия"), header);
        runTitle = heading(QStringLiteral("УБСИ"), 14, header);
        runSubtitle = muted(QStringLiteral("Производство"), header);
        stopButton = new QPushButton(QStringLiteral("Остановить"), header);
        stopButton->setObjectName(QStringLiteral("danger"));
        headerLayout->addWidget(runtimeBack);
        headerLayout->addWidget(runTitle);
        headerLayout->addWidget(runSubtitle);
        headerLayout->addStretch();
        headerLayout->addWidget(stopButton);
        layout->addWidget(header);

        auto* contextStrip = new QFrame(runtimePageWidget);
        contextStrip->setFixedHeight(78);
        contextStrip->setProperty("panel", true);
        auto* contextLayout = new QHBoxLayout(contextStrip);
        contextLayout->setContentsMargins(16, 10, 16, 10);
        auto* contextColumn = new QVBoxLayout;
        contextColumn->setSpacing(2);
        contextColumn->addWidget(caption(QStringLiteral("ТЕКУЩИЙ ЭТАП"), contextStrip));
        context = new QLabel(QStringLiteral("Ожидание запуска"), contextStrip);
        context->setObjectName(QStringLiteral("frozenProcedureContext"));
        QFont contextFont = context->font();
        contextFont.setPointSize(13);
        contextFont.setBold(true);
        context->setFont(contextFont);
        contextColumn->addWidget(context);
        contextLayout->addLayout(contextColumn, 1);
        progressText = muted(QStringLiteral("—"), contextStrip);
        progressText->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        contextLayout->addWidget(progressText);
        layout->addWidget(contextStrip);

        auto* body = new QHBoxLayout;
        body->setSpacing(0);

        sidebarStack = new QStackedWidget(runtimePageWidget);
        sidebarStack->setFixedWidth(270);

        productionSidebar = new QFrame(sidebarStack);
        productionSidebar->setProperty("flatPanel", true);
        auto* productionLayout = new QVBoxLayout(productionSidebar);
        productionLayout->setContentsMargins(14, 14, 14, 14);
        productionLayout->setSpacing(8);
        sideTitle = heading(QStringLiteral("ПРОЦЕДУРА"), 12, productionSidebar);
        sideTitle->setObjectName(QStringLiteral("frozenSideTitle"));
        productionLayout->addWidget(sideTitle);
        procedureSteps = new QVBoxLayout;
        procedureSteps->setSpacing(2);
        productionLayout->addLayout(procedureSteps);
        productionLayout->addStretch();
        sidebarStack->addWidget(productionSidebar);

        tuSidebar = new QFrame(sidebarStack);
        tuSidebar->setProperty("flatPanel", true);
        auto* tuLayout = new QVBoxLayout(tuSidebar);
        tuLayout->setContentsMargins(12, 12, 12, 12);
        tuLayout->setSpacing(8);
        auto* tuTitle = heading(QStringLiteral("ПРОВЕРКА ПО ТУ"), 12, tuSidebar);
        tuTitle->setObjectName(QStringLiteral("tuRuntimeTitle"));
        tuLayout->addWidget(tuTitle);
        tuSerial = muted(QStringLiteral("УБСИ"), tuSidebar);
        tuSerial->setObjectName(QStringLiteral("tuRuntimeSerial"));
        tuLayout->addWidget(tuSerial);
        tuLayout->addSpacing(4);
        tuRequirementLayout = new QVBoxLayout;
        tuRequirementLayout->setSpacing(5);
        tuLayout->addLayout(tuRequirementLayout);
        tuLayout->addStretch();
        buildTuRequirementRail();
        sidebarStack->addWidget(tuSidebar);

        body->addWidget(sidebarStack);

        stageStack = new QStackedWidget(runtimePageWidget);
        buildStagePages();
        body->addWidget(stageStack, 1);
        layout->addLayout(body, 1);

        productionTelemetryFooter = new QFrame(runtimePageWidget);
        productionTelemetryFooter->setObjectName(QStringLiteral("productionTelemetryFooter"));
        productionTelemetryFooter->setFixedHeight(168);
        productionTelemetryFooter->setProperty("panel", true);
        auto* footerLayout = new QHBoxLayout(productionTelemetryFooter);
        footerLayout->setContentsMargins(16, 10, 16, 10);
        auto* stats = new QVBoxLayout;
        stats->addWidget(caption(QStringLiteral("ВРЕМЯ"), productionTelemetryFooter));
        elapsed = heading(QStringLiteral("00:00"), 18, productionTelemetryFooter);
        stats->addWidget(elapsed);
        stats->addWidget(caption(QStringLiteral("ОБЩИЙ ТОК"), productionTelemetryFooter));
        current = heading(QStringLiteral("— А"), 18, productionTelemetryFooter);
        stats->addWidget(current);
        footerLayout->addLayout(stats);
        currentPlot = new HistoryPlot(productionTelemetryFooter);
        currentPlot->configure(QStringLiteral("Общий ток УБСИ"), QStringLiteral("А"), QStringLiteral("I"));
        footerLayout->addWidget(currentPlot, 1);
        layout->addWidget(productionTelemetryFooter);

        pages->addWidget(runtimePageWidget);

        for (int index = 0; index < 6; ++index) {
            auto* anchor = new QLabel(runtimePageWidget);
            anchor->setProperty("routeStageIndex", index);
            anchor->setProperty("includedInRoute", true);
            anchor->hide();
            routeAnchors.push_back(anchor);
        }

        QObject::connect(runtimeBack, &QPushButton::clicked, q, [this] {
            if (runInProgress) return;
            if (productionMode) {
                pages->setCurrentWidget(sessionPage);
            } else if (tuFlow) {
                tuFlow->resetToSelection();
                pages->setCurrentWidget(tuFlow);
            }
        });
        QObject::connect(stopButton, &QPushButton::clicked, q, &TestPage::stopRequested);
    }

    void buildStagePages()
    {
        // POWER
        auto* power = new QWidget(stageStack);
        auto* powerLayout = new QVBoxLayout(power);
        powerLayout->setContentsMargins(18, 14, 18, 14);
        powerLayout->setSpacing(10);
        powerLayout->addWidget(heading(QStringLiteral("Питание"), 18, power));
        auto* powerMetrics = new QHBoxLayout;
        auto metric = [power, powerMetrics](const QString& name, QLabel*& target) {
            auto* frame = makePanel(power);
            auto* layout = new QVBoxLayout(frame);
            layout->setContentsMargins(12, 8, 12, 8);
            layout->addWidget(caption(name, frame));
            target = heading(QStringLiteral("—"), 15, frame);
            layout->addWidget(target);
            powerMetrics->addWidget(frame, 1);
        };
        metric(QStringLiteral("ЗАДАНО"), powerSet);
        metric(QStringLiteral("ФАКТИЧЕСКИ"), powerActual);
        metric(QStringLiteral("ВЫДЕРЖКА / ШАГ"), powerHold);
        powerLayout->addLayout(powerMetrics);
        powerPlot = new HistoryPlot(power);
        powerPlot->configure(QStringLiteral("Напряжение питания"), QStringLiteral("В"),
                             QStringLiteral("задано"), QStringLiteral("измерено"));
        powerLayout->addWidget(powerPlot, 1);
        powerYalkStatus = muted(QStringLiteral("ЯЛК · ожидание свежего состояния"), power);
        powerYalkStatus->setObjectName(QStringLiteral("powerYalkStatus"));
        powerLayout->addWidget(powerYalkStatus);
        powerYalk = new ChannelPlane(ChannelPlane::Kind::PassivePower, power);
        powerYalk->setMinimumHeight(150);
        powerYalk->setMaximumHeight(190);
        powerLayout->addWidget(powerYalk);
        stageStack->addWidget(power);

        // YALK INITIAL
        auto* initial = new QWidget(stageStack);
        auto* initialLayout = new QVBoxLayout(initial);
        initialLayout->setContentsMargins(18, 14, 18, 14);
        initialLayout->addWidget(heading(QStringLiteral("ЯЛК-96 · обрыв / исходное состояние"), 18, initial));
        initialLayout->addWidget(muted(
            QStringLiteral("Все 80 рабочих адресов. Отображается фактическое аналоговое значение и дискретный признак."), initial));
        initialView = new InitialStateView(initial);
        initialLayout->addWidget(initialView, 1);
        stageStack->addWidget(initial);

        // YALK ANALOG
        auto* analog = new QWidget(stageStack);
        auto* analogLayout = new QVBoxLayout(analog);
        analogLayout->setContentsMargins(18, 14, 18, 14);
        analogLayout->setSpacing(8);
        analogContext = heading(QStringLiteral("ЯЛК-96 · аналоговые каналы"), 18, analog);
        analogLayout->addWidget(analogContext);
        analogLayout->addWidget(muted(
            QStringLiteral("Все 80 каналов в одной плоскости · текущий канал выделен · min/max сохраняются."), analog));
        yalkPlane = new ChannelPlane(ChannelPlane::Kind::Yalk, analog);
        analogLayout->addWidget(yalkPlane, 1);
        legacyAnalogProxy = new QWidget(analog);
        legacyAnalogProxy->setObjectName(QStringLiteral("yalkAnalogOverviewV05"));
        legacyAnalogProxy->hide();
        stageStack->addWidget(analog);

        // CONTACTS
        auto* contacts = new QWidget(stageStack);
        auto* contactsLayout = new QVBoxLayout(contacts);
        contactsLayout->setContentsMargins(18, 14, 18, 14);
        contactsContext = heading(QStringLiteral("ЯЛК-96 · контактные сигналы"), 18, contacts);
        contactsLayout->addWidget(contactsContext);
        contactsLayout->addWidget(muted(
            QStringLiteral("Аналоговый фон всех 80 адресов и дискретные результаты 0 / 0,9 / 2,5 В."), contacts));
        contactPlane = new ContactPlane(contacts);
        contactsLayout->addWidget(contactPlane, 1);
        stageStack->addWidget(contacts);

        // OVERLOAD
        auto* overload = new QWidget(stageStack);
        auto* overloadLayout = new QVBoxLayout(overload);
        overloadLayout->setContentsMargins(18, 14, 18, 14);
        overloadContext = heading(QStringLiteral("ЯЛК-96 · перегрузка ±12 В"), 18, overload);
        overloadLayout->addWidget(overloadContext);
        overloadLayout->addWidget(muted(
            QStringLiteral("88 физических каналов · baseline и current одновременно · критерий |Δcode| ≤ 2 для остальных."), overload));
        overloadPlane = new OverloadPlane(overload);
        overloadLayout->addWidget(overloadPlane, 1);
        stageStack->addWidget(overload);

        // REFERENCE
        auto* reference = new QWidget(stageStack);
        auto* referenceLayout = new QVBoxLayout(reference);
        referenceLayout->setContentsMargins(70, 42, 70, 42);
        referenceLayout->addWidget(heading(QStringLiteral("Эталон 6,2 В"), 20, reference));
        referenceText = heading(QStringLiteral("Ожидание измерения В7"), 25, reference);
        referenceText->setAlignment(Qt::AlignCenter);
        referenceLayout->addStretch();
        referenceLayout->addWidget(referenceText);
        referenceLayout->addStretch();
        stageStack->addWidget(reference);

        // YTP
        auto* ytp = new QWidget(stageStack);
        auto* ytpLayout = new QVBoxLayout(ytp);
        ytpLayout->setContentsMargins(18, 14, 18, 14);
        ytpContext = heading(QStringLiteral("ЯТП · 30 каналов"), 18, ytp);
        ytpLayout->addWidget(ytpContext);
        ytpLayout->addWidget(muted(
            QStringLiteral("Одна ручная точка Р4831 относится ко всем 30 каналам. UI не создаёт второй modal поверх backend operator.manual_input."), ytp));
        ytpPlane = new ChannelPlane(ChannelPlane::Kind::Ytp, ytp);
        ytpLayout->addWidget(ytpPlane, 1);
        auto* ytpPoints = new QLabel(QStringLiteral("0 Ом      ·      120 Ом      ·      240 Ом"), ytp);
        ytpPoints->setAlignment(Qt::AlignCenter);
        ytpPoints->setStyleSheet(QStringLiteral("color:#8ea6b7;font-weight:700;"));
        ytpLayout->addWidget(ytpPoints);
        stageStack->addWidget(ytp);

        // YVP
        auto* yvp = new QWidget(stageStack);
        auto* yvpLayout = new QVBoxLayout(yvp);
        yvpLayout->setContentsMargins(18, 14, 18, 14);
        yvpLayout->setSpacing(8);
        yvpContext = heading(QStringLiteral("ЯВП-8"), 18, yvp);
        yvpLayout->addWidget(yvpContext);
        auto* yvpMetrics = new QHBoxLayout;
        yvpMetrics->setSpacing(10);
        auto yvpMetric = [yvp, yvpMetrics](const QString& name, QLabel*& target) {
            auto* frame = makePanel(yvp);
            auto* layout = new QVBoxLayout(frame);
            layout->setContentsMargins(10, 7, 10, 7);
            layout->addWidget(caption(name, frame));
            target = heading(QStringLiteral("—"), 13, frame);
            layout->addWidget(target);
            yvpMetrics->addWidget(frame, 1);
        };
        yvpMetric(QStringLiteral("КАНАЛ"), yvpChannel);
        yvpMetric(QStringLiteral("KУ ЗАДАН"), yvpGain);
        yvpMetric(QStringLiteral("KУ РАСЧЁТ"), yvpCalculated);
        yvpAcceptance = muted(QStringLiteral("приёмочные критерии оцениваются после серии"), yvp);
        yvpAcceptance->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        yvpMetrics->addWidget(yvpAcceptance, 2);
        yvpLayout->addLayout(yvpMetrics);
        yvpPlane = new YvpPlane(yvp);
        yvpLayout->addWidget(yvpPlane, 1);
        stageStack->addWidget(yvp);

        // FINISH
        auto* finish = new QWidget(stageStack);
        auto* finishLayout = new QVBoxLayout(finish);
        finishLayout->setContentsMargins(70, 32, 70, 32);
        finishLayout->setSpacing(12);
        finishVerdict = heading(QStringLiteral("ЗАВЕРШЕНО"), 28, finish);
        finishVerdict->setAlignment(Qt::AlignCenter);
        finishLayout->addWidget(finishVerdict);
        finishMeta = muted(QStringLiteral("УБСИ"), finish);
        finishMeta->setAlignment(Qt::AlignCenter);
        finishLayout->addWidget(finishMeta);

        auto* summary = makePanel(finish);
        auto* summaryGrid = new QGridLayout(summary);
        summaryGrid->setContentsMargins(18, 14, 18, 14);
        const QStringList names = {
            QStringLiteral("Питание"), QStringLiteral("ЯЛК-96"),
            QStringLiteral("ЯТП"), QStringLiteral("ЯВП-8")
        };
        for (int index = 0; index < 4; ++index) {
            summaryGrid->addWidget(new QLabel(names[index], summary), index, 0);
            finishSummary[index] = heading(QStringLiteral("—"), 13, summary);
            finishSummary[index]->setObjectName(
                index == 0 ? QStringLiteral("finishPowerSummary")
                : index == 1 ? QStringLiteral("finishYalkSummary")
                : index == 2 ? QStringLiteral("finishYtpSummary")
                             : QStringLiteral("finishYvpSummary"));
            summaryGrid->addWidget(finishSummary[index], index, 1);
        }
        finishLayout->addWidget(summary);

        finishReport = muted(QString(), finish);
        finishReport->setObjectName(QStringLiteral("finishReportPaths"));
        finishReport->hide();
        finishLayout->addWidget(finishReport);

        finishComment = new QLineEdit(finish);
        finishComment->setPlaceholderText(QStringLiteral("Комментарий испытателя"));
        finishLayout->addWidget(finishComment);

        auto* finishActions = new QHBoxLayout;
        nextProduct = new QPushButton(QStringLiteral("Следующий УБСИ"), finish);
        returnAction = new QPushButton(QStringLiteral("Вернуться в сессию"), finish);
        finishActions->addStretch();
        finishActions->addWidget(nextProduct);
        finishActions->addWidget(returnAction);
        finishLayout->addLayout(finishActions);
        finishLayout->addStretch();

        QObject::connect(returnAction, &QPushButton::clicked, q, [this] {
            if (productionMode) pages->setCurrentWidget(sessionPage);
            else if (tuFlow) {
                tuFlow->resetToSelection();
                pages->setCurrentWidget(tuFlow);
            }
        });
        QObject::connect(nextProduct, &QPushButton::clicked, q, [this] { advanceQueue(); });
        stageStack->addWidget(finish);
    }

    void buildTuRequirementRail()
    {
        // Only requirements that belong to the current automated TU route are
        // present here. 1.1.4.2/.4/.6/.12 and the non-automated input-current
        // part of 1.1.4.14 are intentionally not rendered as pending checks.
        addTuRequirement(QStringLiteral("1.1.4.13"), QStringLiteral("Готовность после включения ≤ 30 с"));
        addTuRequirement(QStringLiteral("1.1.4.3"), QStringLiteral("Работа при 24 / 27 / 35 В и 19 / 37 В"));
        addTuRequirement(QStringLiteral("1.1.4.5"), QStringLiteral("Общий ток УБСИ ≤ 0,4 А"));
        addTuRequirement(QStringLiteral("1.1.4.10"), QStringLiteral("Обрыв входов ЯЛК"));
        addTuRequirement(QStringLiteral("1.1.4.11"), QStringLiteral("Устойчивость ЯЛК при ±12 В"));
        addTuRequirement(QStringLiteral("1.1.4.9"), QStringLiteral("Опорные 6,20 ± 0,03 В"));
        addTuRequirement(QStringLiteral("1.1.4.1"), QStringLiteral("Функциональные тракты ЯЛК / ЯТП / ЯВП"));
        addTuRequirement(QStringLiteral("1.1.4.14"), QStringLiteral("Автоматизируемые погрешности ЯЛК / ЯТП / ЯВП"));
        addTuRequirement(QStringLiteral("1.1.4.7"), QStringLiteral("АЧХ ЯВП"));
        addTuRequirement(QStringLiteral("1.1.4.8"), QStringLiteral("Коэффициент усиления ЯВП"));
    }

    void addTuRequirement(const QString& id, const QString& title)
    {
        auto row = TuRequirementRow{};
        row.id = id;
        row.title = title;
        row.frame = new QFrame(tuSidebar);
        row.frame->setProperty("tuRow", true);
        row.frame->setMinimumHeight(48);
        auto* layout = new QVBoxLayout(row.frame);
        layout->setContentsMargins(9, 6, 9, 6);
        layout->setSpacing(1);
        auto* top = new QHBoxLayout;
        auto* idLabel = new QLabel(id, row.frame);
        idLabel->setProperty("tuId", true);
        row.status = new QLabel(QStringLiteral("ОЖИДАЕТ"), row.frame);
        row.status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row.status->setStyleSheet(QStringLiteral("color:#61788a;font-size:10px;font-weight:700;"));
        top->addWidget(idLabel);
        top->addStretch();
        top->addWidget(row.status);
        layout->addLayout(top);
        auto* titleLabel = new QLabel(title, row.frame);
        titleLabel->setProperty("tuTitle", true);
        titleLabel->setWordWrap(true);
        layout->addWidget(titleLabel);
        row.frame->setObjectName(QStringLiteral("tuRequirement_%1").arg(id));
        row.status->setObjectName(QStringLiteral("tuRequirementStatus_%1").arg(id));
        const int index = tuRows.size();
        tuIndex[id] = index;
        tuRows.push_back(row);
        tuRequirementLayout->addWidget(row.frame);
    }

    void buildTu()
    {
        tuFlow = new TuFlowWidget(pages);
        pages->addWidget(tuFlow);

        QObject::connect(tuFlow, &TuFlowWidget::homeRequested, q, &TestPage::homeRequested);

        auto beginCheck = [this](const QString& serial, const QString& operatorName) {
            activeSerial = serial;
            activeOperator = operatorName;
            serialEdit->setText(serial);
            operatorEdit->setText(operatorName);
            tuFlow->beginStandCheck(serial, operatorName, q->currentRequiredEquipment());
            emit q->equipmentCheckRequested();
        };

        QObject::connect(tuFlow, &TuFlowWidget::readinessRequested, q, beginCheck);
        QObject::connect(tuFlow, &TuFlowWidget::retryRequested, q, beginCheck);
        QObject::connect(tuFlow, &TuFlowWidget::startRequested, q,
                         [this](const QString& serial, const QString& operatorName) {
                             activeSerial = serial;
                             activeOperator = operatorName;
                             serialEdit->setText(serial);
                             operatorEdit->setText(operatorName);
                             resetRuntime();
                             q->startSelectedTest();
                         });
    }

    void addEquipment(const QString& code, const QString& name,
                      const QString& connection, const QString& detail, bool confirmation)
    {
        if (equipmentRows.contains(code)) return;
        const int row = equipmentTable->rowCount();
        equipmentTable->insertRow(row);
        equipmentTable->setItem(row, 0, new QTableWidgetItem(name));
        auto* state = new QTableWidgetItem(QStringLiteral("НЕ ПРОВЕРЕНО"));
        equipmentTable->setItem(row, 1, state);
        equipmentTable->setItem(row, 2, new QTableWidgetItem(detail));
        equipmentRows.insert(code, {row, false, confirmation, connection});
    }

    void refreshRegistry()
    {
        const QString filter = registrySearch ? registrySearch->text().trimmed() : QString();
        registry->setRowCount(0);
        for (const auto& serial : registeredSerials) {
            if (!filter.isEmpty() && !serial.contains(filter, Qt::CaseInsensitive)) continue;
            const int row = registry->rowCount();
            registry->insertRow(row);
            registry->setItem(row, 0, new QTableWidgetItem(QStringLiteral("УБСИ %1").arg(serial)));
            auto* add = new QPushButton(QStringLiteral("Добавить →"), registry);
            add->setObjectName(QStringLiteral("registryAddButton"));
            QObject::connect(add, &QPushButton::clicked, q, [this, serial] { addQueueSerial(serial); });
            registry->setCellWidget(row, 1, add);
        }
    }

    void addQueueSerial(const QString& serial)
    {
        const QString normalized = serial.trimmed();
        if (normalized.isEmpty()) return;
        for (int row = 0; row < queue->rowCount(); ++row) {
            if (queue->item(row, 0) && queue->item(row, 0)->text() == normalized) {
                queue->selectRow(row);
                refreshSessionSummary();
                return;
            }
        }
        const int row = queue->rowCount();
        queue->insertRow(row);
        queue->setItem(row, 0, new QTableWidgetItem(normalized));
        queue->setItem(row, 1, new QTableWidgetItem(scopeDisplay(scopeCombo->currentData().toString())));
        auto* state = new QTableWidgetItem(QStringLiteral("ОЖИДАЕТ"));
        state->setForeground(palette::amber);
        queue->setItem(row, 2, state);
        queue->selectRow(row);
        serialEdit->setText(normalized);
        refreshSessionSummary();
    }

    void refreshSessionSummary()
    {
        const QString operatorName = operatorSelector->currentData().toString().trimmed();
        const QString operatorText = operatorName.isEmpty() ? QStringLiteral("оператор не выбран") : operatorName;
        sessionSummary->setText(QStringLiteral("%1 изделий · %2 · %3 · %4")
            .arg(queue->rowCount())
            .arg(operatorText, stage->currentText(), scopeDisplay(scopeCombo->currentData().toString())));
        enterPreparation->setEnabled(queue->rowCount() > 0 && !operatorName.isEmpty());
    }

    void syncTuOperators()
    {
        if (!tuFlow) return;
        QStringList operators;
        for (int index = 1; index < operatorSelector->count(); ++index) {
            const QString value = operatorSelector->itemData(index).toString().trimmed();
            if (!value.isEmpty()) operators << value;
        }
        tuFlow->setOperators(operators);
    }

    void selectScope(int id)
    {
        if (id < 0 || id >= scopeButtons.size()) return;
        const QString scope = scopeButtons[id]->property("scope").toString();
        int index = scopeCombo->findData(scope);
        if (index < 0) {
            scopeCombo->addItem(scopeDisplay(scope), scope);
            index = scopeCombo->count() - 1;
        }
        scopeCombo->setCurrentIndex(index);
        q->rebuildTests();
        for (int row = 0; row < queue->rowCount(); ++row)
            if (queue->item(row, 1)) queue->item(row, 1)->setText(scopeDisplay(scope));
        configureRoute();
        refreshSessionSummary();
    }

    void configureRoute()
    {
        const QString scope = scopeCombo->currentData().toString();
        for (auto* anchor : routeAnchors) anchor->setProperty("includedInRoute", false);
        auto include = [this](int index) { routeAnchors[index]->setProperty("includedInRoute", true); };
        include(0); // preparation
        include(1); // power
        if (scope == QStringLiteral("ЯЛК-96")) include(2);
        else if (scope == QStringLiteral("ЯТП")) include(3);
        else if (scope == QStringLiteral("ЯВП-8")) include(4);
        else { include(2); include(3); include(4); }
        include(5); // finish
    }

    void resetRuntime()
    {
        adapter.reset();
        contactMeasurements = 0;
        yvpCompleted = 0;
        currentPlot->clear();
        powerPlot->clear();
        elapsed->setText(QStringLiteral("00:00"));
        current->setText(QStringLiteral("— А"));
        initialView->setFrame(adapter.initial);
        yalkPlane->setYalkFrame(adapter.yalkAnalog);
        legacyAnalogProxy->setProperty("renderedChannelCount", 0);
        legacyAnalogProxy->setProperty("warningChannelCount", 0);
        contactPlane->setFrame(adapter.yalkContact, 0);
        overloadPlane->setFrame(adapter.yalkOverload);
        ytpPlane->setYtpFrame(adapter.ytp);
        yvpPlane->setFrame(adapter.yvp, 0);
        context->setText(QStringLiteral("Ожидание запуска"));
        progressText->setText(QStringLiteral("—"));
        resetTuRail();
    }

    void resetTuRail()
    {
        for (int index = 0; index < tuRows.size(); ++index) {
            auto& row = tuRows[index];
            row.state = VerificationState::Pending;
            row.active = false;
            refreshTuRow(index);
        }
    }

    void refreshTuRow(int index)
    {
        if (index < 0 || index >= tuRows.size()) return;
        auto& row = tuRows[index];
        row.frame->setProperty("tuRowActive", row.active);
        row.frame->setProperty("tuRow", !row.active);
        row.frame->style()->unpolish(row.frame);
        row.frame->style()->polish(row.frame);

        QString text;
        QColor color = palette::dim;
        if (row.active) {
            text = QStringLiteral("ВЫПОЛНЯЕТСЯ");
            color = palette::blue2;
        } else if (row.state == VerificationState::Pending) {
            text = QStringLiteral("ОЖИДАЕТ");
        } else {
            text = verificationText(row.state);
            color = stateColor(row.state);
        }
        row.status->setText(text);
        row.status->setStyleSheet(QStringLiteral("color:%1;font-size:10px;font-weight:700;")
                                      .arg(color.name()));
    }

    void clearActiveTuRows()
    {
        for (int index = 0; index < tuRows.size(); ++index) {
            if (!tuRows[index].active) continue;
            tuRows[index].active = false;
            refreshTuRow(index);
        }
    }

    QStringList requirementsForNode(const QString& node, const QString& stageName = {}) const
    {
        if (node == QStringLiteral("readiness") || node.contains(QStringLiteral("readiness")))
            return {QStringLiteral("1.1.4.13")};
        if (node == QStringLiteral("supply_range") || node.startsWith(QStringLiteral("supply_")))
            return {QStringLiteral("1.1.4.3"), QStringLiteral("1.1.4.5")};
        if (node.contains(QStringLiteral("yalk_initial")))
            return {QStringLiteral("1.1.4.10")};
        if (node.contains(QStringLiteral("yalk_overload")))
            return {QStringLiteral("1.1.4.11")};
        if (node.contains(QStringLiteral("yalk_reference")) || node.contains(QStringLiteral("reference_voltage")))
            return {QStringLiteral("1.1.4.9")};
        if (node.startsWith(QStringLiteral("yalk_")))
            return {QStringLiteral("1.1.4.1"), QStringLiteral("1.1.4.14")};
        if (node.startsWith(QStringLiteral("ytp_")))
            return {QStringLiteral("1.1.4.1"), QStringLiteral("1.1.4.14")};
        if (node.startsWith(QStringLiteral("yvp_")) || stageName == QStringLiteral("YVP_V7_POINT"))
            return {QStringLiteral("1.1.4.1"), QStringLiteral("1.1.4.7"),
                    QStringLiteral("1.1.4.8"), QStringLiteral("1.1.4.14")};
        return {};
    }

    void updateTuForEvent(const orbita::stand::RunEvent& event)
    {
        if (productionMode) return;
        clearActiveTuRows();
        const QString node = QString::fromStdString(event.nodeId);
        const QString stageName = QString::fromStdString(event.stage);
        const auto requirements = requirementsForNode(node, stageName);
        for (const auto& id : requirements) {
            const auto found = tuIndex.constFind(id);
            if (found == tuIndex.cend()) continue;
            auto& row = tuRows[*found];
            if (event.verdict == orbita::stand::RunVerdict::Fail
                || event.verdict == orbita::stand::RunVerdict::Error
                || event.verdict == orbita::stand::RunVerdict::Aborted) {
                row.state = worseState(row.state, verificationFromVerdict(event.verdict));
            }
            row.active = true;
            refreshTuRow(*found);
        }
    }

    void updateTuFromResult(const orbita::stand::ScenarioRunResult& result)
    {
        if (productionMode) return;
        resetTuRail();
        QHash<QString, VerificationState> aggregate;

        std::function<void(const orbita::stand::StepRunResult&)> collect;
        collect = [&](const orbita::stand::StepRunResult& step) {
            if (!step.children.empty()) {
                for (const auto& child : step.children) collect(child);
                return;
            }

            QStringList requirements;
            const QString tu = QString::fromStdString(step.tuRequirement);
            static const QRegularExpression requirementRx(QStringLiteral("1\\.1\\.4\\.\\d+"));
            auto match = requirementRx.globalMatch(tu);
            while (match.hasNext()) requirements << match.next().captured(0);
            if (requirements.isEmpty())
                requirements = requirementsForNode(QString::fromStdString(step.nodeId));

            const auto state = verificationFromVerdict(step.verdict);
            for (const auto& id : requirements) {
                if (!tuIndex.contains(id)) continue;
                aggregate[id] = aggregate.contains(id) ? worseState(aggregate[id], state) : state;
            }
        };
        for (const auto& step : result.steps) collect(step);

        for (auto it = aggregate.cbegin(); it != aggregate.cend(); ++it) {
            const int index = tuIndex.value(it.key(), -1);
            if (index < 0) continue;
            tuRows[index].state = it.value();
            tuRows[index].active = false;
            refreshTuRow(index);
        }
    }

    void showProcedure(Procedure procedure, const QString& node = {})
    {
        adapter.run.currentProcedure = procedure;
        stageStack->setCurrentIndex(runtimePage(procedure));
        const QString title = sectionText(procedure);
        context->setText(title);

        if (!productionMode) {
            sidebarStack->setCurrentWidget(tuSidebar);
            tuSerial->setText(QStringLiteral("УБСИ %1 · %2").arg(activeSerial, activeOperator));
            return;
        }

        sidebarStack->setCurrentWidget(productionSidebar);
        sideTitle->setText(
            procedure == Procedure::Ytp ? QStringLiteral("ЯТП")
            : procedure == Procedure::Yvp ? QStringLiteral("ЯВП-8")
            : procedure == Procedure::Power ? QStringLiteral("ПИТАНИЕ")
                                            : QStringLiteral("ЯЛК-96"));

        while (QLayoutItem* item = procedureSteps->takeAt(0)) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }

        QStringList steps;
        if (sideTitle->text() == QStringLiteral("ЯЛК-96")) {
            steps = {
                QStringLiteral("Инициализация потока"),
                QStringLiteral("Калибровка 97 / 99"),
                QStringLiteral("Обрыв / исходное состояние"),
                QStringLiteral("Аналоговые каналы"),
                QStringLiteral("Контактные сигналы"),
                QStringLiteral("Перегрузка ±12 В"),
                QStringLiteral("Эталон 6,2 В"),
                QStringLiteral("Безопасное завершение")
            };
        } else if (procedure == Procedure::Ytp) {
            steps = {QStringLiteral("Инициализация"), QStringLiteral("Калибровка"),
                     QStringLiteral("30 каналов"), QStringLiteral("Безопасное завершение")};
        } else if (procedure == Procedure::Yvp) {
            steps = {QStringLiteral("8 каналов · 7 Kу · 7 частот"),
                     QStringLiteral("Kу @ 500 Гц"), QStringLiteral("АЧХ"),
                     QStringLiteral("Затухание 4000 Гц"), QStringLiteral("Безопасное завершение")};
        } else {
            steps = {title};
        }

        const QString active = activeStepForNode(node, procedure);
        for (const auto& step : steps) {
            auto* label = new QLabel((step == active ? QStringLiteral("●  ") : QStringLiteral("○  ")) + step,
                                     productionSidebar);
            label->setWordWrap(true);
            label->setStyleSheet(step == active
                ? QStringLiteral("color:#58a5ff;font-weight:700;padding:5px 2px;")
                : QStringLiteral("color:#8ea6b7;padding:5px 2px;"));
            procedureSteps->addWidget(label);
        }
    }

    void renderModel(const QString& node = {})
    {
        showProcedure(adapter.run.currentProcedure, node);
        progressText->setText(adapter.run.progressText.isEmpty()
            ? adapter.run.procedureContext : adapter.run.progressText);

        if (adapter.telemetry.fresh && std::isfinite(adapter.telemetry.totalCurrentA)) {
            current->setText(QStringLiteral("%1 А").arg(adapter.telemetry.totalCurrentA, 0, 'f', 3));
            currentPlot->setSeries(adapter.telemetry.samples);
        }

        powerSet->setText(std::isfinite(adapter.power.setpointV)
            ? QStringLiteral("%1 В").arg(adapter.power.setpointV, 0, 'f', 2)
            : QStringLiteral("— В"));
        powerActual->setText(std::isfinite(adapter.power.actualV)
            ? QStringLiteral("%1 В").arg(adapter.power.actualV, 0, 'f', 3)
            : QStringLiteral("— В"));
        powerHold->setText(adapter.power.holdDurationMs > 0
            ? QStringLiteral("%1 / %2 с")
                .arg(adapter.power.holdElapsedMs / 1000)
                .arg(adapter.power.holdDurationMs / 1000)
            : QStringLiteral("рабочая точка"));
        powerPlot->setSeries(adapter.power.setpointHistory, adapter.power.actualHistory);
        powerYalk->setPassiveValues(adapter.power.passiveYalk, adapter.power.passiveYalkFresh);

        initialView->setFrame(adapter.initial);
        yalkPlane->setYalkFrame(adapter.yalkAnalog);
        legacyAnalogProxy->setProperty("renderedChannelCount", yalkPlane->property("renderedChannelCount"));
        legacyAnalogProxy->setProperty("warningChannelCount", yalkPlane->property("warningChannelCount"));
        analogContext->setText(QStringLiteral("ЯЛК-96 · точка %1 В · В7 %2 В · канал %3")
            .arg(adapter.yalkAnalog.pointV, 0, 'f', 2)
            .arg(adapter.yalkAnalog.actualReferenceV7, 0, 'f', 3)
            .arg(adapter.yalkAnalog.stimulatedChannel));

        contactPlane->setFrame(adapter.yalkContact, contactMeasurements);
        contactsContext->setText(QStringLiteral("ЯЛК-96 · %1 В · ожидаемая логика %2")
            .arg(adapter.yalkContact.pointV, 0, 'f', 1)
            .arg(adapter.yalkContact.expectedLogic));

        overloadPlane->setFrame(adapter.yalkOverload);
        overloadContext->setText(QStringLiteral("ЯЛК-96 · %1 · канал %2 · воздействие %3 / %4")
            .arg(adapter.yalkOverload.polarity)
            .arg(adapter.yalkOverload.stressedChannel)
            .arg(adapter.yalkOverload.impactIndex)
            .arg(adapter.yalkOverload.impactCount));

        ytpPlane->setYtpFrame(adapter.ytp);
        ytpContext->setText(QStringLiteral("ЯТП · точка %1 Ом · канал %2 / 30 · Р4831 %3 Ом")
            .arg(adapter.ytp.resistancePointOhm, 0, 'f', 0)
            .arg(adapter.ytp.testedChannel)
            .arg(adapter.ytp.actualReferenceOhm, 0, 'f', 3));

        yvpPlane->setFrame(adapter.yvp, yvpCompleted);
        yvpChannel->setText(QStringLiteral("%1 / 8").arg(adapter.yvp.testedChannel));
        yvpGain->setText(QStringLiteral("%1 мВ/пКл").arg(adapter.yvp.gain, 0, 'g', 4));
        double calculated = std::numeric_limits<double>::quiet_NaN();
        for (const auto& channel : adapter.yvp.channels) {
            if (channel.channel == adapter.yvp.testedChannel) {
                calculated = channel.calculatedGain;
                break;
            }
        }
        yvpCalculated->setText(std::isfinite(calculated)
            ? QStringLiteral("%1 мВ/пКл").arg(calculated, 0, 'g', 4)
            : QStringLiteral("— мВ/пКл"));
        yvpAcceptance->setText(adapter.yvp.acceptanceApplied
            ? QStringLiteral("приёмка: Kу @ 500 Гц · АЧХ · 4000 Гц")
            : QStringLiteral("критерий приёмки не применён"));
        yvpContext->setText(QStringLiteral("ЯВП-8 · канал %1 / 8 · Kу %2 · %3 Гц · точка %4 / %5")
            .arg(adapter.yvp.testedChannel)
            .arg(adapter.yvp.gain, 0, 'g', 4)
            .arg(adapter.yvp.frequencyHz, 0, 'g', 6)
            .arg(yvpCompleted)
            .arg(adapter.yvp.pointCount));
    }

    void advanceQueue()
    {
        const int row = activeRow;
        if (row >= 0 && row < queue->rowCount() && queue->item(row, 2))
            queue->item(row, 2)->setText(QStringLiteral("ЗАВЕРШЕНО"));
        const int next = row + 1;
        if (next >= 0 && next < queue->rowCount()) {
            queue->selectRow(next);
            activeRow = next;
            activeSerial = queue->item(next, 0)->text();
            serialEdit->setText(activeSerial);
            pages->setCurrentWidget(preparationPage);
            q->updateStartAvailability();
        } else {
            pages->setCurrentWidget(sessionPage);
        }
    }

    TestPage* q = nullptr;
    QVBoxLayout* root = nullptr;
    QStackedWidget* pages = nullptr;
    QWidget* bridge = nullptr;
    QWidget* sessionPage = nullptr;
    QWidget* preparationPage = nullptr;
    QWidget* runtimePageWidget = nullptr;
    TuFlowWidget* tuFlow = nullptr;

    QComboBox* scopeCombo = nullptr;
    QComboBox* testCombo = nullptr;
    QComboBox* modeCombo = nullptr;
    QCheckBox* partial = nullptr;
    QCheckBox* includeYvp = nullptr;
    QCheckBox* includeOverload = nullptr;
    QCheckBox* includeSurvival = nullptr;

    QLineEdit* registrySearch = nullptr;
    QTableWidget* registry = nullptr;
    QTableWidget* queue = nullptr;
    QComboBox* operatorSelector = nullptr;
    QComboBox* stage = nullptr;
    QButtonGroup* scopeGroup = nullptr;
    QVector<QPushButton*> scopeButtons;
    QLabel* sessionSummary = nullptr;
    QPushButton* enterPreparation = nullptr;
    QLineEdit* serialEdit = nullptr;
    QLineEdit* operatorEdit = nullptr;

    QTableWidget* equipmentTable = nullptr;
    QLabel* readiness = nullptr;
    QPushButton* checkButton = nullptr;
    QPushButton* startButton = nullptr;

    QPushButton* runtimeBack = nullptr;
    QLabel* runTitle = nullptr;
    QLabel* runSubtitle = nullptr;
    QPushButton* stopButton = nullptr;
    QLabel* context = nullptr;
    QLabel* progressText = nullptr;
    QStackedWidget* sidebarStack = nullptr;
    QFrame* productionSidebar = nullptr;
    QFrame* tuSidebar = nullptr;
    QLabel* sideTitle = nullptr;
    QVBoxLayout* procedureSteps = nullptr;
    QLabel* tuSerial = nullptr;
    QVBoxLayout* tuRequirementLayout = nullptr;
    QVector<TuRequirementRow> tuRows;
    QHash<QString, int> tuIndex;
    QStackedWidget* stageStack = nullptr;
    QFrame* productionTelemetryFooter = nullptr;
    QLabel* elapsed = nullptr;
    QLabel* current = nullptr;
    HistoryPlot* currentPlot = nullptr;

    QLabel* powerSet = nullptr;
    QLabel* powerActual = nullptr;
    QLabel* powerHold = nullptr;
    HistoryPlot* powerPlot = nullptr;
    QLabel* powerYalkStatus = nullptr;
    ChannelPlane* powerYalk = nullptr;
    InitialStateView* initialView = nullptr;
    ChannelPlane* yalkPlane = nullptr;
    QWidget* legacyAnalogProxy = nullptr;
    QLabel* analogContext = nullptr;
    ContactPlane* contactPlane = nullptr;
    QLabel* contactsContext = nullptr;
    OverloadPlane* overloadPlane = nullptr;
    QLabel* overloadContext = nullptr;
    QLabel* referenceText = nullptr;
    ChannelPlane* ytpPlane = nullptr;
    QLabel* ytpContext = nullptr;
    YvpPlane* yvpPlane = nullptr;
    QLabel* yvpContext = nullptr;
    QLabel* yvpChannel = nullptr;
    QLabel* yvpGain = nullptr;
    QLabel* yvpCalculated = nullptr;
    QLabel* yvpAcceptance = nullptr;
    QLabel* finishVerdict = nullptr;
    QLabel* finishMeta = nullptr;
    std::array<QLabel*, 4> finishSummary{};
    QLabel* finishReport = nullptr;
    QLineEdit* finishComment = nullptr;
    QPushButton* nextProduct = nullptr;
    QPushButton* returnAction = nullptr;

    QVector<QLabel*> routeAnchors;
    QHash<QString, ScenarioInfo> scenarios;
    QHash<QString, EquipmentRow> equipmentRows;
    QStringList registeredSerials;
    QString activeSerial;
    QString activeOperator;
    int activeRow = -1;
    bool productionMode = false;
    bool engineerMode = false;
    bool runInProgress = false;
    EquipmentInvoke equipmentInvoke;
    UiAdapter adapter;
    int contactMeasurements = 0;
    int yvpCompleted = 0;
    QElapsedTimer clock;
    QTimer* timer = nullptr;
};

TestPage::TestPage(QWidget* parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(this))
{
    rebuildScopes();
    connect(impl_->scopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { rebuildTests(); updateSelectionSummary(); });
}

TestPage::~TestPage() = default;

bool TestPage::eventFilter(QObject* watched, QEvent* event)
{
    return QWidget::eventFilter(watched, event);
}

void TestPage::setEquipmentInvoker(EquipmentInvoke invoke)
{
    impl_->equipmentInvoke = std::move(invoke);
}

void TestPage::registerEquipmentRow(const QString& code, const QString& name,
                                    const QString& connection, const QString& initialDetail,
                                    bool operatorConfirmation)
{
    impl_->addEquipment(code, name, connection, initialDetail, operatorConfirmation);
}

void TestPage::setEquipmentStatus(const QString& code, bool ready, const QString& detail)
{
    if (!impl_->productionMode && impl_->tuFlow)
        impl_->tuFlow->setEquipmentStatus(code, ready, detail);

    auto it = impl_->equipmentRows.find(code);
    if (it == impl_->equipmentRows.end()) return;
    if (!it->confirmation) it->ready = ready;
    if (auto* state = impl_->equipmentTable->item(it->row, 1)) {
        state->setText(ready ? QStringLiteral("ГОТОВО") : QStringLiteral("ОШИБКА"));
        state->setForeground(ready ? palette::green : palette::red);
    }
    if (auto* diagnostic = impl_->equipmentTable->item(it->row, 2)) diagnostic->setText(detail);
    updateStartAvailability();
}

void TestPage::setEquipmentConnection(const QString& code, const QString& connection)
{
    auto it = impl_->equipmentRows.find(code);
    if (it != impl_->equipmentRows.end()) it->connection = connection;
}

void TestPage::setEquipmentMissingPlugin(const QString& code, const QString& detail)
{
    setEquipmentStatus(code, false, detail);
}

void TestPage::setEquipmentChecking(const QString& code, const QString& detail)
{
    if (!impl_->productionMode && impl_->tuFlow) impl_->tuFlow->setEquipmentChecking(code);
    auto it = impl_->equipmentRows.find(code);
    if (it == impl_->equipmentRows.end()) return;
    it->ready = false;
    if (auto* state = impl_->equipmentTable->item(it->row, 1)) {
        state->setText(QStringLiteral("ПРОВЕРКА"));
        state->setForeground(palette::amber);
    }
    if (auto* diagnostic = impl_->equipmentTable->item(it->row, 2)) diagnostic->setText(detail);
    updateStartAvailability();
}

void TestPage::setScenarioInfo(const QString& code, bool available, bool diagnostic,
                               const QStringList& requiredEquipment, const QString& detail)
{
    impl_->scenarios[code] = {available, diagnostic, requiredEquipment, detail};
    if (!impl_->productionMode && impl_->tuFlow && code == QStringLiteral("ULK_COMBINED_CHECK"))
        impl_->tuFlow->setScenarioAvailable(available, detail);
    updateSelectionSummary();
}

void TestPage::setEngineerMode(bool enabled)
{
    impl_->engineerMode = enabled;
}

bool TestPage::isEngineerMode() const
{
    return impl_->engineerMode;
}

void TestPage::setProductionMode(bool enabled)
{
    impl_->productionMode = enabled;
    if (impl_->productionTelemetryFooter)
        impl_->productionTelemetryFooter->setVisible(enabled);
    rebuildScopes();
    if (enabled) {
        impl_->pages->setCurrentWidget(impl_->sessionPage);
    } else {
        impl_->pages->setCurrentWidget(impl_->tuFlow);
        impl_->tuFlow->setRegisteredSerials(impl_->registeredSerials);
        impl_->syncTuOperators();
    }
    updateSelectionSummary();
}

void TestPage::setAvailableProductionProducts(const QStringList& serials)
{
    impl_->registeredSerials = serials;
    impl_->registeredSerials.removeDuplicates();
    impl_->registeredSerials.sort(Qt::CaseInsensitive);
    setProperty("productionRegisteredSerials", impl_->registeredSerials);
    impl_->refreshRegistry();
    if (impl_->tuFlow) impl_->tuFlow->setRegisteredSerials(impl_->registeredSerials);
}

QStringList TestPage::currentRequiredEquipment() const
{
    return impl_->scenarios.value(currentScenarioCode()).required;
}

QString TestPage::currentScenarioCode() const
{
    return impl_->testCombo->currentData().toString();
}

bool TestPage::includeYvp() const
{
    return impl_->includeYvp->isChecked();
}

bool TestPage::includeProductionOverload() const
{
    return impl_->includeOverload->isChecked();
}

bool TestPage::includeProductionSurvival() const
{
    return impl_->includeSurvival->isChecked();
}

void TestPage::rebuildScopes()
{
    const QString previous = impl_->scopeCombo->currentData().toString();
    impl_->scopeCombo->blockSignals(true);
    impl_->scopeCombo->clear();
    if (impl_->productionMode) {
        impl_->scopeCombo->addItem(QStringLiteral("Полная УБСИ"), QStringLiteral("УБСИ ПО ТУ"));
        impl_->scopeCombo->addItem(QStringLiteral("ЯЛК-96"), QStringLiteral("ЯЛК-96"));
        impl_->scopeCombo->addItem(QStringLiteral("ЯТП"), QStringLiteral("ЯТП"));
        impl_->scopeCombo->addItem(QStringLiteral("ЯВП-8"), QStringLiteral("ЯВП-8"));
    } else {
        impl_->scopeCombo->addItem(QStringLiteral("УБСИ ПО ТУ"), QStringLiteral("УБСИ ПО ТУ"));
    }
    const int index = impl_->scopeCombo->findData(previous);
    impl_->scopeCombo->setCurrentIndex(index >= 0 ? index : 0);
    impl_->scopeCombo->blockSignals(false);

    if (impl_->productionMode) {
        const int id = std::max(0, impl_->scopeCombo->currentIndex());
        if (id < impl_->scopeButtons.size()) impl_->scopeButtons[id]->setChecked(true);
    }
    rebuildTests();
    impl_->configureRoute();
}

void TestPage::rebuildTests()
{
    const QString scope = impl_->scopeCombo->currentData().toString();
    impl_->testCombo->blockSignals(true);
    impl_->testCombo->clear();
    if (impl_->productionMode) {
        impl_->testCombo->addItem(scenarioDisplay(scope), scenarioForScope(scope));
    } else {
        impl_->testCombo->addItem(QStringLiteral("Полная автоматизированная проверка УБСИ по ТУ"),
                                  QStringLiteral("ULK_COMBINED_CHECK"));
    }
    impl_->testCombo->setCurrentIndex(0);
    impl_->testCombo->blockSignals(false);
    updateSelectionSummary();
}

void TestPage::updateSelectionSummary()
{
    const auto info = impl_->scenarios.value(currentScenarioCode());
    for (auto it = impl_->equipmentRows.begin(); it != impl_->equipmentRows.end(); ++it) {
        const bool visible = info.required.contains(it.key());
        impl_->equipmentTable->setRowHidden(it->row, !visible);
    }
    impl_->refreshSessionSummary();
    updateStartAvailability();
}

void TestPage::updateStartAvailability()
{
    const auto info = impl_->scenarios.value(currentScenarioCode());
    bool ready = info.available;
    for (const auto& code : info.required) {
        if (code == QStringLiteral("R4831") || code == QStringLiteral("SCHEME")) continue;
        auto it = impl_->equipmentRows.constFind(code);
        if (it == impl_->equipmentRows.cend() || !it->ready) {
            ready = false;
            break;
        }
    }

    impl_->checkButton->setEnabled(!impl_->runInProgress && info.available);
    impl_->startButton->setEnabled(!impl_->runInProgress && ready);
    impl_->stopButton->setEnabled(impl_->runInProgress);

    if (impl_->runInProgress) impl_->readiness->setText(QStringLiteral("Проверка выполняется"));
    else if (!info.available) impl_->readiness->setText(
        info.detail.isEmpty() ? QStringLiteral("Сценарий недоступен") : info.detail);
    else if (!ready) impl_->readiness->setText(QStringLiteral("Проверьте оборудование"));
    else impl_->readiness->setText(QStringLiteral("Оборудование готово. Можно запускать испытание."));
}

void TestPage::startSelectedTest()
{
    if (impl_->productionMode) {
        if (impl_->queue->rowCount() == 0) {
            QMessageBox::warning(this, QStringLiteral("УБСИ"), QStringLiteral("Очередь сессии пуста."));
            return;
        }
        int row = impl_->queue->currentRow();
        if (row < 0) row = 0;
        impl_->activeRow = row;
        impl_->activeSerial = impl_->queue->item(row, 0)->text();
        impl_->activeOperator = impl_->operatorSelector->currentData().toString();
        impl_->queue->selectRow(row);
        if (impl_->queue->item(row, 2)) impl_->queue->item(row, 2)->setText(QStringLiteral("В РАБОТЕ"));
    }

    if (impl_->activeSerial.isEmpty()) impl_->activeSerial = impl_->serialEdit->text().trimmed();
    if (impl_->activeSerial.isEmpty()) return;

    impl_->serialEdit->setText(impl_->activeSerial);
    impl_->runTitle->setText(QStringLiteral("УБСИ %1").arg(impl_->activeSerial));
    impl_->runSubtitle->setText(impl_->productionMode
        ? QStringLiteral("Производство · %1").arg(scopeDisplay(impl_->scopeCombo->currentData().toString()))
        : QStringLiteral("Проверка по ТУ · автоматизированный маршрут"));
    impl_->runtimeBack->setText(impl_->productionMode ? QStringLiteral("← Сессия") : QStringLiteral("← ТУ"));
    impl_->sidebarStack->setCurrentWidget(impl_->productionMode ? impl_->productionSidebar : impl_->tuSidebar);
    if (impl_->productionTelemetryFooter)
        impl_->productionTelemetryFooter->setVisible(impl_->productionMode);
    impl_->tuSerial->setText(QStringLiteral("УБСИ %1 · %2").arg(impl_->activeSerial, impl_->activeOperator));
    impl_->nextProduct->setVisible(impl_->productionMode);
    impl_->returnAction->setText(impl_->productionMode
        ? QStringLiteral("Вернуться в сессию") : QStringLiteral("Новая проверка по ТУ"));

    impl_->pages->setCurrentWidget(impl_->runtimePageWidget);
    impl_->resetRuntime();
    emit runRequested(currentScenarioCode(), impl_->activeSerial, false);
}

void TestPage::advanceDemo()
{
}

void TestPage::setRunInProgress(bool running, const QString& stage)
{
    impl_->runInProgress = running;
    if (running) {
        if (impl_->productionMode && impl_->activeSerial.isEmpty() && impl_->queue->rowCount() > 0) {
            impl_->activeRow = std::max(0, impl_->queue->currentRow());
            impl_->activeSerial = impl_->queue->item(impl_->activeRow, 0)->text();
        }
        impl_->pages->setCurrentWidget(impl_->runtimePageWidget);
        impl_->clock.restart();
        impl_->timer->start();
        if (!stage.isEmpty()) impl_->progressText->setText(stage);
    } else {
        impl_->timer->stop();
    }
    updateStartAvailability();
}

void TestPage::setRunEvent(const orbita::stand::RunEvent& event)
{
    if (!impl_->runInProgress) return;

    const QString node = QString::fromStdString(event.nodeId);
    const QString stageName = QString::fromStdString(event.stage);

    if (stageName == QStringLiteral("MEASUREMENT") && node.contains(QStringLiteral("yalk_contact")))
        ++impl_->contactMeasurements;
    if (stageName == QStringLiteral("YVP_V7_POINT")) ++impl_->yvpCompleted;

    impl_->adapter.apply(event);

    if (node == QStringLiteral("readiness")) {
        impl_->adapter.run.currentProcedure = Procedure::Power;
        impl_->adapter.run.procedureContext = QStringLiteral("Готовность после холодного включения");
    } else if (node == QStringLiteral("yalk_stream") || node == QStringLiteral("yalk_calibration")
               || node == QStringLiteral("yalk_cleanup")) {
        impl_->adapter.run.currentProcedure = Procedure::YalkAnalog;
    } else if (node == QStringLiteral("yalk_reference_voltage")) {
        impl_->adapter.run.currentProcedure = Procedure::YalkReference;
    }

    impl_->updateTuForEvent(event);
    impl_->renderModel(node);

    if (stageName == QStringLiteral("POWER_YALK")) {
        const bool fresh = eventValue(event, "fresh") == QStringLiteral("true");
        impl_->powerYalkStatus->setText(fresh
            ? QStringLiteral("ЯЛК · свежий снимок 80 каналов · питание %1 В")
                  .arg(eventValue(event, "setpoint_v"))
            : QStringLiteral("ЯЛК · НЕТ СВЕЖИХ ДАННЫХ · питание %1 В")
                  .arg(eventValue(event, "setpoint_v")));
        impl_->powerYalkStatus->setStyleSheet(fresh
            ? QStringLiteral("color:#35cf79;")
            : QStringLiteral("color:#e1ad46;font-weight:700;"));
    }

    if (stageName == QStringLiteral("YVP_V7_POINT")) {
        impl_->context->setText(QStringLiteral("Канал %1 / 8 · Kу %2 · %3 Гц")
            .arg(eventValue(event, "yvp_channel"), eventValue(event, "gain_mv_per_pc"),
                 eventValue(event, "set_frequency_hz")));
    }
}

void TestPage::setRunResult(const orbita::stand::ScenarioRunResult& result,
                            const QString& tuReportPath,
                            const QString& productionReportPath)
{
    impl_->adapter.applyResult(result);
    impl_->runInProgress = false;
    impl_->timer->stop();
    impl_->pages->setCurrentWidget(impl_->runtimePageWidget);
    impl_->updateTuFromResult(result);
    impl_->showProcedure(Procedure::Finish);

    impl_->finishVerdict->setText(verificationText(impl_->adapter.run.productVerdict));
    impl_->finishVerdict->setStyleSheet(QStringLiteral("color:%1;")
        .arg(stateColor(impl_->adapter.run.productVerdict).name()));
    impl_->finishMeta->setText(impl_->productionMode
        ? QStringLiteral("УБСИ %1 · %2 · %3")
              .arg(impl_->activeSerial, impl_->stage->currentText(), impl_->activeOperator)
        : QStringLiteral("УБСИ %1 · проверка по ТУ · %2")
              .arg(impl_->activeSerial, impl_->activeOperator));

    for (int index = 0; index < 4; ++index) {
        const auto& summary = impl_->adapter.summaries[index];
        impl_->finishSummary[index]->setText(summary.stepCount
            ? QStringLiteral("%1%2")
                  .arg(verificationText(summary.verdict),
                       summary.stepCount > 1
                           ? QStringLiteral(" · %1 шагов").arg(summary.stepCount)
                           : QString())
            : QStringLiteral("—"));
        impl_->finishSummary[index]->setStyleSheet(QStringLiteral("color:%1;font-weight:700;")
            .arg(stateColor(summary.verdict).name()));
    }

    QStringList reports;
    if (!tuReportPath.trimmed().isEmpty()) reports << QStringLiteral("Протокол ТУ: %1").arg(tuReportPath);
    if (!productionReportPath.trimmed().isEmpty()) reports << QStringLiteral("Отчёт: %1").arg(productionReportPath);
    if (!result.runId.empty()) reports << QStringLiteral("run_id: %1").arg(QString::fromStdString(result.runId));
    impl_->finishReport->setText(reports.join(QLatin1Char('\n')));
    impl_->finishReport->setVisible(!reports.isEmpty());

    if (!impl_->productionMode && impl_->tuFlow) {
        impl_->tuFlow->completeRun(result, tuReportPath);
        impl_->pages->setCurrentWidget(impl_->tuFlow);
    }

    if (impl_->activeRow >= 0 && impl_->activeRow < impl_->queue->rowCount()
        && impl_->queue->item(impl_->activeRow, 2)) {
        impl_->queue->item(impl_->activeRow, 2)->setText(
            verificationText(impl_->adapter.run.productVerdict));
    }

    updateStartAvailability();
}
