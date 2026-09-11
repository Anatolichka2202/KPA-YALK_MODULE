#pragma once

struct TestPage::Impl
{
    struct EquipmentRow { int row=-1; bool ready=false; bool operatorConfirmation=false; QString connection; };
    struct ScenarioInfo { bool available=false; bool diagnostic=false; QStringList required; QString detail; };

    explicit Impl(TestPage* q) : q(q)
    {
        q->setObjectName(QStringLiteral("operatorTestPage"));
        q->setStyleSheet(pageStyle());

        root = new QVBoxLayout(q);
        root->setContentsMargins(14, 10, 14, 10);
        pages = new QStackedWidget;
        root->addWidget(pages, 1);

        buildBridge();
        buildSession();
        buildWorkspace();
        loadOperatorHistory();
        pages->setCurrentWidget(sessionPage);

        runClockTimer = new QTimer(q);
        runClockTimer->setInterval(250);
        QObject::connect(runClockTimer, &QTimer::timeout, q, [this] {
            if (runClock.isValid()) elapsed->setText(elapsedText(runClock.elapsed()));
        });
    }

    void buildBridge()
    {
        bridge = new QWidget;
        bridge->setVisible(false);
        auto* layout = new QHBoxLayout(bridge);
        layout->setContentsMargins(0,0,0,0);
        auto makeBridgeCombo = [this, layout](const QString& name) {
            auto* wrapper = new QWidget(bridge);
            auto* wrapperLayout = new QHBoxLayout(wrapper);
            wrapperLayout->setContentsMargins(0,0,0,0);
            auto* combo = new QComboBox(wrapper);
            combo->setObjectName(name);
            wrapperLayout->addWidget(combo);
            layout->addWidget(wrapper);
            return combo;
        };
        objectCombo = makeBridgeCombo(QStringLiteral("testObject"));
        objectCombo->addItem(QStringLiteral("УЛК · ЯЛК-96 + ЯТП"), QStringLiteral("UBSI-7"));
        scopeCombo = makeBridgeCombo(QStringLiteral("testScope"));
        testCombo = makeBridgeCombo(QStringLiteral("testType"));
        modeCombo = makeBridgeCombo(QStringLiteral("testMode"));
        modeCombo->addItem(QStringLiteral("Стенд — реальное оборудование"));
        modeCombo->addItem(QStringLiteral("Демонстрация интерфейса — имитация"));
        partial = new QCheckBox(QStringLiteral("Диагностический запуск"));
        partial->setObjectName(QStringLiteral("allowPartial"));
        includeYvpCheck = new QCheckBox(QStringLiteral("Включить ЯВП"));
        includeYvpCheck->setObjectName(QStringLiteral("includeYvp"));
        includeYvpCheck->setChecked(true);
        includeOverload = new QCheckBox(QStringLiteral("ЯЛК: перегрузка"));
        includeOverload->setChecked(true);
        includeSurvival = new QCheckBox(QStringLiteral("Выдержки 19 / 37 В"));
        includeSurvival->setChecked(true);
        layout->addWidget(partial);
        layout->addWidget(includeYvpCheck);
        layout->addWidget(includeOverload);
        layout->addWidget(includeSurvival);
        root->addWidget(bridge);

        QObject::connect(scopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), q, [this] {
            if (!productionMode) q->rebuildTests();
            q->updateSelectionSummary();
        });
        QObject::connect(testCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), q, [this] {
            q->updateSelectionSummary();
        });
    }

    void buildSession()
    {
        sessionPage = new QWidget;
        auto* layout = new QVBoxLayout(sessionPage);
        layout->setContentsMargins(24, 18, 24, 18);
        layout->setSpacing(11);

        auto* top = new QHBoxLayout;
        home = new QPushButton(QStringLiteral("← КТМА"));
        home->setMaximumWidth(105);
        top->addWidget(home);
        auto* titleBox = new QVBoxLayout;
        titleBox->setSpacing(1);
        sessionTitle = titleLabel(QStringLiteral("Производственная сессия"));
        sessionSubtitle = subtitleLabel(QStringLiteral("Один оператор может последовательно проверить несколько УБСИ."));
        titleBox->addWidget(sessionTitle);
        titleBox->addWidget(sessionSubtitle);
        top->addLayout(titleBox, 1);
        workflowBadge = new QLabel(QStringLiteral("ПРОИЗВОДСТВО"));
        workflowBadge->setStyleSheet(QStringLiteral("background:#14251c;color:#70d79b;border:1px solid #315c43;border-radius:5px;padding:8px 12px;font-weight:700;"));
        top->addWidget(workflowBadge,0,Qt::AlignTop);
        layout->addLayout(top);

        sessionDataPanel = panel(true);
        auto* sessionData = new QHBoxLayout(sessionDataPanel);
        sessionData->setContentsMargins(13, 9, 13, 9);
        operatorCaption = new QLabel(QStringLiteral("ФИО оператора"));
        operatorEdit = new QLineEdit;
        operatorEdit->setObjectName(QStringLiteral("operatorName"));
        operatorEdit->setPlaceholderText(QStringLiteral("Фамилия Имя Отчество"));
        operatorEdit->setMinimumWidth(250);
        operatorHistory = new QComboBox;
        operatorHistory->setObjectName(QStringLiteral("operatorHistory"));
        operatorHistory->setMinimumWidth(210);
        operatorHistory->setToolTip(QStringLiteral("Последние операторы"));
        serialCaption = new QLabel(QStringLiteral("SN УБСИ"));
        serialEdit = new QLineEdit;
        serialEdit->setObjectName(QStringLiteral("objectSerial"));
        serialEdit->setPlaceholderText(QStringLiteral("например, УБСИ-001"));
        serialEdit->setMinimumWidth(220);
        addProduct = new QPushButton(QStringLiteral("+ Добавить УБСИ"));
        addProduct->setObjectName(QStringLiteral("addProductionProduct"));
        sessionData->addWidget(operatorCaption);
        sessionData->addWidget(operatorEdit);
        sessionData->addWidget(operatorHistory);
        sessionData->addSpacing(14);
        sessionData->addWidget(serialCaption);
        sessionData->addWidget(serialEdit);
        sessionData->addWidget(addProduct);
        layout->addWidget(sessionDataPanel);
        QObject::connect(operatorHistory, QOverload<int>::of(&QComboBox::activated), q,
            [this](int index) {
                if (index > 0) operatorEdit->setText(operatorHistory->itemText(index));
            });

        auto* body = new QHBoxLayout;
        body->setSpacing(10);
        productsPanel = panel();
        auto* productsLayout = new QVBoxLayout(productsPanel);
        productsLayout->setContentsMargins(11,11,11,11);
        productsLayout->addWidget(sectionLabel(QStringLiteral("УБСИ в текущей сессии")));
        productTable = new QTableWidget(0,3);
        productTable->setObjectName(QStringLiteral("productionSessionTable"));
        productTable->setHorizontalHeaderLabels({QStringLiteral("Заводской №"),QStringLiteral("Объём"),QStringLiteral("Результат")});
        productTable->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
        productTable->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
        productTable->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
        productTable->verticalHeader()->hide();
        productTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        productTable->setSelectionMode(QAbstractItemView::SingleSelection);
        productTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        productsLayout->addWidget(productTable,1);
        body->addWidget(productsPanel,3);

        auto* scopePanel = panel();
        auto* scopeLayout = new QVBoxLayout(scopePanel);
        scopeLayout->setContentsMargins(11,11,11,11);
        scopeLayout->addWidget(sectionLabel(QStringLiteral("Объём проверки")));
        scopeGroup = new QButtonGroup(q);
        scopeGroup->setExclusive(true);
        auto* scopeGrid = new QGridLayout;
        const QVector<std::tuple<QString,QString,QString>> definitions = {
            {QStringLiteral("▣  ПОЛНАЯ ПРОВЕРКА УБСИ\nПитание · ЯЛК-96 · ЯТП · ЯВП-8"),QStringLiteral("УБСИ ПО ТУ"),QStringLiteral("Полный производственный маршрут. ЯП-П в объём проверки не входит.")},
            {QStringLiteral("∿  ЯЛК-96\nотдельная ячейка"),QStringLiteral("ЯЛК-96"),QStringLiteral("Проверка ЯЛК-96. Для Production backend сейчас исполняется полный пакет ЯЛК.")},
            {QStringLiteral("Ω  ЯТП\n30 каналов"),QStringLiteral("ЯТП"),QStringLiteral("Поток, калибровка и 30 каналов при 0 / 120 / 240 Ом.")},
            {QStringLiteral("ƒ  ЯВП-8\n8 каналов"),QStringLiteral("ЯВП-8"),QStringLiteral("Коэффициенты усиления, АЧХ и затухание. Доступность определяет backend.")}
        };
        for(int i=0;i<definitions.size();++i){
            auto* button=new QPushButton(std::get<0>(definitions[i]));
            button->setObjectName(i==0?QStringLiteral("modeCard_УБСИ ПО ТУ"):QStringLiteral("scopeCard"));
            if(i!=0) button->setProperty("class",QStringLiteral("scopeCard"));
            button->setStyleSheet(QStringLiteral("QPushButton{background:#12161c;color:#dfe6ee;border:1px solid #2c333d;border-radius:9px;text-align:left;padding:13px;font-weight:650;min-height:74px;} QPushButton:hover{background:#1b2129;border:2px solid #5e93b8;} QPushButton:checked{background:#18212c;border:2px solid #5e93b8;color:#9ac7ff;}"));
            button->setCheckable(true);
            button->setToolTip(std::get<2>(definitions[i]));
            button->setProperty("scopeCode",std::get<1>(definitions[i]));
            scopeGroup->addButton(button,i);
            scopeGrid->addWidget(button,i/2,i%2);
            scopeButtons.insert(std::get<1>(definitions[i]),button);
        }
        scopeGroup->button(0)->setChecked(true);
        scopeLayout->addLayout(scopeGrid);

        yalkSubPanel = new QWidget;
        auto* yalkSubLayout = new QVBoxLayout(yalkSubPanel);
        yalkSubLayout->setContentsMargins(0,5,0,0);
        yalkSubLayout->addWidget(mutedLabel(QStringLiteral("ПРОВЕРКА ЯЛК-96")));
        yalkSubGroup = new QButtonGroup(q);
        yalkSubGroup->setExclusive(true);
        auto* yalkGrid = new QGridLayout;
        const QVector<QPair<QString,QString>> yalkChecks = {
            {QStringLiteral("▣  Полная ЯЛК-96"),QStringLiteral("Полный производственный пакет ЯЛК: питание, инициализация, калибровка, исходное состояние, аналоговые каналы, дискретные пороги, перегрузка и эталон.")},
            {QStringLiteral("∿  Аналоговые каналы"),QStringLiteral("Дизайн отдельной проверки. Отдельный Production scenario в текущем backend не зарегистрирован.")},
            {QStringLiteral("01  Дискретные пороги"),QStringLiteral("Дизайн отдельной проверки. Отдельный Production scenario в текущем backend не зарегистрирован.")},
            {QStringLiteral("±12  Перегрузка"),QStringLiteral("Дизайн отдельной проверки. Отдельный Production scenario в текущем backend не зарегистрирован.")},
            {QStringLiteral("6.2  Эталон"),QStringLiteral("Дизайн отдельной проверки. Отдельный Production scenario в текущем backend не зарегистрирован.")}
        };
        for(int i=0;i<yalkChecks.size();++i){
            auto* b=new QPushButton(yalkChecks[i].first);
            b->setStyleSheet(QStringLiteral("QPushButton{background:#12161c;color:#dfe6ee;border:1px solid #2c333d;border-radius:8px;text-align:left;padding:11px;min-height:58px;} QPushButton:hover{border:2px solid #5e93b8;} QPushButton:checked{background:#18212c;border:2px solid #5e93b8;color:#9ac7ff;}"));
            b->setCheckable(true); b->setToolTip(yalkChecks[i].second); b->setToolTipDuration(15000);
            yalkSubGroup->addButton(b,i); yalkGrid->addWidget(b,i/3,i%3);
        }
        yalkSubGroup->button(0)->setChecked(true);
        yalkSubLayout->addLayout(yalkGrid);
        yalkSubPanel->setVisible(false);
        scopeLayout->addWidget(yalkSubPanel);

        scenarioInfo = subtitleLabel(QStringLiteral("Полная проверка УБСИ: питание → ЯЛК-96 → ЯТП → ЯВП-8 → завершение."));
        scopeLayout->addWidget(scenarioInfo);
        scopeLayout->addStretch();
        enterPreparation = new QPushButton(QStringLiteral("Перейти к подготовке"));
        enterPreparation->setObjectName(QStringLiteral("enterPreparation"));
        enterPreparation->setStyleSheet(QStringLiteral("background:#214e78;border:1px solid #5e93b8;color:#eef7ff;font-weight:700;padding:10px 18px;"));
        enterPreparation->setMinimumHeight(42);
        scopeLayout->addWidget(enterPreparation);
        body->addWidget(scopePanel,2);
        layout->addLayout(body,1);

        engineerBridgePanel = panel();
        auto* engineerLayout = new QHBoxLayout(engineerBridgePanel);
        engineerLayout->setContentsMargins(8,6,8,6);
        engineerLayout->addWidget(mutedLabel(QStringLiteral("Инженерный режим: сценарий выбирается операторскими карточками; YAML/профиль открываются из меню приложения.")));
        engineerBridgePanel->setVisible(false);
        layout->addWidget(engineerBridgePanel);

        pages->addWidget(sessionPage);

        QObject::connect(home,&QPushButton::clicked,q,&TestPage::homeRequested);
        QObject::connect(addProduct,&QPushButton::clicked,q,[this]{ addSerial(); });
        QObject::connect(productTable,&QTableWidget::currentCellChanged,q,[this](int row,int,int,int){
            if(row>=0&&row<productTable->rowCount()) serialEdit->setText(productTable->item(row,0)->text());
        });
        QObject::connect(scopeGroup,&QButtonGroup::idClicked,q,[this](int id){ selectScope(id); });
        QObject::connect(yalkSubGroup,&QButtonGroup::idClicked,q,[this](int id){
            if(id==0){scenarioInfo->setText(QStringLiteral("Полная ЯЛК-96 · backend PROD_YALK"));enterPreparation->setEnabled(true);}
            else {scenarioInfo->setText(QStringLiteral("Отдельный Production scenario для этой подпроверки пока не зарегистрирован. Полная ЯЛК-96 работает через backend."));enterPreparation->setEnabled(false);}
        });
        QObject::connect(enterPreparation,&QPushButton::clicked,q,[this]{ enterWorkspace(); });
    }

    void buildWorkspace()
    {
        workspacePage = new QWidget;
        auto* outer = new QVBoxLayout(workspacePage);
        outer->setContentsMargins(0,0,0,0);
        outer->setSpacing(7);

        auto* top = new QHBoxLayout;
        backSession = new QPushButton(QStringLiteral("← Сессия"));
        top->addWidget(backSession);
        auto* text = new QVBoxLayout;
        text->setSpacing(0);
        workspaceTitle = new QLabel(QStringLiteral("УБСИ"));
        workspaceTitle->setStyleSheet(QStringLiteral("font-size:22px;font-weight:700;color:#f1f5f9;"));
        workspaceSubtitle = subtitleLabel(QStringLiteral("Проверка"));
        text->addWidget(workspaceTitle);text->addWidget(workspaceSubtitle);top->addLayout(text,1);
        operatorBadge = new QLabel;
        operatorBadge->setStyleSheet(QStringLiteral("background:#132033;color:#9ac7ff;border:1px solid #27466c;border-radius:5px;padding:7px 10px;"));
        top->addWidget(operatorBadge);
        stopButton = new QPushButton(QStringLiteral("Остановить"));
        stopButton->setEnabled(false);
        top->addWidget(stopButton);
        outer->addLayout(top);

        auto* body = new QHBoxLayout;
        body->setSpacing(8);
        auto* side=panel();side->setFixedWidth(238);
        auto* sideLayout=new QVBoxLayout(side);sideLayout->setContentsMargins(9,9,9,9);
        sideLayout->addWidget(sectionLabel(QStringLiteral("Маршрут УБСИ")));
        const QStringList names={QStringLiteral("Подготовка"),QStringLiteral("Питание / потребление"),QStringLiteral("ЯЛК-96"),QStringLiteral("ЯТП"),QStringLiteral("ЯВП-8"),QStringLiteral("Завершение")};
        for(int i=0;i<names.size();++i){auto*l=new QLabel(QStringLiteral("%1. %2").arg(i+1).arg(names[i]));l->setObjectName(QStringLiteral("stage"));l->setMinimumHeight(46);l->setWordWrap(true);stageLabels.push_back(l);sideLayout->addWidget(l);} sideLayout->addStretch();
        body->addWidget(side);

        auto* work=new QVBoxLayout;
        workStack=new QStackedWidget;
        workStack->addWidget(buildPreparation());
        workStack->addWidget(buildPower());
        workStack->addWidget(buildYalk());
        workStack->addWidget(buildYtp());
        workStack->addWidget(buildYvp());
        workStack->addWidget(buildFinish());
        work->addWidget(workStack,1);

        auto* footer=panel();auto* footerLayout=new QHBoxLayout(footer);footerLayout->setContentsMargins(10,6,10,6);
        auto* elapsedBox=new QVBoxLayout;elapsedBox->setSpacing(0);elapsedBox->addWidget(mutedLabel(QStringLiteral("Время")));elapsed=new QLabel(QStringLiteral("00:00:00"));elapsed->setObjectName(QStringLiteral("metricValue"));elapsedBox->addWidget(elapsed);footerLayout->addLayout(elapsedBox,1);
        consumption=new TrendPlot;consumption->configure(QStringLiteral("Потребление УБСИ"),QStringLiteral("А"),QStringLiteral("I"));consumption->setMaximumHeight(88);footerLayout->addWidget(consumption,3);
        auto* progressBox=new QVBoxLayout;progressBox->setSpacing(2);footerStage=mutedLabel(QStringLiteral("Подготовка"));progressBox->addWidget(footerStage);progress=new QProgressBar;progress->setRange(0,100);progress->setValue(0);progress->setMinimumWidth(250);progressBox->addWidget(progress);footerLayout->addLayout(progressBox,2);
        work->addWidget(footer);
        body->addLayout(work,1);
        outer->addLayout(body,1);
        pages->addWidget(workspacePage);

        QObject::connect(backSession,&QPushButton::clicked,q,[this]{ if(!runInProgress) pages->setCurrentWidget(sessionPage); });
        QObject::connect(stopButton,&QPushButton::clicked,q,&TestPage::stopRequested);
    }

    QWidget* buildPreparation()
    {
        auto* page=new QWidget;auto* l=new QVBoxLayout(page);l->setContentsMargins(0,0,0,0);l->setSpacing(8);
        l->addWidget(titleLabel(QStringLiteral("Подготовка к проверке")));
        preparationSubtitle=subtitleLabel(QStringLiteral("Проверка необходимого оборудования выполняется в этом же испытательном окне."));l->addWidget(preparationSubtitle);
        auto* frame=panel();auto* fl=new QVBoxLayout(frame);fl->setContentsMargins(10,10,10,10);
        equipmentTable=new QTableWidget(0,5);equipmentTable->setObjectName(QStringLiteral("equipmentTable"));equipmentTable->setHorizontalHeaderLabels({QStringLiteral("Устройство"),QStringLiteral("Связь с ПЭВМ"),QStringLiteral("Контроль"),QStringLiteral("Состояние"),QStringLiteral("Диагностика")});
        equipmentTable->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);equipmentTable->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);equipmentTable->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);equipmentTable->horizontalHeader()->setSectionResizeMode(3,QHeaderView::ResizeToContents);equipmentTable->horizontalHeader()->setSectionResizeMode(4,QHeaderView::Stretch);equipmentTable->verticalHeader()->hide();equipmentTable->setEditTriggers(QAbstractItemView::NoEditTriggers);equipmentTable->setSelectionMode(QAbstractItemView::NoSelection);fl->addWidget(equipmentTable);l->addWidget(frame,1);
        addEquipment(QStringLiteral("RS485"),QStringLiteral("Адаптер УЛК"),QStringLiteral("профиль стенда"),QStringLiteral("не проверено"),false);
        addEquipment(QStringLiteral("ISD"),QStringLiteral("ИСД"),QStringLiteral("профиль стенда"),QStringLiteral("не проверено"),false);
        addEquipment(QStringLiteral("V7"),QStringLiteral("В7-78/1"),QStringLiteral("профиль стенда"),QStringLiteral("не проверено"),false);
        addEquipment(QStringLiteral("AKIP"),QStringLiteral("АКИП-1160/6"),QStringLiteral("профиль стенда"),QStringLiteral("не проверено"),false);
        addEquipment(QStringLiteral("R4831"),QStringLiteral("Магазин Р4831"),QStringLiteral("ручной"),QStringLiteral("значение запрашивается диалогом при смене точки"),true);

        auto* actions=panel(true);auto* al=new QHBoxLayout(actions);al->setContentsMargins(11,8,11,8);readiness=new QLabel(QStringLiteral("Оборудование ещё не проверено"));readiness->setStyleSheet(QStringLiteral("color:#d7a95b;font-weight:700;"));al->addWidget(readiness,1);checkButton=new QPushButton(QStringLiteral("Проверить оборудование"));checkButton->setObjectName(QStringLiteral("equipmentCheckButton"));checkButton->setStyleSheet(QStringLiteral("background:#214e78;border:1px solid #5e93b8;color:#eef7ff;font-weight:700;"));startButton=new QPushButton(QStringLiteral("Начать проверку"));startButton->setObjectName(QStringLiteral("startTestButton"));startButton->setStyleSheet(QStringLiteral("background:#214e78;border:1px solid #5e93b8;color:#eef7ff;font-weight:700;"));startButton->setEnabled(false);al->addWidget(checkButton);al->addWidget(startButton);l->addWidget(actions);
        QObject::connect(checkButton,&QPushButton::clicked,q,&TestPage::equipmentCheckRequested);
        QObject::connect(startButton,&QPushButton::clicked,q,&TestPage::startSelectedTest);
        return page;
    }

    QWidget* buildPower()
    {
        auto* page=new QWidget;auto* l=new QVBoxLayout(page);l->setContentsMargins(0,0,0,0);l->setSpacing(7);l->addWidget(titleLabel(QStringLiteral("Питание / потребление")));
        auto* metrics=new QHBoxLayout;metrics->addWidget(metricCard(QStringLiteral("Задано"),powerSet),1);metrics->addWidget(metricCard(QStringLiteral("Измерено U"),powerActual),1);metrics->addWidget(metricCard(QStringLiteral("Ток УБСИ"),powerCurrent),1);metrics->addWidget(metricCard(QStringLiteral("Выдержка"),powerHold),1);l->addLayout(metrics);
        powerTrend=new TrendPlot;powerTrend->configure(QStringLiteral("Напряжение · задано / измерено"),QStringLiteral("В"),QStringLiteral("задано"),QStringLiteral("измерено"));l->addWidget(powerTrend,1);
        powerSteps=new StepPlot;powerSteps->configure(QStringLiteral("Маршрут воздействия питания"),QStringLiteral("В"),{24,27,35,19,27,37,27});l->addWidget(powerSteps);
        return page;
    }

    QWidget* buildYalk()
    {
        auto* page=new QWidget;auto* l=new QVBoxLayout(page);l->setContentsMargins(0,0,0,0);l->setSpacing(6);
        auto* head=new QHBoxLayout;auto* titles=new QVBoxLayout;titles->setSpacing(0);titles->addWidget(titleLabel(QStringLiteral("ЯЛК-96")));yalkPhaseTitle=subtitleLabel(QStringLiteral("Инициализация"));titles->addWidget(yalkPhaseTitle);head->addLayout(titles,1);yalkPhaseStrip=new QLabel;yalkPhaseStrip->setStyleSheet(QStringLiteral("background:#10151b;border:1px solid #27313c;border-radius:5px;color:#8b95a3;padding:7px 10px;"));head->addWidget(yalkPhaseStrip);l->addLayout(head);
        auto* current=new QHBoxLayout;current->addWidget(metricCard(QStringLiteral("Текущий адрес"),yalkChannel),1);current->addWidget(metricCard(QStringLiteral("Точка"),yalkPoint),1);current->addWidget(metricCard(QStringLiteral("U ИСД по В7"),yalkV7),1);l->addLayout(current);
        yalkOverview=new ChannelOverview;yalkOverview->setObjectName(QStringLiteral("yalkChannelHistogram"));yalkOverview->configure(80,QStringLiteral("В"));l->addWidget(yalkOverview,3);
        yalkStack=new QStackedWidget;
        {auto*w=new QWidget;auto*wl=new QHBoxLayout(w);wl->setContentsMargins(0,0,0,0);wl->addWidget(metricCard(QStringLiteral("Поток ЯЛК"),yalkStream),1);wl->addWidget(metricCard(QStringLiteral("Sequence"),yalkSequence),1);yalkStack->addWidget(w);}
        {auto*w=new QWidget;auto*wl=new QHBoxLayout(w);wl->setContentsMargins(0,0,0,0);wl->addWidget(metricCard(QStringLiteral("Ноль"),yalkCalZero),1);wl->addWidget(metricCard(QStringLiteral("Полная шкала"),yalkCalFull),1);yalkCalTrend=new TrendPlot;yalkCalTrend->configure(QStringLiteral("Калибровка"),QStringLiteral("В"),QStringLiteral("0"),QStringLiteral("6,2"));wl->addWidget(yalkCalTrend,2);yalkStack->addWidget(w);}
        {auto*w=new QWidget;auto*wl=new QHBoxLayout(w);wl->setContentsMargins(0,0,0,0);wl->addWidget(sectionLabel(QStringLiteral("Исходное состояние 80 входов")));yalkInitial=new StateGrid;yalkInitial->setMaximumHeight(130);wl->addWidget(yalkInitial,1);yalkStack->addWidget(w);}
        {auto*w=new QWidget;auto*wl=new QHBoxLayout(w);wl->setContentsMargins(0,0,0,0);wl->addWidget(subtitleLabel(QStringLiteral("Измеренные значения накладываются на непрерывный фоновый срез всех 80 каналов.")),1);yalkStack->addWidget(w);}
        {auto*w=new QWidget;auto*wl=new QHBoxLayout(w);wl->setContentsMargins(0,0,0,0);auto*m=new QVBoxLayout;m->addWidget(metricCard(QStringLiteral("Точка"),yalkDiscretePoint));m->addWidget(metricCard(QStringLiteral("Ожидается"),yalkExpected));m->addWidget(metricCard(QStringLiteral("Адрес"),yalkDiscreteChannel));wl->addLayout(m,1);yalkDiscrete=new StateGrid;yalkDiscrete->setMaximumHeight(150);wl->addWidget(yalkDiscrete,2);yalkDiscreteSteps=new StepPlot;yalkDiscreteSteps->configure(QStringLiteral("Дискретный порог"),QStringLiteral("В"),{0,0.9,2.5});yalkDiscreteSteps->setMaximumHeight(150);wl->addWidget(yalkDiscreteSteps,2);yalkStack->addWidget(w);}
        {auto*w=new QWidget;auto*wl=new QHBoxLayout(w);wl->setContentsMargins(0,0,0,0);wl->addWidget(metricCard(QStringLiteral("Канал"),overloadChannel),1);wl->addWidget(metricCard(QStringLiteral("Воздействие"),overloadPolarity),1);wl->addWidget(metricCard(QStringLiteral("Макс. |Δcode|"),overloadDelta),1);overloadSteps=new StepPlot;overloadSteps->configure(QStringLiteral("Перегрузка"),QStringLiteral("В"),{12,-12});overloadSteps->setMaximumHeight(145);wl->addWidget(overloadSteps,2);yalkStack->addWidget(w);}
        {auto*w=new QWidget;auto*wl=new QHBoxLayout(w);wl->setContentsMargins(0,0,0,0);wl->addWidget(metricCard(QStringLiteral("В7"),referenceV7),1);wl->addWidget(metricCard(QStringLiteral("ЯЛК"),referenceYalk),1);wl->addWidget(metricCard(QStringLiteral("|Δ|"),referenceDelta),1);referenceTrend=new TrendPlot;referenceTrend->configure(QStringLiteral("Эталон 6,2 В"),QStringLiteral("В"),QStringLiteral("В7"),QStringLiteral("ЯЛК"));referenceTrend->setMaximumHeight(145);wl->addWidget(referenceTrend,2);yalkStack->addWidget(w);}
        yalkStack->setMaximumHeight(165);l->addWidget(yalkStack,1);setYalkPhase(YalkPhase::Init);return page;
    }

    QWidget* buildYtp()
    {
        auto* page=new QWidget;auto* l=new QVBoxLayout(page);l->setContentsMargins(0,0,0,0);l->setSpacing(6);auto*head=new QHBoxLayout;auto*titles=new QVBoxLayout;titles->setSpacing(0);titles->addWidget(titleLabel(QStringLiteral("ЯТП")));ytpPhaseTitle=subtitleLabel(QStringLiteral("Инициализация"));titles->addWidget(ytpPhaseTitle);head->addLayout(titles,1);ytpOperatorBanner=new QLabel;ytpOperatorBanner->setStyleSheet(QStringLiteral("background:#38290d;color:#ffda83;border:1px solid #765d32;border-radius:5px;padding:7px 10px;font-weight:700;"));ytpOperatorBanner->setVisible(false);head->addWidget(ytpOperatorBanner);l->addLayout(head);
        auto* current=new QHBoxLayout;current->addWidget(metricCard(QStringLiteral("Канал"),ytpChannel),1);current->addWidget(metricCard(QStringLiteral("Р4831"),ytpReference),1);current->addWidget(metricCard(QStringLiteral("ЯТП"),ytpMeasured),1);l->addLayout(current);
        ytpOverview=new ChannelOverview;ytpOverview->setObjectName(QStringLiteral("ytpChannelHistogram"));ytpOverview->configure(30,QStringLiteral("Ом"));l->addWidget(ytpOverview,3);
        ytpStack=new QStackedWidget;
        {auto*w=new QWidget;auto*wl=new QHBoxLayout(w);wl->setContentsMargins(0,0,0,0);wl->addWidget(metricCard(QStringLiteral("Поток ЯТП"),ytpStream),1);wl->addWidget(metricCard(QStringLiteral("Endpoint"),ytpEndpoint),1);ytpStack->addWidget(w);}
        {auto*w=new QWidget;auto*wl=new QHBoxLayout(w);wl->setContentsMargins(0,0,0,0);wl->addWidget(metricCard(QStringLiteral("Калибровочный ноль"),ytpCalZero),1);wl->addWidget(metricCard(QStringLiteral("Калибровочная шкала"),ytpCalFull),1);ytpCalTrend=new TrendPlot;ytpCalTrend->configure(QStringLiteral("Калибровка ЯТП"),QStringLiteral("Ом"),QStringLiteral("ноль"),QStringLiteral("шкала"));ytpCalTrend->setMaximumHeight(145);wl->addWidget(ytpCalTrend,2);ytpStack->addWidget(w);}
        {auto*w=new QWidget;auto*wl=new QHBoxLayout(w);wl->setContentsMargins(0,0,0,0);ytpResistanceSteps=new StepPlot;ytpResistanceSteps->configure(QStringLiteral("Магазин сопротивления Р4831"),QStringLiteral("Ом"),{0,120,240});ytpResistanceSteps->setMaximumHeight(145);wl->addWidget(ytpResistanceSteps,1);ytpStack->addWidget(w);}
        ytpStack->setMaximumHeight(165);l->addWidget(ytpStack,1);setYtpPhase(YtpPhase::Init);return page;
    }

    QWidget* buildYvp()
    {
        auto* page=new QWidget;auto*l=new QVBoxLayout(page);l->setContentsMargins(0,0,0,0);l->setSpacing(7);l->addWidget(titleLabel(QStringLiteral("ЯВП-8")));yvpStatus=subtitleLabel(QStringLiteral("Ожидание этапа"));l->addWidget(yvpStatus);auto*m=new QHBoxLayout;m->addWidget(metricCard(QStringLiteral("Канал"),yvpChannel),1);m->addWidget(metricCard(QStringLiteral("Частота"),yvpFrequency),1);m->addWidget(metricCard(QStringLiteral("Kу"),yvpGain),1);m->addWidget(metricCard(QStringLiteral("Статус"),yvpResult),1);l->addLayout(m);yvpTrend=new TrendPlot;yvpTrend->configure(QStringLiteral("ЯВП-8 · измеренные точки"),QStringLiteral("В"),QStringLiteral("эталон"),QStringLiteral("ЯЛК"));l->addWidget(yvpTrend,1);auto*note=panel(true);auto*nl=new QVBoxLayout(note);nl->addWidget(subtitleLabel(QStringLiteral("Текущий backend может вернуть НЕПОЛНАЯ до включения генератора, если commissioning-карта ЯВП не подтверждена.")));l->addWidget(note);return page;
    }

    QWidget* buildFinish()
    {
        auto* page=new QWidget;auto*l=new QVBoxLayout(page);l->setContentsMargins(0,0,0,0);l->setSpacing(8);l->addWidget(titleLabel(QStringLiteral("Результат изделия")));auto*m=new QHBoxLayout;m->addWidget(metricCard(QStringLiteral("Питание"),finishPower),1);m->addWidget(metricCard(QStringLiteral("ЯЛК-96"),finishYalk),1);m->addWidget(metricCard(QStringLiteral("ЯТП"),finishYtp),1);m->addWidget(metricCard(QStringLiteral("ЯВП-8"),finishYvp),1);l->addLayout(m);auto*result=panel(true);auto*rl=new QVBoxLayout(result);finishVerdict=new QLabel(QStringLiteral("Формирование результата…"));finishVerdict->setStyleSheet(QStringLiteral("font-size:31px;font-weight:800;color:#d7a95b;"));rl->addWidget(finishVerdict);finishDetail=subtitleLabel(QString());rl->addWidget(finishDetail);rl->addStretch();auto*buttons=new QHBoxLayout;nextProduct=new QPushButton(QStringLiteral("Следующий УБСИ"));nextProduct->setObjectName(QStringLiteral("primary"));backToSession=new QPushButton(QStringLiteral("Вернуться в сессию"));reportButton=new QPushButton(QStringLiteral("Открыть отчёт"));reportButton->setEnabled(false);buttons->addWidget(nextProduct);buttons->addWidget(backToSession);buttons->addWidget(reportButton);buttons->addStretch();rl->addLayout(buttons);l->addWidget(result,1);QObject::connect(nextProduct,&QPushButton::clicked,q,[this]{selectNextWaiting();});QObject::connect(backToSession,&QPushButton::clicked,q,[this]{pages->setCurrentWidget(sessionPage);});QObject::connect(reportButton,&QPushButton::clicked,q,[this]{const QString path=!productionReportPath.isEmpty()?productionReportPath:tuReportPath;if(!path.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(path));});return page;
    }

    void addEquipment(const QString& code,const QString& name,const QString& connection,const QString& detail,bool operatorConfirmation)
    {
        if(equipmentRows.contains(code))return;const int row=equipmentTable->rowCount();equipmentTable->insertRow(row);equipmentTable->setItem(row,0,new QTableWidgetItem(name));equipmentTable->setItem(row,1,new QTableWidgetItem(connection));equipmentTable->setItem(row,2,new QTableWidgetItem(operatorConfirmation?QStringLiteral("Оператор"):QStringLiteral("Автоматически")));auto*state=new QTableWidgetItem(operatorConfirmation?QStringLiteral("ПО ТРЕБОВАНИЮ"):QStringLiteral("НЕ ПРОВЕРЕНО"));state->setForeground(operatorConfirmation?QColor("#69aee6"):QColor("#d7a95b"));equipmentTable->setItem(row,3,state);equipmentTable->setItem(row,4,new QTableWidgetItem(detail));equipmentRows.insert(code,{row,operatorConfirmation,operatorConfirmation,connection});
    }

    void addSerial()
    {
        const QString serial=serialEdit->text().trimmed();if(operatorEdit->text().trimmed().isEmpty()){QMessageBox::warning(q,QStringLiteral("Оператор"),QStringLiteral("Введите оператора."));return;}if(serial.isEmpty()){QMessageBox::warning(q,QStringLiteral("УБСИ"),QStringLiteral("Введите заводской номер."));return;}for(int row=0;row<productTable->rowCount();++row)if(productTable->item(row,0)->text().compare(serial,Qt::CaseInsensitive)==0){QMessageBox::warning(q,QStringLiteral("УБСИ"),QStringLiteral("Этот SN уже есть в сессии."));return;}const int row=productTable->rowCount();productTable->insertRow(row);productTable->setItem(row,0,new QTableWidgetItem(serial));productTable->setItem(row,1,new QTableWidgetItem(scopeDisplay()));auto*status=new QTableWidgetItem(QStringLiteral("ОЖИДАЕТ"));status->setForeground(QColor("#d7a95b"));productTable->setItem(row,2,status);productTable->selectRow(row);serialEdit->clear();
    }

    QString scopeDisplay() const
    {
        const QString scope=scopeCombo->currentData().toString();if(scope==QStringLiteral("ЯЛК-96"))return QStringLiteral("ЯЛК-96");if(scope==QStringLiteral("ЯТП"))return QStringLiteral("ЯТП");if(scope==QStringLiteral("ЯВП-8"))return QStringLiteral("ЯВП-8");return productionMode?QStringLiteral("Полная УБСИ"):QStringLiteral("УБСИ по ТУ");
    }

    void selectScope(int id)
    {
        const QString code=id==1?QStringLiteral("ЯЛК-96"):id==2?QStringLiteral("ЯТП"):id==3?QStringLiteral("ЯВП-8"):QStringLiteral("УБСИ ПО ТУ");const int index=scopeCombo->findData(code);if(index>=0)scopeCombo->setCurrentIndex(index);yalkSubPanel->setVisible(productionMode&&id==1);if(!productionMode&&id==3)return;scenarioInfo->setText(id==0?QStringLiteral("Полный маршрут: питание → ЯЛК-96 → ЯТП → ЯВП-8 → завершение."):id==1?QStringLiteral("ЯЛК-96: поканальные графики и состояния."):id==2?QStringLiteral("ЯТП: текущие колебания + 30 каналов + нижний график Р4831."):QStringLiteral("ЯВП-8: доступность определяется backend."));q->updateStartAvailability();
    }

    void enterWorkspace()
    {
        if(productionMode){if(productTable->currentRow()<0){QMessageBox::warning(q,QStringLiteral("Сессия"),QStringLiteral("Добавьте и выберите УБСИ."));return;}if(operatorEdit->text().trimmed().isEmpty()){QMessageBox::warning(q,QStringLiteral("Оператор"),QStringLiteral("Введите оператора."));return;}activeRow=productTable->currentRow();activeSerial=productTable->item(activeRow,0)->text();serialEdit->setText(activeSerial);activeOperator=operatorEdit->text().trimmed();productTable->item(activeRow,1)->setText(scopeDisplay());}else{activeSerial=serialEdit->text().trimmed();if(activeSerial.isEmpty()){QMessageBox::warning(q,QStringLiteral("Проверка по ТУ"),QStringLiteral("Введите заводской номер УБСИ."));return;}activeOperator=QStringLiteral("—");}
        workspaceTitle->setText(QStringLiteral("УБСИ %1").arg(activeSerial));workspaceSubtitle->setText(productionMode?QStringLiteral("%1 · производственная проверка").arg(scopeDisplay()):QStringLiteral("%1 · проверка по ТУ").arg(scopeDisplay()));operatorBadge->setText(productionMode?QStringLiteral("Оператор · %1").arg(activeOperator):QStringLiteral("ПРОВЕРКА ПО ТУ"));resetWorkspace();pages->setCurrentWidget(workspacePage);q->updateStartAvailability();
    }

    void resetWorkspace()
    {
        runInProgress=false;runClockTimer->stop();elapsed->setText(QStringLiteral("00:00:00"));progress->setValue(0);consumption->clear();powerTrend->clear();yalkCalTrend->clear();yalkOverview->clear();yalkDiscrete->clear();referenceTrend->clear();ytpCalTrend->clear();ytpOverview->clear();yvpTrend->clear();finishVerdict->setText(QStringLiteral("Формирование результата…"));finishVerdict->setStyleSheet(QStringLiteral("font-size:31px;font-weight:800;color:#d7a95b;"));reportButton->setEnabled(false);setTopStage(TopStage::Preparation);configureRouteVisibility();
    }

    void configureRouteVisibility()
    {
        for(auto*l:stageLabels)l->setVisible(true);const QString scope=scopeCombo->currentData().toString();if(scope==QStringLiteral("ЯЛК-96")){stageLabels[static_cast<int>(TopStage::Ytp)]->hide();stageLabels[static_cast<int>(TopStage::Yvp)]->hide();}else if(scope==QStringLiteral("ЯТП")){stageLabels[static_cast<int>(TopStage::Yalk)]->hide();stageLabels[static_cast<int>(TopStage::Yvp)]->hide();}else if(scope==QStringLiteral("ЯВП-8")){stageLabels[static_cast<int>(TopStage::Power)]->hide();stageLabels[static_cast<int>(TopStage::Yalk)]->hide();stageLabels[static_cast<int>(TopStage::Ytp)]->hide();}updateStageLabels();
    }

    void setTopStage(TopStage stage)
    {
        topStage=stage;workStack->setCurrentIndex(static_cast<int>(stage));const QStringList names={QStringLiteral("Подготовка"),QStringLiteral("Питание / потребление"),QStringLiteral("ЯЛК-96"),QStringLiteral("ЯТП"),QStringLiteral("ЯВП-8"),QStringLiteral("Завершение")};footerStage->setText(names[static_cast<int>(stage)]);updateStageLabels();if(stage==TopStage::Yalk)setYalkPhase(YalkPhase::Init);if(stage==TopStage::Ytp)setYtpPhase(YtpPhase::Init);
    }

    void setYalkPhase(YalkPhase phase)
    {
        yalkPhase=phase;yalkStack->setCurrentIndex(static_cast<int>(phase));const QStringList names={QStringLiteral("Инициализация"),QStringLiteral("Калибровка"),QStringLiteral("Исходное состояние"),QStringLiteral("Аналоговые каналы"),QStringLiteral("Дискретные пороги"),QStringLiteral("Перегрузка ±12 В"),QStringLiteral("Эталон 6,2 В")};yalkPhaseTitle->setText(names[static_cast<int>(phase)]);QStringList parts;for(int i=0;i<names.size();++i)parts<<(i<static_cast<int>(phase)?QStringLiteral("✓ %1").arg(names[i]):i==static_cast<int>(phase)?QStringLiteral("▶ %1").arg(names[i]):names[i]);yalkPhaseStrip->setText(parts.join(QStringLiteral("  ·  ")));
    }

    void setYtpPhase(YtpPhase phase){ytpPhase=phase;ytpStack->setCurrentIndex(static_cast<int>(phase));const QStringList names={QStringLiteral("Инициализация"),QStringLiteral("Калибровка"),QStringLiteral("30 каналов")};ytpPhaseTitle->setText(names[static_cast<int>(phase)]);}

    void updateStageLabels()
    {
        const QStringList names={QStringLiteral("Подготовка"),QStringLiteral("Питание / потребление"),QStringLiteral("ЯЛК-96"),QStringLiteral("ЯТП"),QStringLiteral("ЯВП-8"),QStringLiteral("Завершение")};int visibleNumber=0;for(int i=0;i<stageLabels.size();++i){auto*l=stageLabels[i];if(l->isHidden())continue;++visibleNumber;if(i<static_cast<int>(topStage)){l->setObjectName(QStringLiteral("stageDone"));l->setText(QStringLiteral("✓  %1. %2\nзавершено").arg(visibleNumber).arg(names[i]));}else if(i==static_cast<int>(topStage)){l->setObjectName(QStringLiteral("stageActive"));l->setText(QStringLiteral("▶  %1. %2\nтекущий этап").arg(visibleNumber).arg(names[i]));}else{l->setObjectName(QStringLiteral("stage"));l->setText(QStringLiteral("○  %1. %2").arg(visibleNumber).arg(names[i]));}l->style()->unpolish(l);l->style()->polish(l);}
    }

    int visibleRouteNumber(int stageIndex) const
    {
        int number=0;for(int i=0;i<=stageIndex&&i<stageLabels.size();++i)if(!stageLabels[i]->isHidden())++number;return number;
    }

    void mapNode(const QString& node)
    {
        if(node==QStringLiteral("readiness")||node==QStringLiteral("supply_range")||node==QStringLiteral("supply_status")){setTopStage(TopStage::Power);return;}
        if(node.startsWith(QStringLiteral("yalk_"))&&!node.startsWith(QStringLiteral("yvp_"))){setTopStage(TopStage::Yalk);if(node.contains(QStringLiteral("stream")))setYalkPhase(YalkPhase::Init);else if(node.contains(QStringLiteral("calibration")))setYalkPhase(YalkPhase::Calibration);else if(node.contains(QStringLiteral("initial")))setYalkPhase(YalkPhase::Initial);else if(node==QStringLiteral("yalk_channels"))setYalkPhase(YalkPhase::Analog);else if(node.contains(QStringLiteral("contact")))setYalkPhase(YalkPhase::Discrete);else if(node.contains(QStringLiteral("overload")))setYalkPhase(YalkPhase::Overload);else if(node.contains(QStringLiteral("reference")))setYalkPhase(YalkPhase::Reference);return;}
        if(node.startsWith(QStringLiteral("ytp_"))){setTopStage(TopStage::Ytp);if(node.contains(QStringLiteral("stream")))setYtpPhase(YtpPhase::Init);else if(node.contains(QStringLiteral("calibration")))setYtpPhase(YtpPhase::Calibration);else if(node.contains(QStringLiteral("channels")))setYtpPhase(YtpPhase::Channels);return;}
        if(node.startsWith(QStringLiteral("yvp_"))){setTopStage(TopStage::Yvp);return;}
        if(node==QStringLiteral("power_off")){setTopStage(TopStage::Finish);return;}
    }

    void updateProgressByStage()
    {
        int value=0;switch(topStage){case TopStage::Preparation:value=0;break;case TopStage::Power:value=15;break;case TopStage::Yalk:value=28+static_cast<int>(yalkPhase)*6;break;case TopStage::Ytp:value=70+static_cast<int>(ytpPhase)*5;break;case TopStage::Yvp:value=88;break;case TopStage::Finish:value=100;break;}progress->setValue(std::clamp(value,0,100));
    }

    bool equipmentReady() const
    {
        const auto info=scenarios.value(testCombo->currentData().toString());if(!info.available)return false;for(const auto& code:info.required){if(code==QStringLiteral("SCHEME")||code==QStringLiteral("R4831"))continue;const auto row=equipmentRows.constFind(code);if(row==equipmentRows.cend()||!row->ready)return false;}return true;
    }

    void selectNextWaiting()
    {
        if(!productionMode){pages->setCurrentWidget(sessionPage);return;}int next=-1;for(int i=activeRow+1;i<productTable->rowCount();++i)if(productTable->item(i,2)->text()==QStringLiteral("ОЖИДАЕТ")){next=i;break;}if(next<0)for(int i=0;i<productTable->rowCount();++i)if(productTable->item(i,2)->text()==QStringLiteral("ОЖИДАЕТ")){next=i;break;}if(next<0){pages->setCurrentWidget(sessionPage);return;}productTable->selectRow(next);activeRow=next;activeSerial=productTable->item(next,0)->text();serialEdit->setText(activeSerial);workspaceTitle->setText(QStringLiteral("УБСИ %1").arg(activeSerial));resetWorkspace();pages->setCurrentWidget(workspacePage);
    }

    void appendSessionRecord(const QString& status,const QString& runId={})
    {
        if(!productionMode)return;const QString rootPath=QCoreApplication::applicationDirPath()+QStringLiteral("/runs");QDir().mkpath(rootPath);QFile file(rootPath+QStringLiteral("/operator_sessions.csv"));const bool fresh=!file.exists();if(!file.open(QIODevice::WriteOnly|QIODevice::Append|QIODevice::Text))return;QTextStream out(&file);if(fresh)out<<"timestamp;operator;serial;scenario;status;run_id\n";out<<QDateTime::currentDateTime().toString(Qt::ISODateWithMs)<<';'<<activeOperator<<';'<<activeSerial<<';'<<testCombo->currentData().toString()<<';'<<status<<';'<<runId<<'\n';out.flush();file.close();if(status==QStringLiteral("START"))loadOperatorHistory();
    }

    void loadOperatorHistory()
    {
        operatorHistory->clear();
        operatorHistory->addItem(QStringLiteral("Выбрать из списка"));
        const QString path = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("runs/operator_sessions.csv"));
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        QStringList recordedNames;
        QTextStream input(&file);
        while (!input.atEnd()) {
            const QStringList fields = input.readLine().split(';');
            if (fields.size() < 2 || fields[0] == QStringLiteral("timestamp")) continue;
            const QString name = fields[1].trimmed();
            if (!name.isEmpty()) recordedNames << name;
        }
        QSet<QString> seen;
        QStringList names;
        for (auto iterator = recordedNames.crbegin(); iterator != recordedNames.crend(); ++iterator)
            if (!seen.contains(*iterator)) { seen.insert(*iterator); names << *iterator; }
        operatorHistory->addItems(names);
        if (operatorHistory->count() > 1)
            operatorEdit->setText(operatorHistory->itemText(1));
    }

    TestPage* q=nullptr;
    QVBoxLayout* root=nullptr;QStackedWidget* pages=nullptr;QWidget* bridge=nullptr;QWidget* sessionPage=nullptr;QWidget* workspacePage=nullptr;
    QComboBox* objectCombo=nullptr;QComboBox* scopeCombo=nullptr;QComboBox* testCombo=nullptr;QComboBox* modeCombo=nullptr;QCheckBox* partial=nullptr;QCheckBox* includeYvpCheck=nullptr;QCheckBox* includeOverload=nullptr;QCheckBox* includeSurvival=nullptr;
    QPushButton* home=nullptr;QLabel* sessionTitle=nullptr;QLabel* sessionSubtitle=nullptr;QLabel* workflowBadge=nullptr;QFrame* sessionDataPanel=nullptr;QLabel* operatorCaption=nullptr;QLineEdit* operatorEdit=nullptr;QComboBox* operatorHistory=nullptr;QLabel* serialCaption=nullptr;QLineEdit* serialEdit=nullptr;QPushButton* addProduct=nullptr;QFrame* productsPanel=nullptr;QTableWidget* productTable=nullptr;QButtonGroup* scopeGroup=nullptr;QHash<QString,QPushButton*> scopeButtons;QWidget* yalkSubPanel=nullptr;QButtonGroup* yalkSubGroup=nullptr;QLabel* scenarioInfo=nullptr;QPushButton* enterPreparation=nullptr;QFrame* engineerBridgePanel=nullptr;
    QPushButton* backSession=nullptr;QLabel* workspaceTitle=nullptr;QLabel* workspaceSubtitle=nullptr;QLabel* operatorBadge=nullptr;QPushButton* stopButton=nullptr;QVector<QLabel*> stageLabels;QStackedWidget* workStack=nullptr;QLabel* elapsed=nullptr;QLabel* footerStage=nullptr;QProgressBar* progress=nullptr;TrendPlot* consumption=nullptr;
    QLabel* preparationSubtitle=nullptr;QTableWidget* equipmentTable=nullptr;QLabel* readiness=nullptr;QPushButton* checkButton=nullptr;QPushButton* startButton=nullptr;
    QLabel* powerSet=nullptr;QLabel* powerActual=nullptr;QLabel* powerCurrent=nullptr;QLabel* powerHold=nullptr;TrendPlot* powerTrend=nullptr;StepPlot* powerSteps=nullptr;
    QLabel* yalkPhaseTitle=nullptr;QLabel* yalkPhaseStrip=nullptr;QStackedWidget* yalkStack=nullptr;YalkPhase yalkPhase=YalkPhase::Init;QLabel* yalkStream=nullptr;QLabel* yalkSequence=nullptr;QLabel* yalkCalZero=nullptr;QLabel* yalkCalFull=nullptr;TrendPlot* yalkCalTrend=nullptr;StateGrid* yalkInitial=nullptr;QLabel* yalkChannel=nullptr;QLabel* yalkPoint=nullptr;QLabel* yalkV7=nullptr;ChannelOverview* yalkOverview=nullptr;QLabel* yalkDiscretePoint=nullptr;QLabel* yalkExpected=nullptr;QLabel* yalkDiscreteChannel=nullptr;StateGrid* yalkDiscrete=nullptr;StepPlot* yalkDiscreteSteps=nullptr;QLabel* overloadChannel=nullptr;QLabel* overloadPolarity=nullptr;QLabel* overloadDelta=nullptr;StepPlot* overloadSteps=nullptr;QLabel* referenceV7=nullptr;QLabel* referenceYalk=nullptr;QLabel* referenceDelta=nullptr;TrendPlot* referenceTrend=nullptr;
    QLabel* ytpPhaseTitle=nullptr;QLabel* ytpOperatorBanner=nullptr;QStackedWidget* ytpStack=nullptr;YtpPhase ytpPhase=YtpPhase::Init;QLabel* ytpStream=nullptr;QLabel* ytpEndpoint=nullptr;QLabel* ytpCalZero=nullptr;QLabel* ytpCalFull=nullptr;TrendPlot* ytpCalTrend=nullptr;QLabel* ytpChannel=nullptr;QLabel* ytpReference=nullptr;QLabel* ytpMeasured=nullptr;ChannelOverview* ytpOverview=nullptr;StepPlot* ytpResistanceSteps=nullptr;
    QLabel* yvpStatus=nullptr;QLabel* yvpChannel=nullptr;QLabel* yvpFrequency=nullptr;QLabel* yvpGain=nullptr;QLabel* yvpResult=nullptr;TrendPlot* yvpTrend=nullptr;
    QLabel* finishPower=nullptr;QLabel* finishYalk=nullptr;QLabel* finishYtp=nullptr;QLabel* finishYvp=nullptr;QLabel* finishVerdict=nullptr;QLabel* finishDetail=nullptr;QPushButton* nextProduct=nullptr;QPushButton* backToSession=nullptr;QPushButton* reportButton=nullptr;
    EquipmentInvoke equipmentInvoke;QHash<QString,EquipmentRow> equipmentRows;QHash<QString,ScenarioInfo> scenarios;
    bool engineerMode=false;bool productionMode=false;bool runInProgress=false;QTimer* runClockTimer=nullptr;QElapsedTimer runClock;TopStage topStage=TopStage::Preparation;int activeRow=-1;QString activeSerial;QString activeOperator;QString tuReportPath;QString productionReportPath;
};
