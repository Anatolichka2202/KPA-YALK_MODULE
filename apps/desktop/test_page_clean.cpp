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
#include <utility>

using namespace ubsi::ui;

namespace {

QString appStyle()
{
    return QStringLiteral(R"QSS(
QWidget#operatorTestPage { background:#08131d; color:#eaf4fb; font-family:"Segoe UI"; font-size:13px; }
QWidget { color:#eaf4fb; }
QFrame[panel="true"] { background:#102333; border:1px solid #264257; border-radius:8px; }
QLabel[muted="true"] { color:#8ea6b7; }
QLabel[caption="true"] { color:#8ea6b7; font-size:11px; font-weight:700; }
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
    auto* f = new QFrame(parent);
    f->setProperty("panel", true);
    return f;
}

QLabel* caption(const QString& text, QWidget* parent = nullptr)
{
    auto* l = new QLabel(text, parent);
    l->setProperty("caption", true);
    return l;
}

QLabel* muted(const QString& text, QWidget* parent = nullptr)
{
    auto* l = new QLabel(text, parent);
    l->setProperty("muted", true);
    l->setWordWrap(true);
    return l;
}

QLabel* heading(const QString& text, int pt, QWidget* parent = nullptr)
{
    auto* l = new QLabel(text, parent);
    QFont f = l->font();
    f.setPointSize(pt);
    f.setBold(true);
    l->setFont(f);
    return l;
}

bool validOperatorName(const QString& value)
{
    static const QRegularExpression rx(QStringLiteral("^[А-ЯЁ][а-яё-]+\\s[А-ЯЁ]\\.[А-ЯЁ]\\.$"));
    return rx.match(value.trimmed()).hasMatch();
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

QString sectionText(Procedure p)
{
    switch (p) {
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

int runtimePage(Procedure p)
{
    switch (p) {
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

QString formatClock(qint64 ms)
{
    const qint64 sec = std::max<qint64>(0, ms / 1000);
    return QStringLiteral("%1:%2").arg(sec / 60, 2, 10, QLatin1Char('0')).arg(sec % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

struct TestPage::Impl
{
    struct ScenarioInfo { bool available=false; bool diagnostic=false; QStringList required; QString detail; };
    struct EquipmentRow { int row=-1; bool ready=false; bool confirmation=false; QString connection; };

    explicit Impl(TestPage* owner) : q(owner)
    {
        q->setObjectName(QStringLiteral("operatorTestPage"));
        q->setStyleSheet(appStyle());
        root = new QVBoxLayout(q);
        root->setContentsMargins(0,0,0,0);
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
        auto* l = new QHBoxLayout(bridge);
        l->setContentsMargins(0,0,0,0);
        scopeCombo = new QComboBox(bridge); scopeCombo->setObjectName(QStringLiteral("testScope"));
        testCombo = new QComboBox(bridge); testCombo->setObjectName(QStringLiteral("testType"));
        modeCombo = new QComboBox(bridge); modeCombo->setObjectName(QStringLiteral("testMode"));
        modeCombo->addItem(QStringLiteral("Стенд — реальное оборудование"));
        partial = new QCheckBox(bridge); partial->setObjectName(QStringLiteral("allowPartial"));
        includeYvp = new QCheckBox(bridge); includeYvp->setObjectName(QStringLiteral("includeYvp")); includeYvp->setChecked(true);
        includeOverload = new QCheckBox(bridge); includeOverload->setChecked(true);
        includeSurvival = new QCheckBox(bridge); includeSurvival->setChecked(true);
        l->addWidget(scopeCombo); l->addWidget(testCombo); l->addWidget(modeCombo);
        l->addWidget(partial); l->addWidget(includeYvp); l->addWidget(includeOverload); l->addWidget(includeSurvival);
        root->addWidget(bridge);
    }

    void buildSession()
    {
        sessionPage = new QWidget(pages);
        auto* outer = new QVBoxLayout(sessionPage);
        outer->setContentsMargins(56,42,56,34);
        outer->setSpacing(18);
        auto* brand = caption(QStringLiteral("MILTECHSTATION / КТМА"), sessionPage);
        outer->addWidget(brand);
        auto* title = heading(QStringLiteral("Производственная сессия"), 26, sessionPage);
        outer->addWidget(title);
        outer->addWidget(muted(QStringLiteral("УБСИ · один оператор, один производственный этап и один объём проверки на всю очередь."), sessionPage));

        auto* body = new QHBoxLayout;
        body->setSpacing(18);

        auto* registryPanel = makePanel(sessionPage);
        auto* rl = new QVBoxLayout(registryPanel); rl->setContentsMargins(16,16,16,16); rl->setSpacing(10);
        rl->addWidget(caption(QStringLiteral("ЗАРЕГИСТРИРОВАННЫЕ УБСИ"), registryPanel));
        registrySearch = new QLineEdit(registryPanel); registrySearch->setObjectName(QStringLiteral("productionRegistrySearch")); registrySearch->setPlaceholderText(QStringLiteral("Поиск…"));
        rl->addWidget(registrySearch);
        registry = new QTableWidget(0,2,registryPanel); registry->setObjectName(QStringLiteral("productionRegistryTable"));
        registry->horizontalHeader()->hide(); registry->verticalHeader()->hide(); registry->setShowGrid(false); registry->setSelectionBehavior(QAbstractItemView::SelectRows); registry->setEditTriggers(QAbstractItemView::NoEditTriggers);
        registry->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch); registry->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents); registry->verticalHeader()->setDefaultSectionSize(42);
        rl->addWidget(registry,1); body->addWidget(registryPanel,123);

        auto* queuePanel = makePanel(sessionPage);
        auto* ql = new QVBoxLayout(queuePanel); ql->setContentsMargins(16,16,16,16); ql->setSpacing(10);
        ql->addWidget(caption(QStringLiteral("ОЧЕРЕДЬ СЕССИИ"), queuePanel));
        queue = new QTableWidget(0,3,queuePanel); queue->setObjectName(QStringLiteral("productionSessionTable")); queue->setHorizontalHeaderLabels({QStringLiteral("УБСИ"),QStringLiteral("Объём"),QStringLiteral("Состояние")});
        queue->verticalHeader()->hide(); queue->setSelectionBehavior(QAbstractItemView::SelectRows); queue->setEditTriggers(QAbstractItemView::NoEditTriggers); queue->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch); queue->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents); queue->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
        ql->addWidget(queue,1);
        auto* tools = new QHBoxLayout; auto* up=new QPushButton(QStringLiteral("↑"),queuePanel); auto* down=new QPushButton(QStringLiteral("↓"),queuePanel); auto* remove=new QPushButton(QStringLiteral("Убрать"),queuePanel); auto* clear=new QPushButton(QStringLiteral("Очистить"),queuePanel);
        tools->addWidget(up); tools->addWidget(down); tools->addWidget(remove); tools->addWidget(clear); tools->addStretch(); ql->addLayout(tools); body->addWidget(queuePanel,92);

        auto* cfg = makePanel(sessionPage);
        auto* cl = new QVBoxLayout(cfg); cl->setContentsMargins(16,16,16,16); cl->setSpacing(8);
        cl->addWidget(caption(QStringLiteral("ПАРАМЕТРЫ СЕССИИ"),cfg));
        cl->addWidget(caption(QStringLiteral("ОПЕРАТОР"),cfg));
        operatorSelector = new QComboBox(cfg); operatorSelector->setObjectName(QStringLiteral("productionOperatorSelector")); operatorSelector->addItem(QStringLiteral("Выберите оператора"),QString()); cl->addWidget(operatorSelector);
        auto* addOperator = new QPushButton(QStringLiteral("+ Добавить оператора"),cfg); cl->addWidget(addOperator);
        cl->addWidget(caption(QStringLiteral("ПРОИЗВОДСТВЕННЫЙ ЭТАП"),cfg));
        stage = new QComboBox(cfg); stage->setObjectName(QStringLiteral("productionStage"));
        stage->addItem(QStringLiteral("Первичная проверка"),QStringLiteral("Primary")); stage->addItem(QStringLiteral("Климатические испытания — нормальные условия"),QStringLiteral("ClimateNormal")); stage->addItem(QStringLiteral("Климатические испытания — отрицательная температура"),QStringLiteral("ClimateMinus")); stage->addItem(QStringLiteral("Климатические испытания — повышенная температура"),QStringLiteral("ClimatePlus")); stage->addItem(QStringLiteral("После заливки — нормальные условия"),QStringLiteral("PottingClimateNormal")); stage->addItem(QStringLiteral("После заливки — повышенная температура"),QStringLiteral("PottingClimatePlus")); stage->addItem(QStringLiteral("После заливки — отрицательная температура"),QStringLiteral("PottingClimateMinus")); cl->addWidget(stage);
        cl->addWidget(caption(QStringLiteral("ОБЪЁМ ПРОВЕРКИ"),cfg));
        scopeGroup = new QButtonGroup(q); scopeGroup->setExclusive(true);
        const QVector<QPair<QString,QString>> scopes={{QStringLiteral("Полная УБСИ"),QStringLiteral("УБСИ ПО ТУ")},{QStringLiteral("ЯЛК-96"),QStringLiteral("ЯЛК-96")},{QStringLiteral("ЯТП"),QStringLiteral("ЯТП")},{QStringLiteral("ЯВП-8"),QStringLiteral("ЯВП-8")}};
        for(int i=0;i<scopes.size();++i){auto* b=new QPushButton(scopes[i].first,cfg);b->setCheckable(true);b->setProperty("scope",scopes[i].second);b->setMinimumHeight(48);scopeGroup->addButton(b,i);scopeButtons.push_back(b);cl->addWidget(b);} scopeGroup->button(0)->setChecked(true);
        cl->addStretch(); body->addWidget(cfg,72);
        outer->addLayout(body,1);

        auto* action = makePanel(sessionPage); auto* al=new QHBoxLayout(action); al->setContentsMargins(16,12,16,12);
        sessionSummary = muted(QStringLiteral("0 изделий · оператор не выбран · Первичная проверка · Полная УБСИ"),action); al->addWidget(sessionSummary,1);
        enterPreparation = new QPushButton(QStringLiteral("Перейти к подготовке"),action); enterPreparation->setObjectName(QStringLiteral("enterPreparation")); enterPreparation->setProperty("primary",true); enterPreparation->setEnabled(false); al->addWidget(enterPreparation); outer->addWidget(action);

        serialEdit = new QLineEdit(sessionPage); serialEdit->setObjectName(QStringLiteral("objectSerial")); serialEdit->hide();
        operatorEdit = new QLineEdit(sessionPage); operatorEdit->setObjectName(QStringLiteral("operatorName")); operatorEdit->hide();
        pages->addWidget(sessionPage);

        QObject::connect(registrySearch,&QLineEdit::textChanged,q,[this]{refreshRegistry();});
        QObject::connect(operatorSelector,QOverload<int>::of(&QComboBox::currentIndexChanged),q,[this](int){operatorEdit->setText(operatorSelector->currentData().toString());refreshSessionSummary();});
        QObject::connect(stage,QOverload<int>::of(&QComboBox::currentIndexChanged),q,[this](int){refreshSessionSummary();});
        QObject::connect(scopeGroup,&QButtonGroup::idClicked,q,[this](int id){selectScope(id);});
        QObject::connect(addOperator,&QPushButton::clicked,q,[this]{
            bool ok=false; const QString value=QInputDialog::getText(q,QStringLiteral("Новый оператор"),QStringLiteral("Фамилия И.О."),QLineEdit::Normal,{},&ok).trimmed();
            if(!ok||value.isEmpty())return; if(!validOperatorName(value)){QMessageBox::warning(q,QStringLiteral("Оператор"),QStringLiteral("Формат: Фамилия И.О."));return;} int i=operatorSelector->findData(value);if(i<0){operatorSelector->addItem(value,value);i=operatorSelector->count()-1;}operatorSelector->setCurrentIndex(i);
        });
        QObject::connect(enterPreparation,&QPushButton::clicked,q,[this]{activeSerial=queue->currentRow()>=0&&queue->item(queue->currentRow(),0)?queue->item(queue->currentRow(),0)->text():QString(); if(activeSerial.isEmpty()&&queue->rowCount()>0)activeSerial=queue->item(0,0)->text(); serialEdit->setText(activeSerial); pages->setCurrentWidget(preparationPage); q->updateStartAvailability();});
        auto move=[this](int d){int r=queue->currentRow(),t=r+d;if(r<0||t<0||t>=queue->rowCount())return;QStringList v;for(int c=0;c<3;++c)v<<queue->item(r,c)->text();queue->removeRow(r);queue->insertRow(t);for(int c=0;c<3;++c)queue->setItem(t,c,new QTableWidgetItem(v[c]));queue->selectRow(t);};
        QObject::connect(up,&QPushButton::clicked,q,[move]{move(-1);}); QObject::connect(down,&QPushButton::clicked,q,[move]{move(1);});
        QObject::connect(remove,&QPushButton::clicked,q,[this]{int r=queue->currentRow();if(r>=0)queue->removeRow(r);refreshSessionSummary();}); QObject::connect(clear,&QPushButton::clicked,q,[this]{queue->setRowCount(0);refreshSessionSummary();});
    }

    void buildPreparation()
    {
        preparationPage = new QWidget(pages); auto* l=new QVBoxLayout(preparationPage);l->setContentsMargins(70,48,70,48);l->setSpacing(18);
        auto* back=new QPushButton(QStringLiteral("← Сессия"),preparationPage);back->setMaximumWidth(110);l->addWidget(back,0,Qt::AlignLeft);
        l->addWidget(heading(QStringLiteral("Подготовка"),26,preparationPage)); l->addWidget(muted(QStringLiteral("Проверьте оборудование, необходимое выбранному сценарию."),preparationPage));
        auto* p=makePanel(preparationPage);auto* pl=new QVBoxLayout(p);pl->setContentsMargins(18,18,18,18);
        equipmentTable=new QTableWidget(0,3,p);equipmentTable->setObjectName(QStringLiteral("equipmentTable"));equipmentTable->setHorizontalHeaderLabels({QStringLiteral("Устройство"),QStringLiteral("Состояние"),QStringLiteral("Диагностика")});equipmentTable->verticalHeader()->hide();equipmentTable->setEditTriggers(QAbstractItemView::NoEditTriggers);equipmentTable->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);equipmentTable->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);equipmentTable->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);pl->addWidget(equipmentTable);l->addWidget(p,1);
        readiness=muted(QStringLiteral("Оборудование ещё не проверено."),preparationPage);l->addWidget(readiness);
        auto* actions=new QHBoxLayout;checkButton=new QPushButton(QStringLiteral("Проверить оборудование"),preparationPage);startButton=new QPushButton(QStringLiteral("Начать испытание"),preparationPage);startButton->setObjectName(QStringLiteral("primary"));actions->addWidget(checkButton);actions->addStretch();actions->addWidget(startButton);l->addLayout(actions);pages->addWidget(preparationPage);
        QObject::connect(back,&QPushButton::clicked,q,[this]{pages->setCurrentWidget(sessionPage);}); QObject::connect(checkButton,&QPushButton::clicked,q,&TestPage::equipmentCheckRequested); QObject::connect(startButton,&QPushButton::clicked,q,&TestPage::startSelectedTest);
    }

    void buildRuntime()
    {
        runtimePageWidget=new QWidget(pages);auto* l=new QVBoxLayout(runtimePageWidget);l->setContentsMargins(0,0,0,0);l->setSpacing(0);
        auto* header=new QFrame(runtimePageWidget);header->setFixedHeight(52);header->setProperty("panel",true);auto* hl=new QHBoxLayout(header);hl->setContentsMargins(16,8,16,8);auto* back=new QPushButton(QStringLiteral("← Сессия"),header);runTitle=heading(QStringLiteral("УБСИ"),14,header);runSubtitle=muted(QStringLiteral("Производство"),header);stopButton=new QPushButton(QStringLiteral("Остановить"),header);stopButton->setObjectName(QStringLiteral("danger"));hl->addWidget(back);hl->addWidget(runTitle);hl->addWidget(runSubtitle);hl->addStretch();hl->addWidget(stopButton);l->addWidget(header);
        auto* contextStrip=new QFrame(runtimePageWidget);contextStrip->setFixedHeight(78);contextStrip->setProperty("panel",true);auto* ctl=new QHBoxLayout(contextStrip);ctl->setContentsMargins(16,10,16,10);context=new QLabel(QStringLiteral("Ожидание запуска"),contextStrip);context->setObjectName(QStringLiteral("frozenProcedureContext"));QFont cf=context->font();cf.setPointSize(13);cf.setBold(true);context->setFont(cf);progressText=muted(QStringLiteral("—"),contextStrip);ctl->addWidget(context,1);ctl->addWidget(progressText);l->addWidget(contextStrip);
        auto* body=new QHBoxLayout;body->setSpacing(0);sidebar=new QFrame(runtimePageWidget);sidebar->setFixedWidth(240);sidebar->setProperty("panel",true);auto* sl=new QVBoxLayout(sidebar);sl->setContentsMargins(14,14,14,14);sideTitle=heading(QStringLiteral("ПРОЦЕДУРА"),12,sidebar);sideTitle->setObjectName(QStringLiteral("frozenSideTitle"));sl->addWidget(sideTitle);procedureSteps=new QVBoxLayout;sl->addLayout(procedureSteps);sl->addStretch();body->addWidget(sidebar);
        stageStack=new QStackedWidget(runtimePageWidget);buildStagePages();body->addWidget(stageStack,1);l->addLayout(body,1);
        auto* footer=new QFrame(runtimePageWidget);footer->setFixedHeight(168);footer->setProperty("panel",true);auto* fl=new QHBoxLayout(footer);fl->setContentsMargins(16,10,16,10);auto* stats=new QVBoxLayout;stats->addWidget(caption(QStringLiteral("ВРЕМЯ"),footer));elapsed=heading(QStringLiteral("00:00"),18,footer);stats->addWidget(elapsed);stats->addWidget(caption(QStringLiteral("ОБЩИЙ ТОК"),footer));current=heading(QStringLiteral("— А"),18,footer);stats->addWidget(current);fl->addLayout(stats);currentPlot=new HistoryPlot(footer);currentPlot->configure(QStringLiteral("Общий ток УБСИ"),QStringLiteral("А"),QStringLiteral("I"));fl->addWidget(currentPlot,1);l->addWidget(footer);pages->addWidget(runtimePageWidget);
        for(int i=0;i<6;++i){auto* anchor=new QLabel(runtimePageWidget);anchor->setProperty("routeStageIndex",i);anchor->setProperty("includedInRoute",true);anchor->hide();routeAnchors.push_back(anchor);}        
        QObject::connect(back,&QPushButton::clicked,q,[this]{pages->setCurrentWidget(sessionPage);});QObject::connect(stopButton,&QPushButton::clicked,q,&TestPage::stopRequested);
    }

    QWidget* metricRow(const QStringList& names, const QVector<QLabel*>& values, QWidget* parent)
    {
        auto* w=new QWidget(parent);auto* l=new QHBoxLayout(w);l->setContentsMargins(0,0,0,0);l->setSpacing(12);for(int i=0;i<names.size();++i){auto* p=makePanel(w);auto* pl=new QVBoxLayout(p);pl->setContentsMargins(12,8,12,8);pl->addWidget(caption(names[i],p));if(i<values.size())pl->addWidget(values[i]);l->addWidget(p,1);}return w;
    }

    void buildStagePages()
    {
        // POWER
        auto* power=new QWidget(stageStack);auto* pl=new QVBoxLayout(power);pl->setContentsMargins(18,14,18,14);pl->setSpacing(10);pl->addWidget(heading(QStringLiteral("Питание"),20,power));
        powerSet=heading(QStringLiteral("— В"),14,power);powerActual=heading(QStringLiteral("— В"),14,power);powerHold=heading(QStringLiteral("—"),14,power);pl->addWidget(metricRow({QStringLiteral("ЗАДАНО"),QStringLiteral("ФАКТИЧЕСКИ"),QStringLiteral("ВЫДЕРЖКА")},{powerSet,powerActual,powerHold},power));powerPlot=new HistoryPlot(power);powerPlot->configure(QStringLiteral("Напряжение"),QStringLiteral("В"),QStringLiteral("задано"),QStringLiteral("измерено"));pl->addWidget(powerPlot,1);powerYalkStatus=muted(QStringLiteral("ЯЛК · ожидание свежего снимка 80 каналов"),power);powerYalkStatus->setObjectName(QStringLiteral("powerYalkStatus"));pl->addWidget(powerYalkStatus);powerYalk=new ChannelPlane(ChannelPlane::Kind::PassivePower,power);powerYalk->setMinimumHeight(140);pl->addWidget(powerYalk);stageStack->addWidget(power);
        // INITIAL
        auto* initial=new QWidget(stageStack);auto* il=new QVBoxLayout(initial);il->setContentsMargins(18,14,18,14);il->addWidget(heading(QStringLiteral("ЯЛК-96 · Обрыв / исходное состояние"),20,initial));il->addWidget(muted(QStringLiteral("Все 80 физических входов. Адреса отображаются с реальными разрывами нумерации."),initial));initialView=new InitialStateView(initial);il->addWidget(initialView,1);stageStack->addWidget(initial);
        // ANALOG
        auto* analog=new QWidget(stageStack);auto* al=new QVBoxLayout(analog);al->setContentsMargins(18,14,18,14);analogContext=heading(QStringLiteral("ЯЛК-96 · Аналоговые каналы"),18,analog);al->addWidget(analogContext);yalkPlane=new ChannelPlane(ChannelPlane::Kind::Yalk,analog);al->addWidget(yalkPlane,1);legacyAnalogProxy=new QWidget(analog);legacyAnalogProxy->setObjectName(QStringLiteral("yalkAnalogOverviewV05"));legacyAnalogProxy->hide();stageStack->addWidget(analog);
        // CONTACT
        auto* contacts=new QWidget(stageStack);auto* cl=new QVBoxLayout(contacts);cl->setContentsMargins(18,14,18,14);contactsContext=heading(QStringLiteral("ЯЛК-96 · Контактные сигналы"),18,contacts);cl->addWidget(contactsContext);contactPlane=new ContactPlane(contacts);cl->addWidget(contactPlane,1);stageStack->addWidget(contacts);
        // OVERLOAD
        auto* overload=new QWidget(stageStack);auto* ol=new QVBoxLayout(overload);ol->setContentsMargins(18,14,18,14);overloadContext=heading(QStringLiteral("ЯЛК-96 · Перегрузка ±12 В"),18,overload);ol->addWidget(overloadContext);overloadPlane=new OverloadPlane(overload);ol->addWidget(overloadPlane,1);stageStack->addWidget(overload);
        // REFERENCE
        auto* reference=new QWidget(stageStack);auto* rfl=new QVBoxLayout(reference);rfl->setContentsMargins(18,14,18,14);rfl->addWidget(heading(QStringLiteral("ЯЛК-96 · Эталон 6,2 В"),20,reference));referenceText=heading(QStringLiteral("Ожидание измерения"),18,reference);rfl->addWidget(referenceText);rfl->addStretch();stageStack->addWidget(reference);
        // YTP
        auto* ytp=new QWidget(stageStack);auto* yl=new QVBoxLayout(ytp);yl->setContentsMargins(18,14,18,14);ytpContext=heading(QStringLiteral("ЯТП"),18,ytp);yl->addWidget(ytpContext);ytpPlane=new ChannelPlane(ChannelPlane::Kind::Ytp,ytp);yl->addWidget(ytpPlane,1);stageStack->addWidget(ytp);
        // YVP
        auto* yvp=new QWidget(stageStack);auto* vl=new QVBoxLayout(yvp);vl->setContentsMargins(18,14,18,14);yvpContext=heading(QStringLiteral("ЯВП-8"),18,yvp);vl->addWidget(yvpContext);auto* metrics=new QHBoxLayout;yvpChannel=new QLabel(QStringLiteral("— / 8"),yvp);yvpGain=new QLabel(QStringLiteral("— мВ/пКл"),yvp);yvpCalculated=new QLabel(QStringLiteral("— мВ/пКл"),yvp);yvpAcceptance=new QLabel(QStringLiteral("критерий приёмки не применён"),yvp);metrics->addWidget(yvpChannel);metrics->addWidget(yvpGain);metrics->addWidget(yvpCalculated);metrics->addStretch();metrics->addWidget(yvpAcceptance);vl->addLayout(metrics);yvpPlane=new YvpPlane(yvp);vl->addWidget(yvpPlane,1);stageStack->addWidget(yvp);
        // FINISH
        auto* finish=new QWidget(stageStack);auto* fl=new QVBoxLayout(finish);fl->setContentsMargins(70,36,70,36);finishVerdict=heading(QStringLiteral("ЗАВЕРШЕНО"),28,finish);finishVerdict->setAlignment(Qt::AlignCenter);fl->addWidget(finishVerdict);finishMeta=muted(QStringLiteral("УБСИ"),finish);finishMeta->setAlignment(Qt::AlignCenter);fl->addWidget(finishMeta);auto* summary=makePanel(finish);auto* sg=new QGridLayout(summary);const QStringList names={QStringLiteral("Питание"),QStringLiteral("ЯЛК-96"),QStringLiteral("ЯТП"),QStringLiteral("ЯВП-8")};for(int i=0;i<4;++i){sg->addWidget(new QLabel(names[i],summary),i,0);finishSummary[i]=heading(QStringLiteral("—"),13,summary);finishSummary[i]->setObjectName(i==0?QStringLiteral("finishPowerSummary"):i==1?QStringLiteral("finishYalkSummary"):i==2?QStringLiteral("finishYtpSummary"):QStringLiteral("finishYvpSummary"));sg->addWidget(finishSummary[i],i,1);}fl->addWidget(summary);finishComment=new QLineEdit(finish);finishComment->setPlaceholderText(QStringLiteral("Комментарий испытателя"));fl->addWidget(finishComment);auto* actions=new QHBoxLayout;auto* next=new QPushButton(QStringLiteral("Следующий УБСИ"),finish);auto* session=new QPushButton(QStringLiteral("Вернуться в сессию"),finish);actions->addStretch();actions->addWidget(next);actions->addWidget(session);fl->addLayout(actions);fl->addStretch();QObject::connect(session,&QPushButton::clicked,q,[this]{pages->setCurrentWidget(sessionPage);});QObject::connect(next,&QPushButton::clicked,q,[this]{advanceQueue();});stageStack->addWidget(finish);
    }

    void buildTu()
    {
        tuFlow = new TuFlowWidget(pages); pages->addWidget(tuFlow); QObject::connect(tuFlow,&TuFlowWidget::homeRequested,q,&TestPage::homeRequested);
        auto begin=[this](const QString& serial,const QString& op){activeSerial=serial;activeOperator=op;serialEdit->setText(serial);tuFlow->beginStandCheck(serial,op,q->currentRequiredEquipment());emit q->equipmentCheckRequested();};
        QObject::connect(tuFlow,&TuFlowWidget::readinessRequested,q,begin);QObject::connect(tuFlow,&TuFlowWidget::retryRequested,q,begin);QObject::connect(tuFlow,&TuFlowWidget::startRequested,q,[this](const QString& serial,const QString& op){activeSerial=serial;activeOperator=op;serialEdit->setText(serial);pages->setCurrentWidget(runtimePageWidget);resetRuntime();q->startSelectedTest();});
    }

    void refreshRegistry()
    {
        const QString f=registrySearch->text().trimmed();registry->setRowCount(0);for(const auto&s:registeredSerials){if(!f.isEmpty()&&!s.contains(f,Qt::CaseInsensitive))continue;int r=registry->rowCount();registry->insertRow(r);registry->setItem(r,0,new QTableWidgetItem(s));auto* add=new QPushButton(QStringLiteral("Добавить →"),registry);registry->setCellWidget(r,1,add);QObject::connect(add,&QPushButton::clicked,q,[this,s]{addQueue(s);});}
    }
    void addQueue(const QString& serial)
    {
        for(int r=0;r<queue->rowCount();++r)if(queue->item(r,0)&&queue->item(r,0)->text()==serial){queue->selectRow(r);refreshSessionSummary();return;}int r=queue->rowCount();queue->insertRow(r);queue->setItem(r,0,new QTableWidgetItem(serial));queue->setItem(r,1,new QTableWidgetItem(scopeDisplay(scopeCombo->currentData().toString())));queue->setItem(r,2,new QTableWidgetItem(QStringLiteral("ОЖИДАЕТ")));queue->selectRow(r);serialEdit->setText(serial);refreshSessionSummary();
    }
    void refreshSessionSummary()
    {
        const QString op=operatorSelector->currentData().toString();const QString st=stage->currentText();const QString sc=scopeDisplay(scopeCombo->currentData().toString());sessionSummary->setText(QStringLiteral("%1 изделий · %2 · %3 · %4").arg(queue->rowCount()).arg(op.isEmpty()?QStringLiteral("оператор не выбран"):op,st,sc));enterPreparation->setEnabled(queue->rowCount()>0&&!op.isEmpty());
    }
    void selectScope(int id)
    {
        if(id<0||id>=scopeButtons.size())return;const QString scope=scopeButtons[id]->property("scope").toString();int idx=scopeCombo->findData(scope);if(idx<0){scopeCombo->addItem(scopeDisplay(scope),scope);idx=scopeCombo->count()-1;}scopeCombo->setCurrentIndex(idx);q->rebuildTests();for(int r=0;r<queue->rowCount();++r)if(queue->item(r,1))queue->item(r,1)->setText(scopeDisplay(scope));configureRoute();refreshSessionSummary();
    }
    void configureRoute()
    {
        const QString scope=scopeCombo->currentData().toString();for(auto*a:routeAnchors)a->setProperty("includedInRoute",false);auto on=[this](int i){routeAnchors[i]->setProperty("includedInRoute",true);};on(0);on(1);if(scope==QStringLiteral("ЯЛК-96")){on(2);}else if(scope==QStringLiteral("ЯТП")){on(3);}else if(scope==QStringLiteral("ЯВП-8")){on(4);}else{on(2);on(3);on(4);}on(5);
    }
    void addEquipment(const QString& code,const QString& name,const QString& connection,const QString& detail,bool confirmation)
    {
        if(equipmentRows.contains(code))return;int r=equipmentTable->rowCount();equipmentTable->insertRow(r);equipmentTable->setItem(r,0,new QTableWidgetItem(name));auto* state=new QTableWidgetItem(QStringLiteral("НЕ ПРОВЕРЕНО"));equipmentTable->setItem(r,1,state);equipmentTable->setItem(r,2,new QTableWidgetItem(detail));equipmentRows.insert(code,{r,false,confirmation,connection});
    }
    void resetRuntime()
    {
        adapter.reset();contactMeasurements=0;yvpCompleted=0;currentPlot->clear();powerPlot->clear();elapsed->setText(QStringLiteral("00:00"));current->setText(QStringLiteral("— А"));initialView->setFrame(adapter.initial);yalkPlane->setYalkFrame(adapter.yalkAnalog);legacyAnalogProxy->setProperty("renderedChannelCount",0);contactPlane->setFrame(adapter.yalkContact,0);overloadPlane->setFrame(adapter.yalkOverload);ytpPlane->setYtpFrame(adapter.ytp);yvpPlane->setFrame(adapter.yvp,0);context->setText(QStringLiteral("Ожидание запуска"));progressText->setText(QStringLiteral("—"));
    }
    void showProcedure(Procedure p)
    {
        adapter.run.currentProcedure=p;stageStack->setCurrentIndex(runtimePage(p));const QString title=sectionText(p);context->setText(title);sideTitle->setText((p==Procedure::Ytp)?QStringLiteral("ЯТП"):(p==Procedure::Yvp)?QStringLiteral("ЯВП-8"):(p==Procedure::Power)?QStringLiteral("ПИТАНИЕ"):QStringLiteral("ЯЛК-96"));
        while(QLayoutItem* item=procedureSteps->takeAt(0)){if(item->widget())item->widget()->deleteLater();delete item;}
        QStringList steps;if(sideTitle->text()==QStringLiteral("ЯЛК-96"))steps={QStringLiteral("Инициализация потока"),QStringLiteral("Калибровка 97 / 99"),QStringLiteral("Обрыв / исходное состояние"),QStringLiteral("Аналоговые каналы"),QStringLiteral("Контактные сигналы"),QStringLiteral("Перегрузка ±12 В"),QStringLiteral("Эталон 6,2 В"),QStringLiteral("Безопасное завершение")};else steps={title};
        for(const auto&s:steps){auto* label=new QLabel((s==title?QStringLiteral("●  "):QStringLiteral("○  "))+s,sidebar);label->setWordWrap(true);label->setStyleSheet(s==title?QStringLiteral("color:#58a5ff;font-weight:700;padding:5px 2px;"):QStringLiteral("color:#8ea6b7;padding:5px 2px;"));procedureSteps->addWidget(label);}    
    }
    void renderModel()
    {
        const auto p=adapter.run.currentProcedure;showProcedure(p);progressText->setText(adapter.run.progressText.isEmpty()?adapter.run.procedureContext:adapter.run.progressText);if(adapter.telemetry.fresh&&std::isfinite(adapter.telemetry.totalCurrentA)){current->setText(QStringLiteral("%1 А").arg(adapter.telemetry.totalCurrentA,0,'f',3));currentPlot->setSeries(adapter.telemetry.samples);}powerSet->setText(std::isfinite(adapter.power.setpointV)?QStringLiteral("%1 В").arg(adapter.power.setpointV,0,'f',2):QStringLiteral("— В"));powerActual->setText(std::isfinite(adapter.power.actualV)?QStringLiteral("%1 В").arg(adapter.power.actualV,0,'f',3):QStringLiteral("— В"));powerHold->setText(adapter.power.holdDurationMs>0?QStringLiteral("%1 / %2 с").arg(adapter.power.holdElapsedMs/1000).arg(adapter.power.holdDurationMs/1000):QStringLiteral("рабочая точка"));powerPlot->setSeries(adapter.power.setpointHistory,adapter.power.actualHistory);powerYalk->setPassiveValues(adapter.power.passiveYalk,adapter.power.passiveYalkFresh);initialView->setFrame(adapter.initial);yalkPlane->setYalkFrame(adapter.yalkAnalog);legacyAnalogProxy->setProperty("renderedChannelCount",yalkPlane->property("renderedChannelCount"));legacyAnalogProxy->setProperty("warningChannelCount",yalkPlane->property("warningChannelCount"));analogContext->setText(QStringLiteral("ЯЛК-96 · точка %1 В · В7 %2 В · канал %3").arg(adapter.yalkAnalog.pointV,0,'f',2).arg(adapter.yalkAnalog.actualReferenceV7,0,'f',3).arg(adapter.yalkAnalog.stimulatedChannel));contactPlane->setFrame(adapter.yalkContact,contactMeasurements);contactsContext->setText(QStringLiteral("ЯЛК-96 · %1 В · логика %2").arg(adapter.yalkContact.pointV,0,'f',1).arg(adapter.yalkContact.expectedLogic));overloadPlane->setFrame(adapter.yalkOverload);overloadContext->setText(QStringLiteral("ЯЛК-96 · %1 · канал %2 · воздействие %3/%4").arg(adapter.yalkOverload.polarity).arg(adapter.yalkOverload.stressedChannel).arg(adapter.yalkOverload.impactIndex).arg(adapter.yalkOverload.impactCount));ytpPlane->setYtpFrame(adapter.ytp);ytpContext->setText(QStringLiteral("ЯТП · точка %1 Ом · канал %2 / 30 · Р4831 %3 Ом").arg(adapter.ytp.resistancePointOhm,0,'f',0).arg(adapter.ytp.testedChannel).arg(adapter.ytp.actualReferenceOhm,0,'f',3));yvpPlane->setFrame(adapter.yvp,yvpCompleted);yvpChannel->setText(QStringLiteral("%1 / 8").arg(adapter.yvp.testedChannel));yvpGain->setText(QStringLiteral("%1 мВ/пКл").arg(adapter.yvp.gain,0,'g',4));double calc=std::numeric_limits<double>::quiet_NaN();for(const auto&c:adapter.yvp.channels)if(c.channel==adapter.yvp.testedChannel){calc=c.calculatedGain;break;}yvpCalculated->setText(std::isfinite(calc)?QStringLiteral("%1 мВ/пКл").arg(calc,0,'g',4):QStringLiteral("— мВ/пКл"));yvpAcceptance->setText(adapter.yvp.acceptanceApplied?QStringLiteral("критерий приёмки применён"):QStringLiteral("критерий приёмки не применён"));yvpContext->setText(QStringLiteral("ЯВП-8 · канал %1 / 8 · Kу %2 · %3 Гц · точка %4 / %5").arg(adapter.yvp.testedChannel).arg(adapter.yvp.gain,0,'g',4).arg(adapter.yvp.frequencyHz,0,'g',6).arg(yvpCompleted).arg(adapter.yvp.pointCount));
    }
    void advanceQueue()
    {
        int r=activeRow;if(r>=0&&r<queue->rowCount()&&queue->item(r,2))queue->item(r,2)->setText(QStringLiteral("ЗАВЕРШЕНО"));int n=r+1;if(n>=0&&n<queue->rowCount()){queue->selectRow(n);activeRow=n;activeSerial=queue->item(n,0)->text();serialEdit->setText(activeSerial);pages->setCurrentWidget(preparationPage);q->updateStartAvailability();}else pages->setCurrentWidget(sessionPage);
    }

    TestPage* q=nullptr;QVBoxLayout* root=nullptr;QStackedWidget* pages=nullptr;QWidget* bridge=nullptr;QWidget* sessionPage=nullptr;QWidget* preparationPage=nullptr;QWidget* runtimePageWidget=nullptr;TuFlowWidget* tuFlow=nullptr;
    QComboBox* scopeCombo=nullptr;QComboBox* testCombo=nullptr;QComboBox* modeCombo=nullptr;QCheckBox* partial=nullptr;QCheckBox* includeYvp=nullptr;QCheckBox* includeOverload=nullptr;QCheckBox* includeSurvival=nullptr;
    QLineEdit* registrySearch=nullptr;QTableWidget* registry=nullptr;QTableWidget* queue=nullptr;QComboBox* operatorSelector=nullptr;QComboBox* stage=nullptr;QButtonGroup* scopeGroup=nullptr;QVector<QPushButton*> scopeButtons;QLabel* sessionSummary=nullptr;QPushButton* enterPreparation=nullptr;QLineEdit* serialEdit=nullptr;QLineEdit* operatorEdit=nullptr;
    QTableWidget* equipmentTable=nullptr;QLabel* readiness=nullptr;QPushButton* checkButton=nullptr;QPushButton* startButton=nullptr;
    QLabel* runTitle=nullptr;QLabel* runSubtitle=nullptr;QPushButton* stopButton=nullptr;QLabel* context=nullptr;QLabel* progressText=nullptr;QFrame* sidebar=nullptr;QLabel* sideTitle=nullptr;QVBoxLayout* procedureSteps=nullptr;QStackedWidget* stageStack=nullptr;QLabel* elapsed=nullptr;QLabel* current=nullptr;HistoryPlot* currentPlot=nullptr;
    QLabel* powerSet=nullptr;QLabel* powerActual=nullptr;QLabel* powerHold=nullptr;HistoryPlot* powerPlot=nullptr;QLabel* powerYalkStatus=nullptr;ChannelPlane* powerYalk=nullptr;InitialStateView* initialView=nullptr;ChannelPlane* yalkPlane=nullptr;QWidget* legacyAnalogProxy=nullptr;QLabel* analogContext=nullptr;ContactPlane* contactPlane=nullptr;QLabel* contactsContext=nullptr;OverloadPlane* overloadPlane=nullptr;QLabel* overloadContext=nullptr;QLabel* referenceText=nullptr;ChannelPlane* ytpPlane=nullptr;QLabel* ytpContext=nullptr;YvpPlane* yvpPlane=nullptr;QLabel* yvpContext=nullptr;QLabel* yvpChannel=nullptr;QLabel* yvpGain=nullptr;QLabel* yvpCalculated=nullptr;QLabel* yvpAcceptance=nullptr;QLabel* finishVerdict=nullptr;QLabel* finishMeta=nullptr;std::array<QLabel*,4> finishSummary{};QLineEdit* finishComment=nullptr;
    QVector<QLabel*> routeAnchors;QHash<QString,ScenarioInfo> scenarios;QHash<QString,EquipmentRow> equipmentRows;QStringList registeredSerials;QString activeSerial;QString activeOperator;int activeRow=-1;bool productionMode=false;bool engineerMode=false;bool runInProgress=false;EquipmentInvoke equipmentInvoke;UiAdapter adapter;int contactMeasurements=0;int yvpCompleted=0;QElapsedTimer clock;QTimer* timer=nullptr;
};

TestPage::TestPage(QWidget* parent):QWidget(parent),impl_(std::make_unique<Impl>(this))
{
    rebuildScopes();
    connect(impl_->scopeCombo,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){rebuildTests();updateSelectionSummary();});
}
TestPage::~TestPage()=default;
bool TestPage::eventFilter(QObject* watched,QEvent* event){return QWidget::eventFilter(watched,event);}
void TestPage::setEquipmentInvoker(EquipmentInvoke invoke){impl_->equipmentInvoke=std::move(invoke);}
void TestPage::registerEquipmentRow(const QString& code,const QString& name,const QString& connection,const QString& initialDetail,bool operatorConfirmation){impl_->addEquipment(code,name,connection,initialDetail,operatorConfirmation);}
void TestPage::setEquipmentStatus(const QString& code,bool ready,const QString& detail){if(!impl_->productionMode&&impl_->tuFlow)impl_->tuFlow->setEquipmentStatus(code,ready,detail);auto it=impl_->equipmentRows.find(code);if(it==impl_->equipmentRows.end())return;if(!it->confirmation)it->ready=ready;if(auto* s=impl_->equipmentTable->item(it->row,1)){s->setText(ready?QStringLiteral("ГОТОВО"):QStringLiteral("ОШИБКА"));s->setForeground(ready?palette::green:palette::red);}if(auto*d=impl_->equipmentTable->item(it->row,2))d->setText(detail);updateStartAvailability();}
void TestPage::setEquipmentConnection(const QString& code,const QString& connection){auto it=impl_->equipmentRows.find(code);if(it!=impl_->equipmentRows.end())it->connection=connection;}
void TestPage::setEquipmentMissingPlugin(const QString& code,const QString& detail){setEquipmentStatus(code,false,detail);}
void TestPage::setEquipmentChecking(const QString& code,const QString& detail){if(!impl_->productionMode&&impl_->tuFlow)impl_->tuFlow->setEquipmentChecking(code);auto it=impl_->equipmentRows.find(code);if(it==impl_->equipmentRows.end())return;it->ready=false;if(auto*s=impl_->equipmentTable->item(it->row,1)){s->setText(QStringLiteral("ПРОВЕРКА"));s->setForeground(palette::amber);}if(auto*d=impl_->equipmentTable->item(it->row,2))d->setText(detail);updateStartAvailability();}
void TestPage::setScenarioInfo(const QString& code,bool available,bool diagnostic,const QStringList& requiredEquipment,const QString& detail){impl_->scenarios[code]={available,diagnostic,requiredEquipment,detail};if(!impl_->productionMode&&impl_->tuFlow&&code==QStringLiteral("ULK_COMBINED_CHECK"))impl_->tuFlow->setScenarioAvailable(available,detail);updateSelectionSummary();}
void TestPage::setEngineerMode(bool enabled){impl_->engineerMode=enabled;}
bool TestPage::isEngineerMode()const{return impl_->engineerMode;}
void TestPage::setProductionMode(bool enabled){impl_->productionMode=enabled;if(enabled){impl_->pages->setCurrentWidget(impl_->sessionPage);rebuildScopes();}else{impl_->pages->setCurrentWidget(impl_->tuFlow);rebuildScopes();impl_->tuFlow->setRegisteredSerials(impl_->registeredSerials);}updateSelectionSummary();}
void TestPage::setAvailableProductionProducts(const QStringList& serials){impl_->registeredSerials=serials;impl_->registeredSerials.removeDuplicates();impl_->registeredSerials.sort(Qt::CaseInsensitive);setProperty("productionRegisteredSerials",impl_->registeredSerials);impl_->refreshRegistry();if(impl_->tuFlow)impl_->tuFlow->setRegisteredSerials(impl_->registeredSerials);}
QStringList TestPage::currentRequiredEquipment()const{return impl_->scenarios.value(currentScenarioCode()).required;}
QString TestPage::currentScenarioCode()const{return impl_->testCombo->currentData().toString();}
bool TestPage::includeYvp()const{return impl_->includeYvp->isChecked();}
bool TestPage::includeProductionOverload()const{return impl_->includeOverload->isChecked();}
bool TestPage::includeProductionSurvival()const{return impl_->includeSurvival->isChecked();}
void TestPage::rebuildScopes(){const QString previous=impl_->scopeCombo->currentData().toString();impl_->scopeCombo->blockSignals(true);impl_->scopeCombo->clear();if(impl_->productionMode){impl_->scopeCombo->addItem(QStringLiteral("Полная УБСИ"),QStringLiteral("УБСИ ПО ТУ"));impl_->scopeCombo->addItem(QStringLiteral("ЯЛК-96"),QStringLiteral("ЯЛК-96"));impl_->scopeCombo->addItem(QStringLiteral("ЯТП"),QStringLiteral("ЯТП"));impl_->scopeCombo->addItem(QStringLiteral("ЯВП-8"),QStringLiteral("ЯВП-8"));}else{impl_->scopeCombo->addItem(QStringLiteral("УБСИ ПО ТУ"),QStringLiteral("УБСИ ПО ТУ"));}int i=impl_->scopeCombo->findData(previous);impl_->scopeCombo->setCurrentIndex(i>=0?i:0);impl_->scopeCombo->blockSignals(false);if(impl_->productionMode){const int id=std::max(0,impl_->scopeCombo->currentIndex());if(id<impl_->scopeButtons.size())impl_->scopeButtons[id]->setChecked(true);}rebuildTests();impl_->configureRoute();}
void TestPage::rebuildTests(){const QString scope=impl_->scopeCombo->currentData().toString();impl_->testCombo->blockSignals(true);impl_->testCombo->clear();if(impl_->productionMode){const QString code=scenarioForScope(scope);impl_->testCombo->addItem(scopeDisplay(scope),code);}else{impl_->testCombo->addItem(QStringLiteral("Полная проверка УБСИ · ТУ"),QStringLiteral("ULK_COMBINED_CHECK"));}impl_->testCombo->setCurrentIndex(0);impl_->testCombo->blockSignals(false);updateSelectionSummary();}
void TestPage::updateSelectionSummary(){const auto info=impl_->scenarios.value(currentScenarioCode());for(auto it=impl_->equipmentRows.begin();it!=impl_->equipmentRows.end();++it){const bool visible=info.required.contains(it.key());impl_->equipmentTable->setRowHidden(it->row,!visible);}impl_->refreshSessionSummary();updateStartAvailability();}
void TestPage::updateStartAvailability(){const auto info=impl_->scenarios.value(currentScenarioCode());bool ready=info.available;for(const auto& code:info.required){if(code==QStringLiteral("R4831")||code==QStringLiteral("SCHEME"))continue;auto it=impl_->equipmentRows.constFind(code);if(it==impl_->equipmentRows.cend()||!it->ready){ready=false;break;}}impl_->checkButton->setEnabled(!impl_->runInProgress&&info.available);impl_->startButton->setEnabled(!impl_->runInProgress&&ready);impl_->stopButton->setEnabled(impl_->runInProgress);if(impl_->runInProgress)impl_->readiness->setText(QStringLiteral("Проверка выполняется"));else if(!info.available)impl_->readiness->setText(info.detail.isEmpty()?QStringLiteral("Сценарий недоступен"):info.detail);else if(!ready)impl_->readiness->setText(QStringLiteral("Проверьте оборудование"));else impl_->readiness->setText(QStringLiteral("Оборудование готово. Можно запускать испытание."));}
void TestPage::startSelectedTest(){if(impl_->productionMode){if(impl_->queue->rowCount()==0){QMessageBox::warning(this,QStringLiteral("УБСИ"),QStringLiteral("Очередь сессии пуста."));return;}int r=impl_->queue->currentRow();if(r<0)r=0;impl_->activeRow=r;impl_->activeSerial=impl_->queue->item(r,0)->text();impl_->activeOperator=impl_->operatorSelector->currentData().toString();impl_->queue->selectRow(r);impl_->queue->item(r,2)->setText(QStringLiteral("В РАБОТЕ"));}if(impl_->activeSerial.isEmpty())impl_->activeSerial=impl_->serialEdit->text().trimmed();if(impl_->activeSerial.isEmpty())return;impl_->serialEdit->setText(impl_->activeSerial);impl_->runTitle->setText(QStringLiteral("УБСИ %1").arg(impl_->activeSerial));impl_->runSubtitle->setText(impl_->productionMode?QStringLiteral("Производство · %1").arg(scopeDisplay(impl_->scopeCombo->currentData().toString())):QStringLiteral("Проверка по ТУ"));impl_->pages->setCurrentWidget(impl_->runtimePageWidget);impl_->resetRuntime();emit runRequested(currentScenarioCode(),impl_->activeSerial,false);}
void TestPage::advanceDemo(){}
void TestPage::setRunInProgress(bool running,const QString& stage){impl_->runInProgress=running;if(running){impl_->clock.restart();impl_->timer->start();if(!stage.isEmpty())impl_->progressText->setText(stage);}else impl_->timer->stop();updateStartAvailability();}
void TestPage::setRunEvent(const orbita::stand::RunEvent& event){if(!impl_->runInProgress)return;const QString stage=QString::fromStdString(event.stage);if(stage==QStringLiteral("MEASUREMENT")&&QString::fromStdString(event.nodeId).contains(QStringLiteral("yalk_contact")))++impl_->contactMeasurements;if(stage==QStringLiteral("YVP_V7_POINT"))++impl_->yvpCompleted;impl_->adapter.apply(event);impl_->renderModel();if(stage==QStringLiteral("POWER_YALK")){const bool fresh=eventValue(event,"fresh")==QStringLiteral("true");impl_->powerYalkStatus->setText(fresh?QStringLiteral("ЯЛК · свежий снимок 80 каналов · питание %1 В").arg(eventValue(event,"setpoint_v")):QStringLiteral("ЯЛК · НЕТ СВЕЖИХ ДАННЫХ · питание %1 В").arg(eventValue(event,"setpoint_v")));impl_->powerYalkStatus->setStyleSheet(fresh?QStringLiteral("color:#35cf79;"):QStringLiteral("color:#e1ad46;font-weight:700;"));}if(stage==QStringLiteral("YVP_V7_POINT")){impl_->context->setText(QStringLiteral("Канал %1 / 8 · Kу %2 · %3 Гц").arg(eventValue(event,"yvp_channel"),eventValue(event,"gain_mv_per_pc"),eventValue(event,"set_frequency_hz")));}}
void TestPage::setRunResult(const orbita::stand::ScenarioRunResult& result,const QString&,const QString&){impl_->adapter.applyResult(result);impl_->runInProgress=false;impl_->timer->stop();impl_->pages->setCurrentWidget(impl_->runtimePageWidget);impl_->showProcedure(Procedure::Finish);impl_->finishVerdict->setText(verificationText(impl_->adapter.run.productVerdict));impl_->finishMeta->setText(QStringLiteral("УБСИ %1 · %2 · %3").arg(impl_->activeSerial,impl_->stage->currentText(),impl_->activeOperator));for(int i=0;i<4;++i){const auto&s=impl_->adapter.summaries[i];impl_->finishSummary[i]->setText(s.stepCount?QStringLiteral("%1%2").arg(verificationText(s.verdict),s.stepCount>1?QStringLiteral(" · %1 шагов").arg(s.stepCount):QString()):QStringLiteral("—"));impl_->finishSummary[i]->setStyleSheet(QStringLiteral("color:%1;font-weight:700;").arg(stateColor(s.verdict).name()));}if(impl_->activeRow>=0&&impl_->activeRow<impl_->queue->rowCount()&&impl_->queue->item(impl_->activeRow,2))impl_->queue->item(impl_->activeRow,2)->setText(verificationText(impl_->adapter.run.productVerdict));updateStartAvailability();}
