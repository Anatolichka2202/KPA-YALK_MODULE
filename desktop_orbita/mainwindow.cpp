#include "mainwindow.h"
#include "channel_status.h"
#include "encoding_utils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QDateTime>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QIcon>
#include <QSettings>
#include <QPalette>
#include <QFileInfo>
#include <QSet>
#include <QEventLoop>
#include <QtConcurrent/QtConcurrentRun>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <regex>
#include <iomanip>
#include <functional>

#include "orbita_stand/report_writer.h"
#include "orbita_stand/ubsi_procedures.h"
#include "orbita_stand/telemetry_procedures.h"
#include "orbita_stand/catalog.h"
#include "scenario_yaml_editor.h"
#include "adapter_monitor_widget.h"
#include "equipment_control_widget.h"

#define ORBITA_VERSION "0.1.0-alpha"

// ----------------------------------------------------------------------------
//  Конструктор / Деструктор
// ----------------------------------------------------------------------------
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , orbita_(std::make_unique<orbita::Orbita>())
    , updateTimer_(new QTimer(this))
{
    // Порядок важен: тулбар создаёт startBtn_/actMain_ и пр., которые нужны
    // в setupDockWidgets() (там вызывается onWatchSetChanged) и в setMode().
    setupUi();
    setupToolBar();
    setupDockWidgets();
    applyTheme(false);

    // Нижняя строка статуса
    m_statusBarLabel = new QLabel(this);
    m_statusBarLabel->setStyleSheet("color:#7e8a98; font-family:'IBM Plex Mono'; font-size:11px;");
    statusBar()->addPermanentWidget(m_statusBarLabel, 1);
    statusBar()->setStyleSheet("QStatusBar{background:#0e1115; border-top:1px solid #232a33;}");
    m_statusBarLabel->setText("● сбор остановлен");

    // Теперь все элементы созданы — можно выставить начальный режим
    setMode(ModeTests);

    // Сначала читаем профиль: E20 подключён к разным входам в разных стойках.
    // Номер входа не должен быть скрыт в исходном коде приложения.
    initializeStandRuntime();
    unsigned e20Channel = 0;
    double e20RateKhz = 10000.0;
    try {
        if (const auto value = standProfile_.routes.find("orbita_e20_channel");
            value != standProfile_.routes.end()) {
            e20Channel = static_cast<unsigned>(std::stoul(value->second));
        }
        if (const auto value = standProfile_.routes.find("orbita_e20_rate_khz");
            value != standProfile_.routes.end()) {
            e20RateKhz = std::stod(value->second);
        }
    } catch (const std::exception& error) {
        log(QStringLiteral("Профиль E20 некорректен, используется вход 0: %1")
            .arg(QString::fromUtf8(error.what())));
        e20Channel = 0;
        e20RateKhz = 10000.0;
    }

    // Инициализация устройства
    try {
        orbita_->setDeviceE2010(e20Channel, e20RateKhz);
        e20Available_ = true;
        log(QStringLiteral("Устройство E20-10 найдено: вход %1, %2 кГц")
            .arg(e20Channel).arg(e20RateKhz));
    } catch (const std::exception& e) {
        e20Available_ = false;
        orbita_->setDeviceNone();
        log(QString("E20-10 недоступно (%1). Режим без устройства.")
                .arg(QString::fromLocal8Bit(e.what())));
    }

    // Таймер обновления
    connect(updateTimer_, &QTimer::timeout, this, &MainWindow::updateData);
    updateTimer_->start(100);

    log("Система инициализирована. Выберите объект и вид испытания.");
}

MainWindow::~MainWindow()
{
    if (scenarioEngine_) scenarioEngine_->requestStop();
    if (equipmentRegistry_) equipmentRegistry_->safeStopAll();
    if (scenarioWatcher_ && scenarioWatcher_->isRunning()) scenarioWatcher_->waitForFinished();
}

// ----------------------------------------------------------------------------
//  Инициализация UI
// ----------------------------------------------------------------------------
void MainWindow::setupUi()
{
    setWindowTitle(QString("Орбита — стендовый комплекс КТМА · %1").arg(ORBITA_VERSION));
    resize(1280, 800);

    // Центральный стек
    centralStack_ = new QStackedWidget;
    setCentralWidget(centralStack_);

    // --- Создаём страницы ---
    testPage_ = new TestPage;
    testPage_->setEquipmentInvoker([this](
        const std::string& capability, const std::string& operation,
        const std::map<std::string, std::string>& arguments) {
            if (!equipmentRegistry_ || !equipmentRegistry_->hasCapability(capability)) {
                throw std::runtime_error("Оборудование не готово: " + capability);
            }
            return equipmentRegistry_->invoke(capability, operation, arguments);
        });
    centralStack_->addWidget(testPage_);

    // Страница "Сбор"
    mainPage_ = new MainPage;
    centralStack_->addWidget(mainPage_);

    // Страница "Детальный просмотр"
    detailView_ = new DetailView;
    centralStack_->addWidget(detailView_);

    // Страница "Конфигуратор" (пока временная, будет заменена после открытия БД)
    QWidget* configPlaceholder = new QWidget;
    configPlaceholder->setStyleSheet("background: #14171c;");
    QVBoxLayout* cfgLayout = new QVBoxLayout(configPlaceholder);
    cfgLayout->addWidget(new QLabel("Загрузка конфигуратора..."));
    cfgLayout->setAlignment(Qt::AlignCenter);
    centralStack_->addWidget(configPlaceholder);

    // Страница "База параметров" (временная)
    QWidget* dbPlaceholder = new QWidget;
    dbPlaceholder->setStyleSheet("background: #14171c;");
    QVBoxLayout* dbLayout = new QVBoxLayout(dbPlaceholder);
    dbLayout->addWidget(new QLabel("Загрузка базы параметров..."));
    dbLayout->setAlignment(Qt::AlignCenter);
    centralStack_->addWidget(dbPlaceholder);
    // Начальный режим выставляется в конце конструктора — после setupToolBar()/setupDockWidgets(),
    // т.к. setMode() обращается к actMain_ и докам, которых здесь ещё нет.

    // --- Лог (нижняя панель) ---
    logEdit_ = new QTextEdit;
    logEdit_->setMaximumHeight(120);
    logEdit_->setReadOnly(true);
    logEdit_->setFont(QFont("Courier New", 9));
    logEdit_->setStyleSheet("QTextEdit { background: #0e1115; color: #aab4c0; border: 1px solid #2a313b; }");
}

void MainWindow::setupDockWidgets()
{
    // Открываем БД
    dbProvider_ = std::make_unique<MetadataService>(this);
    if (!dbProvider_->open()) {
        log("Ошибка открытия БД parameters.db");
        // Создаём заглушки для док-виджетов, чтобы приложение не падало
        QWidget* dummy = new QWidget;
        dummy->setStyleSheet("background: #14171c;");
        QLabel* errLabel = new QLabel("БД не загружена");
        errLabel->setStyleSheet("color: #cf5b52;");
        QVBoxLayout* l = new QVBoxLayout(dummy);
        l->addWidget(errLabel);
        l->setAlignment(Qt::AlignCenter);

        configDock_ = new QDockWidget("Конфигурации", this);
        configDock_->setWidget(dummy);
        addDockWidget(Qt::LeftDockWidgetArea, configDock_);

        paramDock_ = new QDockWidget("Библиотека параметров", this);
        paramDock_->setWidget(new QWidget);
        addDockWidget(Qt::RightDockWidgetArea, paramDock_);

        watchSetDock_ = new QDockWidget("Активный набор", this);
        watchSetDock_->setWidget(new QWidget);
        addDockWidget(Qt::RightDockWidgetArea, watchSetDock_);

        // Заменяем плейсхолдеры Конфиг и БД в центральном стеке на страницы с ошибкой
        QWidget* oldConfig = centralStack_->widget(ModeConfig);
        QWidget* oldDb = centralStack_->widget(ModeDb);
        centralStack_->removeWidget(oldConfig);
        centralStack_->removeWidget(oldDb);
        delete oldConfig;
        delete oldDb;

        // Страница "Конфиг" с сообщением об ошибке
        QWidget* configError = new QWidget;
        configError->setStyleSheet("background: #14171c;");
        QVBoxLayout* cfgErrLayout = new QVBoxLayout(configError);
        QLabel* cfgErrLabel = new QLabel("База параметров недоступна\n(parameters.db не найдена)");
        cfgErrLabel->setStyleSheet("color: #cf5b52; font-size: 13px;");
        cfgErrLayout->addWidget(cfgErrLabel);
        cfgErrLayout->setAlignment(Qt::AlignCenter);
        centralStack_->insertWidget(ModeConfig, configError);

        // Страница "БД" с сообщением об ошибке
        QWidget* dbError = new QWidget;
        dbError->setStyleSheet("background: #14171c;");
        QVBoxLayout* dbErrLayout = new QVBoxLayout(dbError);
        QLabel* dbErrLabel = new QLabel("База параметров недоступна\n(parameters.db не найдена)");
        dbErrLabel->setStyleSheet("color: #cf5b52; font-size: 13px;");
        dbErrLayout->addWidget(dbErrLabel);
        dbErrLayout->setAlignment(Qt::AlignCenter);
        centralStack_->insertWidget(ModeDb, dbError);

        // Продолжаем работу — приложение остаётся рабочим в части Сбор/Детально
        return;
    }

    // --- Создаём виджеты ---
    configDockWidget_ = new ConfigManagerWidget(dbProvider_.get(), this);
    paramDockWidget_ = new ParameterBrowser(dbProvider_.get(), this);
    watchSetDockWidget_ = new WatchSetWidget(dbProvider_.get(), this);

    // --- Док-виджеты ---
    configDock_ = new QDockWidget("Конфигурации", this);
    configDock_->setWidget(configDockWidget_);
    configDock_->setMinimumWidth(280);
    addDockWidget(Qt::LeftDockWidgetArea, configDock_);

    paramDock_ = new QDockWidget("Библиотека параметров", this);
    paramDock_->setWidget(paramDockWidget_);
    paramDock_->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea, paramDock_);

    watchSetDock_ = new QDockWidget("Активный набор", this);
    watchSetDock_->setWidget(watchSetDockWidget_);
    watchSetDock_->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea, watchSetDock_);

    // --- Лог как док-виджет (уже создан, но добавим как док, чтобы его можно было скрыть) ---
    logDock_ = new QDockWidget("Лог", this);
    logDock_->setWidget(logEdit_);
    logDock_->setMaximumHeight(150);
    addDockWidget(Qt::BottomDockWidgetArea, logDock_);

    // --- Подключаем сигналы ---
    connect(configDockWidget_, &ConfigManagerWidget::applyConfigRequested,
            this, &MainWindow::applyConfiguration);
    connect(configDockWidget_, &ConfigManagerWidget::refreshMetadataRequested,
            this, &MainWindow::onRefreshMetadata);

    connect(paramDockWidget_, &ParameterBrowser::parametersSelected,
            [this](const QList<QString>& addresses, const QList<QString>& names) {
                std::vector<orbita::ChannelSpec> specs;
                for (int i = 0; i < addresses.size(); ++i) {
                    std::string norm = encoding::normalizeAddress(addresses[i].toStdString());
                    if (norm.empty()) continue;
                    // Категорию возьмём из БД
                    QString cat = dbProvider_ ? dbProvider_->getCategory(addresses[i]) : QString();
                    specs.push_back({norm, names[i].toStdString(), cat.toStdString()});
                }
                if (watchSetDockWidget_)
                    watchSetDockWidget_->addParams(specs);
            });

    connect(paramDockWidget_, &ParameterBrowser::overrideTolerance,
            [this](QString address, double lo, double nominal, double hi) {
                toleranceResolver_.setOverride(address, chstatus::Tolerance{lo, nominal, hi, true});
            });
    connect(paramDockWidget_, &ParameterBrowser::toleranceSavedToDb,
            [this](QString address) { toleranceResolver_.clearOverride(address); });

    connect(watchSetDockWidget_, &WatchSetWidget::watchSetChanged,
            this, &MainWindow::onWatchSetChanged);
    connect(watchSetDockWidget_, &WatchSetWidget::configSaved,
            [this]() { if (configDockWidget_) configDockWidget_->refreshFileList(); });

    // --- Заменяем страницы Конфиг и БД в центральном стеке на реальные виджеты ---
    // Удаляем временные placeholder'ы
    QWidget* oldConfig = centralStack_->widget(ModeConfig);
    QWidget* oldDb = centralStack_->widget(ModeDb);
    centralStack_->removeWidget(oldConfig);
    centralStack_->removeWidget(oldDb);
    delete oldConfig;
    delete oldDb;

    // Создаём новые страницы с реальными виджетами
    // Для Конфига используем тот же виджет, что и в доке, но он не может быть одновременно в двух местах.
    // Поэтому создадим отдельный экземпляр для страницы.
    configPage_ = new ConfigManagerWidget(dbProvider_.get(), this);
    centralStack_->insertWidget(ModeConfig, configPage_);

    dbPage_ = new ParameterBrowser(dbProvider_.get(), this);
    centralStack_->insertWidget(ModeDb, dbPage_);

    // Подключаем сигналы страниц (если нужны)
    connect(configPage_, &ConfigManagerWidget::applyConfigRequested,
            this, &MainWindow::applyConfiguration);
    connect(configPage_, &ConfigManagerWidget::refreshMetadataRequested,
            this, &MainWindow::onRefreshMetadata);

    connect(dbPage_, &ParameterBrowser::parametersSelected,
            [this](const QList<QString>& addresses, const QList<QString>& names) {
                // Аналогично добавлению из док-виджета
                std::vector<orbita::ChannelSpec> specs;
                for (int i = 0; i < addresses.size(); ++i) {
                    std::string norm = encoding::normalizeAddress(addresses[i].toStdString());
                    if (norm.empty()) continue;
                    QString cat = dbProvider_ ? dbProvider_->getCategory(addresses[i]) : QString();
                    specs.push_back({norm, names[i].toStdString(), cat.toStdString()});
                }
                if (watchSetDockWidget_)
                    watchSetDockWidget_->addParams(specs);
            });

    connect(dbPage_, &ParameterBrowser::overrideTolerance,
            [this](QString address, double lo, double nominal, double hi) {
                toleranceResolver_.setOverride(address, chstatus::Tolerance{lo, nominal, hi, true});
            });
    connect(dbPage_, &ParameterBrowser::toleranceSavedToDb,
            [this](QString address) { toleranceResolver_.clearOverride(address); });

    // Передаём БД в MainPage и DetailView
    mainPage_->setMetadataService(dbProvider_.get());
    detailView_->setMetadataService(dbProvider_.get());

    // Инициализируем ToleranceResolver и передаём в MainPage и DetailView
    toleranceResolver_.setDb(dbProvider_.get());
    {
        int tolN = toleranceResolver_.loadConfigFile(
            QApplication::applicationDirPath() + "/address/tolerances.tol");
        if (tolN < 0)
            log("tolerances.tol не найден — допуски только из БД");
        else
            log(QString("Загружено %1 допусков из tolerances.tol").arg(tolN));
    }
    mainPage_->setToleranceResolver(&toleranceResolver_);
    detailView_->setToleranceResolver(&toleranceResolver_);

    // Доки скрыты по умолчанию — экран Сбор чистый. Вызвать можно через меню «Вид».
    configDock_->hide();
    paramDock_->hide();
    watchSetDock_->hide();
    logDock_->hide();

    QMenu* viewMenu = menuBar()->addMenu("Вид");
    viewMenu->addAction(watchSetDock_->toggleViewAction());
    viewMenu->addAction(configDock_->toggleViewAction());
    viewMenu->addAction(paramDock_->toggleViewAction());
    viewMenu->addAction(logDock_->toggleViewAction());

    // Устанавливаем начальный набор (пустой)
    onWatchSetChanged({});
}

void MainWindow::setupToolBar()
{
    QToolBar* toolbar = addToolBar("Управление");
    mainToolbar_ = toolbar;
    toolbar->setMovable(false);
    toolbar->setStyleSheet("QToolBar { spacing: 4px; }");

    accessModeCombo_ = new QComboBox;
    accessModeCombo_->setObjectName(QStringLiteral("accessMode"));
    accessModeCombo_->addItem(QStringLiteral("Оператор"), false);
    accessModeCombo_->addItem(QStringLiteral("Инженер"), true);
    accessModeCombo_->addItem(QStringLiteral("Администратор"), true);
    accessModeCombo_->setToolTip(QStringLiteral(
        "Инженерный режим открывает сырые параметры, конфигурацию и редактор сценариев"));
    toolbar->addWidget(accessModeCombo_);
    toolbar->addSeparator();

    // --- Группа переключения режимов ---
    QActionGroup* modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);

    actTests_  = toolbar->addAction(QIcon(":/icons/scenario.svg"), "Испытания");
    actMain_   = toolbar->addAction(QIcon(":/icons/collect.svg"),  "Мониторинг");
    actDetail_ = toolbar->addAction(QIcon(":/icons/detail.svg"),   "Канал");
    actConfig_ = toolbar->addAction(QIcon(":/icons/config.svg"),   "Конфигурация");
    actDb_     = toolbar->addAction(QIcon(":/icons/database.svg"), "Параметры");

    actTests_->setCheckable(true);
    actMain_->setCheckable(true);
    actDetail_->setCheckable(true);
    actConfig_->setCheckable(true);
    actDb_->setCheckable(true);
    actTests_->setChecked(true);

    modeGroup->addAction(actTests_);
    modeGroup->addAction(actMain_);
    modeGroup->addAction(actDetail_);
    modeGroup->addAction(actConfig_);
    modeGroup->addAction(actDb_);

    connect(actTests_, &QAction::triggered, [this]() { setMode(ModeTests); });
    connect(actMain_, &QAction::triggered, [this]() { setMode(ModeMain); });
    connect(actDetail_, &QAction::triggered, [this]() { setMode(ModeDetail); });
    connect(actConfig_, &QAction::triggered, [this]() { setMode(ModeConfig); });
    connect(actDb_, &QAction::triggered, [this]() { setMode(ModeDb); });

    toolbar->addSeparator();

    // --- Быстрый выбор конфига ---
    configCombo_ = new QComboBox;
    configCombo_->setFixedWidth(190);
    configCombo_->setStyleSheet(
        "QComboBox { background:#1B1F26; color:#aab4c0; border:1px solid #2a313b;"
        "            border-radius:3px; padding:2px 6px; }"
        "QComboBox::drop-down { border:none; width:18px; }"
        "QComboBox QAbstractItemView { background:#1B1F26; color:#dfe6ee; selection-background-color:#2a3345; }");
    configCombo_->setToolTip("Быстрый выбор конфигурации (применяется сразу)");
    refreshConfigCombo();
    toolbar->addWidget(configCombo_);

    toolbar->addSeparator();

    // --- Кнопки Старт/Стоп ---
    startBtn_ = new QPushButton(QIcon(":/icons/play.svg"), " Старт");
    stopBtn_  = new QPushButton(QIcon(":/icons/stop.svg"),  " Стоп");
    startBtn_->setEnabled(false);
    stopBtn_->setEnabled(false);
    startBtn_->setFixedWidth(80);
    stopBtn_->setFixedWidth(80);
    toolbar->addWidget(startBtn_);
    toolbar->addWidget(stopBtn_);

    toolbar->addSeparator();

    // --- Запись ---
    recordBtn_ = new QPushButton(QIcon(":/icons/record.svg"), " Запись");
    recordBtn_->setEnabled(false);
    recordBtn_->setFixedWidth(90);
    toolbar->addWidget(recordBtn_);

    recordingLabel_ = new QLabel("—");
    recordingLabel_->setMinimumWidth(150);
    recordingLabel_->setStyleSheet("color: #7e8a98; font-size: 11px;");
    toolbar->addWidget(recordingLabel_);

    toolbar->addSeparator();

    // --- Инверт ---
    invertCheck_ = new QCheckBox("Инверт.");
    invertCheck_->setStyleSheet("QCheckBox { color: #aab4c0; }");
    toolbar->addWidget(invertCheck_);

    toolbar->addSeparator();

    // --- МТВ и статус ---
    mtvLabel_ = new QLabel("--:--:--");
    mtvLabel_->setFont(QFont("Courier New", 14, QFont::Bold));
    mtvLabel_->setStyleSheet("color: #dfe6ee;");
    toolbar->addWidget(mtvLabel_);

    statusLabel_ = new QLabel("● Сбор остановлен");
    statusLabel_->setStyleSheet("color: #d99a4a; font-weight: 500;");
    toolbar->addWidget(statusLabel_);

    toolbar->addSeparator();

    // --- Ошибки ---
    errPhraseLabel_ = new QLabel("ошибки фраз: 0%");
    errGroupLabel_ = new QLabel("групп: 0%");
    errPhraseLabel_->setStyleSheet("color: #8b95a3; font-size: 11px; font-family: 'IBM Plex Mono';");
    errGroupLabel_->setStyleSheet("color: #8b95a3; font-size: 11px; font-family: 'IBM Plex Mono';");
    toolbar->addWidget(errPhraseLabel_);
    toolbar->addWidget(errGroupLabel_);

    // --- Подключаем сигналы кнопок ---
    connect(startBtn_, &QPushButton::clicked, this, &MainWindow::onStart);
    connect(stopBtn_, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(recordBtn_, &QPushButton::clicked, this, &MainWindow::onToggleRecording);
    connect(invertCheck_, &QCheckBox::toggled,
            [this](bool v){ orbita_->setInvertSignal(v); });

    // --- Подключаем сигналы DetailView ---
    connect(detailView_, &DetailView::backToMain, [this]() { setMode(ModeMain); });
    connect(detailView_, &DetailView::navigateChannel, this, &MainWindow::onNavigateChannel);

    // --- Подключаем сигналы MainPage ---
    connect(mainPage_, &MainPage::channelSelected, this, &MainWindow::onChannelSelected);
    connect(mainPage_, &MainPage::channelDoubleClicked, this, &MainWindow::onChannelDoubleClicked);

    // --- Действия для док-виджетов (показывать/скрывать) ---
    // Добавим переключатели видимости доков в меню View (можно позже)

    // Ранний редактор оставлен как диагностический инструмент, но убран из
    // основного операторского потока.
    toolsMenu_ = menuBar()->addMenu("Инструменты");
    actScenario_ = toolsMenu_->addAction("Редактор сценариев (экспериментальный)");
    connect(actScenario_, &QAction::triggered, this, &MainWindow::onOpenScenario);
    auto* catalogAction = toolsMenu_->addAction("Каталог параметров и адресов");
    connect(catalogAction, &QAction::triggered, this, &MainWindow::onOpenCatalog);
    auto* profileAction = toolsMenu_->addAction("Профиль стенда и маршруты");
    connect(profileAction, &QAction::triggered, this, &MainWindow::onOpenStandProfile);
    auto* adapterMonitorAction = toolsMenu_->addAction("ЯЛК / УЛК: живой поток адаптера");
    adapterMonitorAction->setToolTip(QStringLiteral(
        "Пассивно показывает UDP-кадры адаптера. Никаких команд в стенд не отправляет."));
    connect(adapterMonitorAction, &QAction::triggered, this, [this] {
        if (!equipmentRegistry_ || !equipmentRegistry_->hasCapability("ulk.parameter_source")) {
            QMessageBox::information(this, QStringLiteral("Адаптер ЯЛК / УЛК"),
                QStringLiteral("Сначала нажмите «Проверить оборудование» на странице испытаний.\n"
                               "Это загрузит плагин адаптера и выполнит безопасную проверку потока."));
            return;
        }
        auto* monitor = new AdapterMonitorWidget([this] {
            return equipmentRegistry_->invoke("ulk.parameter_source", "read_frame", {});
        }, this);
        monitor->show();
    });
    auto* equipmentControlAction = toolsMenu_->addAction(
        QStringLiteral("Ручное управление ИСД и адаптером"));
    connect(equipmentControlAction, &QAction::triggered, this, [this] {
        if (!equipmentRegistry_
            || !equipmentRegistry_->hasCapability("ulk.parameter_source")
            || !equipmentRegistry_->hasCapability("stand.switch_matrix")) {
            QMessageBox::information(this, QStringLiteral("Ручное управление"),
                QStringLiteral("Сначала нажмите «Проверить оборудование» на странице испытаний."));
            return;
        }
        auto* control = new EquipmentControlWidget([this](
            const std::string& capability, const std::string& operation,
            const std::map<std::string, std::string>& arguments) {
                return equipmentRegistry_->invoke(capability, operation, arguments);
            }, this);
        control->show();
    });

    connect(testPage_, &TestPage::equipmentCheckRequested,
            this, &MainWindow::onCheckTestEquipment);
    connect(testPage_, &TestPage::runRequested,
            this, &MainWindow::onRunScenario);
    connect(testPage_, &TestPage::stopRequested,
            this, &MainWindow::onStopScenario);
    connect(accessModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) { setEngineerMode(index > 0); });
    auto* engineerShortcut = new QAction(this);
    engineerShortcut->setShortcut(QKeySequence(Qt::Key_F12));
    engineerShortcut->setShortcutContext(Qt::ApplicationShortcut);
    addAction(engineerShortcut);
    connect(engineerShortcut, &QAction::triggered, this, [this] {
        accessModeCombo_->setCurrentIndex(accessModeCombo_->currentIndex() == 0 ? 1 : 0);
    });

    themeAction_ = menuBar()->addAction(QStringLiteral("Светлая тема"));
    themeAction_->setCheckable(true);
    themeAction_->setToolTip(QStringLiteral("Переключить тёмную операторскую и светлую инженерскую тему"));
    connect(themeAction_, &QAction::toggled, this, [this](bool light) { applyTheme(light); });

    // --- Подключаем config combo ---
    connect(configCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        if (i > 0) applyConfiguration(configCombo_->itemText(i));
    });
    setEngineerMode(false);
}

void MainWindow::applyTheme(bool light)
{
    lightTheme_ = light;
    QPalette palette = QApplication::palette();
    if (light) {
        palette.setColor(QPalette::Window, QColor("#f3f5f7"));
        palette.setColor(QPalette::Base, QColor("#ffffff"));
        palette.setColor(QPalette::AlternateBase, QColor("#e9edf2"));
        palette.setColor(QPalette::Text, QColor("#1b2430"));
        palette.setColor(QPalette::WindowText, QColor("#1b2430"));
        palette.setColor(QPalette::Button, QColor("#e6ebf0"));
        palette.setColor(QPalette::ButtonText, QColor("#1b2430"));
        palette.setColor(QPalette::Highlight, QColor("#2f6fed"));
        palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        if (themeAction_) themeAction_->setText(QStringLiteral("Тёмная тема"));
    } else {
        palette.setColor(QPalette::Window, QColor("#14171c"));
        palette.setColor(QPalette::Base, QColor("#0e1115"));
        palette.setColor(QPalette::AlternateBase, QColor("#1b1f26"));
        palette.setColor(QPalette::Text, QColor("#dfe6ee"));
        palette.setColor(QPalette::WindowText, QColor("#dfe6ee"));
        palette.setColor(QPalette::Button, QColor("#202631"));
        palette.setColor(QPalette::ButtonText, QColor("#dfe6ee"));
        palette.setColor(QPalette::Highlight, QColor("#2f6fed"));
        palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        if (themeAction_) themeAction_->setText(QStringLiteral("Светлая тема"));
    }
    QApplication::setPalette(palette);
}

void MainWindow::toggleTheme()
{
    applyTheme(!lightTheme_);
}

// ----------------------------------------------------------------------------
//  Сценарий проверки
// ----------------------------------------------------------------------------
void MainWindow::onOpenScenario()
{
    const QString code = testPage_->currentScenarioCode();
    const QString path = scenarioPaths_.value(code);
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, QStringLiteral("Сценарий"),
            QStringLiteral("Файл выбранного сценария не найден.\nРежим: %1\nПуть: %2")
                .arg(code, path.isEmpty() ? QStringLiteral("не определён") : path));
        return;
    }
    ScenarioYamlEditor editor(path, this);
    editor.exec();
}

void MainWindow::onOpenCatalog()
{
    const QString path = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("catalog/catalog.yaml"));
    ScenarioYamlEditor editor(path, this);
    editor.exec();
}

void MainWindow::onOpenStandProfile()
{
    const QString path = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("profiles/stand_ktma.yaml"));
    ScenarioYamlEditor editor(path, this);
    editor.exec();
}

void MainWindow::initializeStandRuntime()
{
    try {
        const QDir root(QCoreApplication::applicationDirPath());
        standProfile_ = orbita::stand::loadStandProfile(
            root.filePath("profiles/stand_ktma.yaml").toStdString());
        const auto catalog = orbita::stand::importCatalogYaml(
            root.filePath("catalog/catalog.yaml").toStdString(),
            root.filePath("parameters.db").toStdString());
        equipmentPlugins_ = std::make_unique<orbita::stand::EquipmentPluginManager>();
        equipmentPlugins_->loadDirectory(root.filePath("plugins").toStdString());
        equipmentRegistry_ = std::make_unique<orbita::stand::EquipmentRegistry>();
        scenarioEngine_ = std::make_unique<orbita::stand::ScenarioEngine>();
        orbita::stand::registerUbsiProcedures(*scenarioEngine_);
        orbita::stand::registerTelemetryProcedures(*scenarioEngine_);
        const QHash<QString, QString> equipmentForCapability = {
            {"ulk.parameter_source", "RS485"}, {"stand.switch_matrix", "ISD"},
            {"orbita.parameter_source", "E20"},
            {"measure.reference_voltage", "V7"}, {"measure.dc_current", "V7"},
            {"measure.reference_ac_voltage", "V7"}, {"measure.reference_frequency", "V7"},
            {"power.dc_supply", "AKIP"}, {"signal.generator", "RIGOL"},
            {"operator.manual_input", "R4831"}, {"measure.waveform", "SCOPE"}};
        scenarios_.clear();
        scenarioPaths_.clear();
        testPage_->setScenarioInfo("UBSI_NORMAL_5_6", false, false, {},
            QStringLiteral("Сценарий УБСИ не загружен"));
        testPage_->setScenarioInfo("YALK_FULL_5_6", false, false, {},
            QStringLiteral("Сценарий ЯЛК не загружен"));
        testPage_->setScenarioInfo("YALK_CONTACT_THRESHOLDS", false, false, {},
            QStringLiteral("Опциональный сценарий контактных порогов не загружен"));
        testPage_->setScenarioInfo("YTP_FULL_5_6", false, false, {},
            QStringLiteral("Сценарий ЯТП не загружен"));
        testPage_->setScenarioInfo("YTP_120_CHECK", false, true, {},
            QStringLiteral("Сценарий контроля ЯТП при 120 Ом не загружен"));
        testPage_->setScenarioInfo("ULK_COMBINED_CHECK", false, false, {},
            QStringLiteral("Совмещённый сценарий ЯЛК + ЯТП не загружен"));
        testPage_->setScenarioInfo("BSI_DIAGNOSTIC", false, true, {},
            QStringLiteral("Диагностический сценарий БСИ не загружен"));
        const QDir scenarioDirectory(root.filePath("scenarios"));
        for (const auto& file : scenarioDirectory.entryInfoList(
                 {QStringLiteral("*.yaml")}, QDir::Files, QDir::Name)) {
            const auto scenario = orbita::stand::loadScenarioYaml(
                file.absoluteFilePath().toUtf8().toStdString());
            QString code;
            bool diagnostic = false;
            if (scenario.id == "ubsi.468157.002.tu5_6.normal") {
                code = QStringLiteral("UBSI_NORMAL_5_6");
            } else if (scenario.id == "ubsi.468157.002.yalk.tu5_6") {
                code = QStringLiteral("YALK_FULL_5_6");
            } else if (scenario.id == "ubsi.468157.002.yalk.contact_thresholds") {
                code = QStringLiteral("YALK_CONTACT_THRESHOLDS");
            } else if (scenario.id == "ubsi.468157.002.ytp.tu5_6") {
                code = QStringLiteral("YTP_FULL_5_6");
            } else if (scenario.id == "ubsi.468157.002.ytp.120ohm.check") {
                code = QStringLiteral("YTP_120_CHECK");
                diagnostic = true;
            } else if (scenario.id == "ubsi.468157.002.ulk.combined.check") {
                code = QStringLiteral("ULK_COMBINED_CHECK");
            } else if (scenario.id == "bsi.468157.003.telemetry.diagnostic") {
                code = QStringLiteral("BSI_DIAGNOSTIC");
                diagnostic = true;
            } else {
                continue;
            }
            QStringList errors;
            if (catalog.version != scenario.catalogVersion) {
                errors << QStringLiteral("Версия каталога %1 не совпадает со сценарием %2")
                    .arg(QString::fromStdString(catalog.version),
                         QString::fromStdString(scenario.catalogVersion));
            }
            for (const auto& error : scenarioEngine_->validate(scenario)) {
                errors << QString::fromStdString(error);
            }
            QSet<QString> requiredRoles;
            if (!diagnostic) requiredRoles.insert(QStringLiteral("SCHEME"));
            std::function<void(const orbita::stand::ScenarioNode&)> collectRequired;
            collectRequired = [&](const orbita::stand::ScenarioNode& node) {
                for (const auto& capability : node.requiredCapabilities) {
                    const QString role = equipmentForCapability.value(
                        QString::fromStdString(capability));
                    if (!role.isEmpty()) requiredRoles.insert(role);
                }
                for (const auto& child : node.children) collectRequired(child);
            };
            for (const auto& step : scenario.steps) collectRequired(step);
            QStringList required = requiredRoles.values();
            required.sort();
            const bool available = errors.isEmpty();
            const QString detail = available
                ? QStringLiteral("Загружен сценарий «%1», версия %2; профиль %3")
                    .arg(QString::fromStdString(scenario.title),
                         QString::fromStdString(scenario.version),
                         QString::fromStdString(standProfile_.version))
                : errors.join(QStringLiteral("; "));
            if (available) {
                scenarios_.insert(code, scenario);
                scenarioPaths_.insert(code, file.absoluteFilePath());
            }
            testPage_->setScenarioInfo(code, available, diagnostic, required, detail);
        }
        if (!scenarios_.contains(QStringLiteral("YALK_FULL_5_6"))
            || !scenarios_.contains(QStringLiteral("YALK_CONTACT_THRESHOLDS"))
            || !scenarios_.contains(QStringLiteral("YTP_FULL_5_6"))
            || !scenarios_.contains(QStringLiteral("YTP_120_CHECK"))
            || !scenarios_.contains(QStringLiteral("ULK_COMBINED_CHECK"))) {
            throw std::runtime_error("Сценарии поставки ЯЛК/ЯТП загружены не полностью");
        }
        QDir().mkpath(root.filePath("runs"));
        runStore_ = std::make_unique<orbita::stand::RunStore>(
            root.filePath("runs/runs.db").toStdString());
        scenarioWatcher_ = new QFutureWatcher<orbita::stand::ScenarioRunResult>(this);
        connect(scenarioWatcher_, &QFutureWatcherBase::finished, this, [this]() {
            const auto result = scenarioWatcher_->result();
            QString tuReportPath;
            QString productionReportPath;
            try {
                runStore_->save(result);
                const QDir root(QCoreApplication::applicationDirPath());
                const QString reportDir = root.filePath("runs/" + QString::fromStdString(result.runId));
                const auto paths = orbita::stand::writeHtmlCsvReport(result, reportDir.toStdString());
                tuReportPath = QString::fromStdString(paths.tuHtml);
                productionReportPath = QString::fromStdString(paths.productionHtml);
                log(QStringLiteral("Краткий протокол ТУ: %1").arg(tuReportPath));
                log(QStringLiteral("Производственная ведомость: %1").arg(productionReportPath));
            } catch (const std::exception& error) {
                log(QStringLiteral("Не удалось сохранить результат: %1").arg(QString::fromUtf8(error.what())));
            }
            testPage_->setRunResult(result, tuReportPath, productionReportPath);
        });
        standRuntimeReady_ = true;
        log(QStringLiteral("Плагины оборудования: загружено %1").arg(equipmentPlugins_->plugins().size()));
    } catch (const std::exception& error) {
        standRuntimeReady_ = false;
        const QString detail = QStringLiteral("Стендовый движок не готов: %1")
            .arg(QString::fromUtf8(error.what()));
        testPage_->setScenarioInfo("UBSI_NORMAL_5_6", false, false, {}, detail);
        log(detail);
    }
}

void MainWindow::onCheckTestEquipment()
{
    if (!standRuntimeReady_) {
        initializeStandRuntime();
        if (!standRuntimeReady_) return;
    }

    const QHash<QString, QString> uiCodes = {
        {"orbita.ktma_adapter_udp", "RS485"}, {"orbita.isd_http", "ISD"},
        {"orbita.v7_visa", "V7"}, {"orbita.akip_1160_pair", "AKIP"},
        {"orbita.rigol_generator", "RIGOL"},
        {"orbita.rigol_dho8xx", "SCOPE"}};
    const QSet<QString> deliveryEquipment = {"RS485", "ISD", "V7", "AKIP"};
    const QSet<QString> activeCapabilities = {
        "stand.switch_matrix", "signal.generator"};

    equipmentRegistry_->safeStopAll();
    equipmentRegistry_->clear();
    equipmentDevices_.clear();
    const std::string catalogDatabase = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("parameters.db")).toUtf8().toStdString();
    equipmentRegistry_->bind("catalog.parameter_resolver",
        [catalogDatabase](const std::string& operation,
                          const std::map<std::string, std::string>& arguments) {
            if (operation != "resolve") {
                throw std::invalid_argument("Неизвестная операция каталога: " + operation);
            }
            const auto required = [&arguments](const char* key) -> const std::string& {
                const auto value = arguments.find(key);
                if (value == arguments.end() || value->second.empty()) {
                    throw std::invalid_argument(std::string("Каталогу требуется ") + key);
                }
                return value->second;
            };
            const auto binding = orbita::stand::resolveCatalogParameterBinding(
                catalogDatabase, required("block_type"), required("parameter_group"),
                static_cast<unsigned>(std::stoul(required("channel_index"))));
            std::ostringstream response;
            response << "source=" << binding.source << '\n'
                     << "locator_type=" << binding.locatorType << '\n'
                     << "locator=" << binding.locator << '\n'
                     << "stream_id=" << binding.streamId << '\n'
                     << "word_index=" << binding.wordIndex << '\n'
                     << "mask=" << binding.mask << '\n'
                     << "shift=" << binding.shift << '\n'
                     << "mode=" << binding.mode << '\n'
                     << "conversion_id=" << binding.conversionId << '\n'
                     << "stimulus_route=" << binding.stimulusRoute << '\n'
                     << "stimulus_offset=" << binding.stimulusOffset << '\n'
                     << "confirmed=" << (binding.confirmed ? "true" : "false") << '\n';
            return response.str();
        });
    equipmentRegistry_->bind("operator.manual_input",
        [this](const std::string& operation,
               const std::map<std::string, std::string>& arguments) {
            if (operation != "confirm_value") {
                throw std::invalid_argument("Неизвестная ручная операция: " + operation);
            }
            bool accepted = false;
            double actual = 0.0;
            QString title = QStringLiteral("Ручная операция");
            QString prompt = QStringLiteral("Введите фактическое значение");
            if (const auto found = arguments.find("title"); found != arguments.end())
                title = QString::fromStdString(found->second);
            if (const auto target = arguments.find("target_value"); target != arguments.end()) {
                const QString unit = arguments.count("unit")
                    ? QString::fromStdString(arguments.at("unit")) : QString();
                prompt = QStringLiteral("Требуется: %1 %2\nВведите фактически установленное значение:")
                    .arg(QString::fromStdString(target->second), unit);
                actual = QString::fromStdString(target->second).toDouble();
            }
            QMetaObject::invokeMethod(this, [&] {
                actual = QInputDialog::getDouble(this, title, prompt, actual,
                    -1000000.0, 1000000.0, 6, &accepted);
            }, Qt::BlockingQueuedConnection);
            if (!accepted) throw std::runtime_error("Ручная операция отменена оператором");
            const QString operatorName = qEnvironmentVariable("USERNAME",
                qEnvironmentVariable("USER", QStringLiteral("неизвестен")));
            std::ostringstream response;
            response << std::setprecision(15) << "status=confirmed\nvalue=" << actual
                     << "\noperator=" << operatorName.toStdString()
                     << "\ntimestamp=" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toStdString()
                     << "\n";
            return response.str();
        });
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto checkDevice = [this, &uiCodes, &deliveryEquipment, &activeCapabilities](
                                 const orbita::stand::DeviceProfile& definition,
                                 bool armSupply) {
        const QString code = uiCodes.value(QString::fromStdString(definition.pluginId));
        if (!deliveryEquipment.contains(code)) return;
        if (!definition.enabled) {
            const auto reason = definition.configuration.find("disabled_reason");
            const QString detail = reason == definition.configuration.end()
                ? QStringLiteral("Отключено в профиле стенда")
                : QString::fromStdString(reason->second);
            if (!code.isEmpty()) testPage_->setEquipmentStatus(code, false, detail);
            log(QStringLiteral("%1: %2")
                .arg(QString::fromStdString(definition.id), detail));
            return;
        }
        if (!code.isEmpty()) {
            testPage_->setEquipmentChecking(code, armSupply
                ? QStringLiteral("Подключение АКИП и включение питания УБСИ 27 В…")
                : QStringLiteral("Загрузка DLL и проверка связи…"));
        }
        try {
            auto config = definition.configuration;
            config["record_root"] = QDir(QCoreApplication::applicationDirPath())
                .filePath(QStringLiteral("runs")).toStdString();
            config["profile.active_outputs_confirmed"] = standProfile_.activeOutputsConfirmed ? "true" : "false";
            for (const auto& [key, value] : standProfile_.routes) config["route." + key] = value;
            auto device = equipmentPlugins_->createDevice(
                definition.pluginId, definition.id, config);
            if (definition.bindCapabilities.empty()) throw std::runtime_error("В профиле не указана возможность устройства");
            const std::string probeCapability = definition.bindCapabilities.front();
            const std::string response = device->invoke(probeCapability, "probe");
            bool passiveReady = response.find("alive=0") == std::string::npos
                && response.find("alive=false") == std::string::npos;
            bool activeBlocked = false;
            const bool deviceActiveCommandsConfirmed =
                config.count("device.active_commands_confirmed")
                && (config.at("device.active_commands_confirmed") == "true"
                    || config.at("device.active_commands_confirmed") == "1");
            for (const auto& capability : definition.bindCapabilities) {
                if (!standProfile_.activeOutputsConfirmed
                    && !deviceActiveCommandsConfirmed
                    && activeCapabilities.contains(QString::fromStdString(capability))) {
                    activeBlocked = true;
                    continue;
                }
                equipmentRegistry_->bind(capability, device);
            }
            equipmentDevices_.push_back(device);
            std::string finalResponse = response;
            if (armSupply) {
                // Адаптер УБСИ питается от этого источника. Мастер готовности
                // обязан включить питание до сетевой проверки адаптера.
                device->invoke("power.dc_supply", "set_current_limit", {{"amperes", "0.6"}});
                device->invoke("power.dc_supply", "set_voltage", {{"volts", "27.0"}});
                device->invoke("power.dc_supply", "output", {{"enabled", "true"}});
                finalResponse = device->invoke("power.dc_supply", "read_state", {});
                if (finalResponse.find("output_enabled=true") == std::string::npos) {
                    throw std::runtime_error("АКИП не подтвердил включение питания УБСИ");
                }
            }
            const bool ready = passiveReady && !activeBlocked;
            QString detail = QString::fromStdString(finalResponse).trimmed();
            if (activeBlocked) detail += QStringLiteral(
                "; активные воздействия заблокированы профилем до подтверждения схемы");
            if (!code.isEmpty()) testPage_->setEquipmentStatus(code, ready, detail);
            log(QStringLiteral("%1: %2").arg(QString::fromStdString(definition.id), detail));
        } catch (const std::exception& error) {
            const QString detail = QString::fromUtf8(error.what());
            if (!code.isEmpty()) testPage_->setEquipmentStatus(code, false, detail);
            log(QStringLiteral("%1 не готов: %2")
                .arg(QString::fromStdString(definition.id), detail));
        }
    };

    // 1. Сначала включаем питание УБСИ. Предыдущий safeStop освобождает COM и
    // гарантирует известное состояние, но новый экземпляр АКИП сразу заново
    // задаёт 27 В / 0,6 А и включает выход.
    for (const auto& definition : standProfile_.devices) {
        if (definition.pluginId == "orbita.akip_1160_pair") checkDevice(definition, true);
    }

    // 2. Проверяем остальное оборудование, не зависящее от запуска UDP-потока.
    testPage_->setEquipmentChecking("E20", QStringLiteral("Проверка E20-10 и активного набора Орбиты…"));
    const bool orbitaReady = e20Available_ && !currentSpecs_.empty();
    if (orbitaReady) {
        equipmentRegistry_->bind("orbita.parameter_source",
            [this](const std::string& operation,
                   const std::map<std::string, std::string>& arguments) {
                return invokeOrbitaParameterSource(operation, arguments);
            });
    }
    for (const auto& definition : standProfile_.devices) {
        if (definition.pluginId == "orbita.akip_1160_pair"
            || definition.pluginId == "orbita.ktma_adapter_udp") continue;
        checkDevice(definition, false);
    }

    // 3. После включения питания адаптеру требуется время на загрузку.
    testPage_->setEquipmentChecking("RS485",
        QStringLiteral("Питание включено; ожидание запуска адаптера 3 с…"));
    log(QStringLiteral("АКИП включён; выдержка 3 с перед проверкой адаптера УЛК"));
    QEventLoop startupDelay;
    QTimer::singleShot(3000, &startupDelay, &QEventLoop::quit);
    startupDelay.exec(QEventLoop::ExcludeUserInputEvents);
    for (const auto& definition : standProfile_.devices) {
        if (definition.pluginId == "orbita.ktma_adapter_udp") checkDevice(definition, false);
    }
    QApplication::restoreOverrideCursor();
    testPage_->setEquipmentStatus("E20", orbitaReady,
        !e20Available_
            ? QStringLiteral("E20-10 не открыт")
            : currentSpecs_.empty()
                ? QStringLiteral("E20-10 найден, но активный набор параметров пуст; откройте инженерный режим и выберите конфигурацию")
                : QStringLiteral("E20-10 найден; активных параметров: %1")
                    .arg(currentSpecs_.size()));
}

std::string MainWindow::invokeOrbitaParameterSource(
    const std::string& operation,
    const std::map<std::string, std::string>& arguments)
{
    if (!e20Available_) return "status=disconnected\ndiagnostic=E20-10 не открыт\n";
    const auto channels = orbita_->getChannels();
    if (operation == "probe") {
        return "status=" + std::string(channels.empty() ? "not_configured" : "ready")
            + "\nrunning=" + (orbita_->isRunning() ? "true" : "false")
            + "\nchannel_count=" + std::to_string(channels.size()) + "\n";
    }
    if (channels.empty()) {
        return "status=not_configured\ndiagnostic=Активный набор параметров Орбиты пуст\n"
               "channel_count=0\nframes_processed=0\nphrase_error_percent=100\n"
               "group_error_percent=100\n";
    }
    if (!orbita_->isRunning()) {
        return "status=not_running\ndiagnostic=Сбор Орбиты не запущен\n"
            "channel_count=" + std::to_string(channels.size()) + "\n"
            "frames_processed=0\nphrase_error_percent=100\ngroup_error_percent=100\n";
    }
    if (operation == "health" || operation == "alive") {
        orbita_->waitForData(std::chrono::milliseconds(1000));
        const auto snapshot = orbita_->getSnapshot();
        const bool ready = snapshot.stats.frames_processed > 0;
        return "status=" + std::string(ready ? "ready" : "no_data")
            + "\ndiagnostic=" + (ready ? std::string("Поток принимается")
                                         : std::string("Нет актуального кадра"))
            + "\nchannel_count=" + std::to_string(channels.size())
            + "\nframes_processed=" + std::to_string(snapshot.stats.frames_processed)
            + "\nphrase_error_percent=" + std::to_string(snapshot.stats.phrase_error_percent)
            + "\ngroup_error_percent=" + std::to_string(snapshot.stats.group_error_percent)
            + "\n";
    }
    if (operation == "read") {
        const auto address = arguments.find("address");
        if (address == arguments.end() || address->second.empty()) {
            throw std::invalid_argument("Для чтения Орбиты требуется address");
        }
        unsigned sampleCount = 1;
        if (const auto count = arguments.find("sample_count"); count != arguments.end()) {
            sampleCount = std::max(1u, static_cast<unsigned>(std::stoul(count->second)));
        }
        double sum = 0.0;
        unsigned accepted = 0;
        for (unsigned index = 0; index < sampleCount; ++index) {
            orbita_->waitForData(std::chrono::milliseconds(500));
            const auto snapshot = orbita_->getSnapshot();
            for (const auto& value : snapshot.values) {
                if (value.address == address->second && value.valid) {
                    sum += value.value;
                    ++accepted;
                    break;
                }
            }
        }
        if (!accepted) {
            return "status=no_data\ndiagnostic=Параметр не обновлялся\n";
        }
        return "status=ready\nvalue=" + std::to_string(sum / accepted)
            + "\nsample_count=" + std::to_string(accepted) + "\n";
    }
    throw std::invalid_argument("Неизвестная операция источника Орбита: " + operation);
}

void MainWindow::onRunScenario(
    const QString& scenarioCode, const QString& objectSerial, bool allowPartial)
{
    if (!standRuntimeReady_ || !scenarioWatcher_ || scenarioWatcher_->isRunning()) return;
    const auto iterator = scenarios_.constFind(scenarioCode);
    if (iterator == scenarios_.cend()) {
        testPage_->setRunInProgress(false);
        log(QStringLiteral("Сценарий %1 не загружен").arg(scenarioCode));
        return;
    }
    const auto scenario = iterator.value();
    if (scenarioCode != QStringLiteral("YALK_FULL_5_6")
        && scenarioCode != QStringLiteral("YALK_CONTACT_THRESHOLDS")
        && scenarioCode != QStringLiteral("YTP_FULL_5_6")
        && scenarioCode != QStringLiteral("YTP_120_CHECK")
        && scenarioCode != QStringLiteral("ULK_COMBINED_CHECK")
        && equipmentRegistry_->hasCapability("orbita.parameter_source")
        && !orbita_->isRunning()) {
        onStart();
    }
    scenarioEngine_->resetStop();
    const std::string serial = objectSerial.toStdString();
    const bool partial = allowPartial;
    testPage_->setRunInProgress(true, QStringLiteral("Выполняется: %1")
        .arg(QString::fromStdString(scenario.title)));
    log(QStringLiteral("Запуск сценария %1, объект %2")
        .arg(QString::fromStdString(scenario.id),
             objectSerial.isEmpty() ? QStringLiteral("без заводского номера") : objectSerial));
    scenarioWatcher_->setFuture(QtConcurrent::run([this, scenario, serial, partial]() {
        return scenarioEngine_->run(scenario, *equipmentRegistry_,
            standProfile_.version, serial, partial,
            [this](const orbita::stand::RunEvent& event) {
                QMetaObject::invokeMethod(this, [this, event]() {
                    testPage_->setRunEvent(event);
                }, Qt::QueuedConnection);
            });
    }));
}

void MainWindow::onStopScenario()
{
    if (scenarioEngine_) scenarioEngine_->requestStop();
    if (equipmentRegistry_) equipmentRegistry_->safeStopAll();
    testPage_->setRunInProgress(true, QStringLiteral("Остановка и безопасный сброс оборудования…"));
    log(QStringLiteral("Оператор запросил безопасную остановку сценария"));
}

// ----------------------------------------------------------------------------
//  Управление режимами
// ----------------------------------------------------------------------------
void MainWindow::setEngineerMode(bool enabled)
{
    testPage_->setEngineerMode(enabled);
    if (mainToolbar_) mainToolbar_->setVisible(enabled);
    if (menuBar()) menuBar()->setVisible(enabled);
    for (auto* action : {actMain_, actDetail_, actConfig_, actDb_}) {
        if (action) action->setVisible(enabled);
    }
    if (toolsMenu_) toolsMenu_->menuAction()->setVisible(enabled);
    if (!enabled) setMode(ModeTests);
}

void MainWindow::setMode(int mode)
{
    // Ранний выход если стек не инициализирован
    if (!centralStack_)
        return;

    centralStack_->setCurrentIndex(mode);

    // Обновляем состояние кнопок на панели (с проверками на nullptr)
    if (actMain_)
        actMain_->setChecked(mode == ModeMain);
    if (actDetail_)
        actDetail_->setChecked(mode == ModeDetail);
    if (actConfig_)
        actConfig_->setChecked(mode == ModeConfig);
    if (actDb_)
        actDb_->setChecked(mode == ModeDb);
    if (actTests_)
        actTests_->setChecked(mode == ModeTests);

    const bool telemetryControlsVisible = mode != ModeTests;
    statusBar()->setVisible(telemetryControlsVisible);
    configCombo_->setVisible(telemetryControlsVisible);
    startBtn_->setVisible(telemetryControlsVisible);
    stopBtn_->setVisible(telemetryControlsVisible);
    recordBtn_->setVisible(telemetryControlsVisible);
    recordingLabel_->setVisible(telemetryControlsVisible);
    invertCheck_->setVisible(telemetryControlsVisible);
    mtvLabel_->setVisible(telemetryControlsVisible);
    statusLabel_->setVisible(telemetryControlsVisible);
    errPhraseLabel_->setVisible(telemetryControlsVisible);
    errGroupLabel_->setVisible(telemetryControlsVisible);

    // Доки пользователь сам показывает/прячет через меню «Вид» — не навязываем по режиму.

    // Если перешли в детальный режим и есть выбранный канал – обновляем DetailView
    if (mode == ModeDetail && selectedChannelIndex_ >= 0) {
        const auto& specs = orbita_->getChannels();
        if (selectedChannelIndex_ < (int)specs.size()) {
            if (detailView_)
                detailView_->setChannel(specs[selectedChannelIndex_], selectedChannelIndex_);
            // Значение обновится в updateData
        }
    }
}

// ----------------------------------------------------------------------------
//  Управление сбором
// ----------------------------------------------------------------------------
void MainWindow::onStart()
{
    try {
        orbita_->start();
        elapsedTimer_.restart();
        isRunning_ = true;
        startBtn_->setEnabled(false);
        stopBtn_->setEnabled(true);
        recordBtn_->setEnabled(true);
        statusLabel_->setText("● Сбор идёт");
        statusLabel_->setStyleSheet("color: #7fc79a; font-weight: 500;");
        log("Старт сбора данных (E20-10)");
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Ошибка запуска", e.what());
        log("Ошибка запуска: " + QString::fromLocal8Bit(e.what()));
    }
}

void MainWindow::onStop()
{
    orbita_->stop();
    isRunning_ = false;
    if (isRecording_) onToggleRecording(); // остановить запись
    startBtn_->setEnabled(!currentSpecs_.empty());
    stopBtn_->setEnabled(false);
    recordBtn_->setEnabled(false);
    statusLabel_->setText("● Сбор остановлен");
    statusLabel_->setStyleSheet("color: #d99a4a; font-weight: 500;");
    log("Стоп сбора данных");
}

// ----------------------------------------------------------------------------
//  Запись TLM
// ----------------------------------------------------------------------------
QString MainWindow::nextRecordingPath() const
{
    QString dir = QCoreApplication::applicationDirPath() + "/records";
    QDir().mkpath(dir);
    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    return dir + "/" + ts + ".tlm";
}

void MainWindow::onToggleRecording()
{
    if (!isRecording_) {
        QString path = nextRecordingPath();
        orbita_->startRecording(path.toStdString());
        isRecording_ = true;
        recordBtn_->setText("⏹ Стоп");
        recordingLabel_->setText("● " + path.section('/', -1));
        recordingLabel_->setStyleSheet("color: #cf5b52; font-weight: bold;");
        log("Запись: " + path);
    } else {
        orbita_->stopRecording();
        isRecording_ = false;
        recordBtn_->setText("⏺ Запись");
        recordingLabel_->setText("—");
        recordingLabel_->setStyleSheet("color: #7e8a98;");
        log("Запись остановлена");
    }
}

// ----------------------------------------------------------------------------
//  Строка статуса
// ----------------------------------------------------------------------------
void MainWindow::updateStatusBar(const orbita::Snapshot& snap)
{
    if (!m_statusBarLabel) return;

    // Время МТВ
    uint32_t sec = snap.mtv_seconds;
    QString timeStr = QString("%1:%2:%3")
        .arg(sec / 3600,       2, 10, QChar('0'))
        .arg((sec % 3600) / 60, 2, 10, QChar('0'))
        .arg(sec % 60,          2, 10, QChar('0'));

    // Подсчёт аномалий
    int anomalyCount = 0;
    int firstAnomalyIdx = -1;
    for (int i = 0; i < (int)snap.values.size(); ++i) {
        const auto& v = snap.values[i];
        auto tol = chstatus::forAddress(dbProvider_.get(), v.address);
        auto lvl = chstatus::evaluate(v.value, tol);
        if (chstatus::isAnomaly(lvl)) {
            if (firstAnomalyIdx < 0) firstAnomalyIdx = i;
            ++anomalyCount;
        }
    }

    QString toleranceStr = QString("контроль допусков: %1 вне нормы").arg(anomalyCount);
    if (anomalyCount > 0 && firstAnomalyIdx >= 0) {
        QString name;
        if (firstAnomalyIdx < (int)currentSpecs_.size()) {
            const auto& sp = currentSpecs_[firstAnomalyIdx];
            name = QString::fromStdString(sp.name.empty() ? sp.address : sp.name);
        } else {
            name = QString::fromStdString(snap.values[firstAnomalyIdx].address);
        }
        toleranceStr += QString(" — КАН %1 %2").arg(firstAnomalyIdx + 1).arg(name);
    }

    // Запись
    QString recStr = isRecording_ ? recordingLabel_->text() : "—";

    QString line = QString("● %1  |  %2  |  запись: %3  |  10 Гц · кадр 1024 Б")
        .arg(timeStr, toleranceStr, recStr);

    m_statusBarLabel->setText(line);
}

// ----------------------------------------------------------------------------
//  Обновление данных
// ----------------------------------------------------------------------------
void MainWindow::updateData()
{
    if (!isRunning_) {
        if (m_statusBarLabel)
            m_statusBarLabel->setText("● сбор остановлен");
        return;
    }

    if (orbita_->waitForData(std::chrono::milliseconds(0))) {
        orbita::Snapshot snap = orbita_->getSnapshot();

        // МТВ
        uint32_t sec = snap.mtv_seconds;
        mtvLabel_->setText(QString("%1:%2:%3")
                               .arg(sec / 3600, 2, 10, QChar('0'))
                               .arg((sec % 3600) / 60, 2, 10, QChar('0'))
                               .arg(sec % 60, 2, 10, QChar('0')));

        // Ошибки
        errPhraseLabel_->setText(QString("ошибки фраз: %1%").arg(snap.stats.phrase_error_percent));
        errGroupLabel_->setText(QString("групп: %1%").arg(snap.stats.group_error_percent));

        // Нижняя строка статуса
        updateStatusBar(snap);

        // Обновляем MainPage
        if (mainPage_)
            mainPage_->updateData(snap);

        // Если в режиме детального просмотра – обновляем DetailView
        if (centralStack_->currentIndex() == ModeDetail && selectedChannelIndex_ >= 0) {
            const auto& specs = orbita_->getChannels();
            if (selectedChannelIndex_ < (int)specs.size()) {
                const auto& spec = specs[selectedChannelIndex_];
                double val = 0.0;
                for (const auto& v : snap.values) {
                    if (v.address == spec.address) {
                        val = v.value;
                        break;
                    }
                }
                detailView_->updateValue(val);
            }
        }
    }
}

// ----------------------------------------------------------------------------
//  Конфигурация и метаданные
// ----------------------------------------------------------------------------
void MainWindow::applyConfiguration(const QString& fileName)
{
    if (fileName.isEmpty()) return;
    QString fullPath = QCoreApplication::applicationDirPath() + "/address/" + fileName;
    std::string content = encoding::readFileToUtf8(fullPath.toStdString());
    if (content.empty()) {
        log("Не удалось прочитать: " + fileName);
        return;
    }

    std::vector<orbita::ChannelSpec> specs;
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        std::string normAddr = encoding::normalizeAddress(line);
        if (normAddr.empty()) continue;
        QString qnorm = QString::fromStdString(normAddr);
        QString name = dbProvider_ ? dbProvider_->getName(qnorm) : QString();
        QString cat = dbProvider_ ? dbProvider_->getCategory(qnorm) : QString();
        specs.push_back({normAddr, name.toStdString(), cat.toStdString()});
    }

    if (specs.empty()) {
        log("Конфигурация пуста");
        return;
    }

    if (watchSetDockWidget_)
        watchSetDockWidget_->setFromSpecs(specs);
    log(QString("Конфигурация загружена: %1 (%2 каналов)")
            .arg(fileName).arg(specs.size()));
}

void MainWindow::refreshConfigCombo()
{
    if (!configCombo_) return;
    QString curText = configCombo_->currentIndex() > 0 ? configCombo_->currentText() : QString();
    configCombo_->blockSignals(true);
    configCombo_->clear();
    configCombo_->addItem("— конфиг —");
    QString addrDir = QCoreApplication::applicationDirPath() + "/address/";
    QDir dir(addrDir);
    for (const QString& f : dir.entryList({"*.txt"}, QDir::Files, QDir::Name))
        configCombo_->addItem(f);
    int idx = curText.isEmpty() ? -1 : configCombo_->findText(curText);
    configCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
    configCombo_->blockSignals(false);
}

void MainWindow::onRefreshMetadata()
{
    if (dbProvider_) dbProvider_->refresh();
    if (configDockWidget_) configDockWidget_->updateMetadata();
    if (configPage_) configPage_->updateMetadata();
    if (paramDockWidget_) paramDockWidget_->rebuildTree();
    if (dbPage_) dbPage_->rebuildTree();
    refreshConfigCombo();
    log("Метаданные обновлены");
}

// ----------------------------------------------------------------------------
//  Активный набор
// ----------------------------------------------------------------------------
void MainWindow::onWatchSetChanged(const std::vector<orbita::ChannelSpec>& specs)
{
    try {
        orbita_->setChannels(specs);
    } catch (const std::exception& e) {
        log("Ошибка набора: " + QString::fromLocal8Bit(e.what()));
        return;
    }

    currentSpecs_ = specs;
    if (mainPage_)
        mainPage_->setChannels(specs);

    startBtn_->setEnabled(!specs.empty() && !isRunning_);
    stopBtn_->setEnabled(isRunning_);
    recordBtn_->setEnabled(isRunning_);

    // Если выбранный индекс выходит за пределы, сбрасываем
    if (selectedChannelIndex_ >= (int)specs.size())
        selectedChannelIndex_ = -1;
    // Авто-выбор первого канала, чтобы отсчёт не висел на "---"
    if (selectedChannelIndex_ < 0 && !specs.empty())
        selectedChannelIndex_ = 0;
    if (selectedChannelIndex_ >= 0 && !specs.empty())
        onChannelSelected(selectedChannelIndex_);
}

// ----------------------------------------------------------------------------
//  Выбор канала
// ----------------------------------------------------------------------------
void MainWindow::onChannelSelected(int index)
{
    selectedChannelIndex_ = index;
    if (mainPage_)
        mainPage_->setSelectedChannel(index);

    if (centralStack_->currentIndex() == ModeDetail) {
        const auto& specs = orbita_->getChannels();
        if (index >= 0 && index < (int)specs.size()) {
            detailView_->setChannel(specs[index], index);
            // Значение будет обновлено в updateData
        }
    }
}

void MainWindow::onChannelDoubleClicked(int index)
{
    setMode(ModeDetail);
    onChannelSelected(index);
}

void MainWindow::onNavigateChannel(int delta)
{
    int newIdx = selectedChannelIndex_ + delta;
    const auto& specs = orbita_->getChannels();
    if (newIdx >= 0 && newIdx < (int)specs.size()) {
        onChannelSelected(newIdx);
    }
}

// ----------------------------------------------------------------------------
//  Вспомогательные методы
// ----------------------------------------------------------------------------
void MainWindow::log(const QString& msg)
{
    QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    if (logEdit_) {
        logEdit_->append(QString("[%1] %2").arg(ts, msg));
        logEdit_->moveCursor(QTextCursor::End);
    }
    qDebug() << msg;
}

// ----------------------------------------------------------------------------
//  Извлечение номера канала из адреса (пример: "M16P1A70B12C10D10T01" -> ?)
//  В прототипе номер канала – это порядковый индекс в конфигурации, а не из адреса.
//  Поэтому мы используем индекс, переданный из виджетов.
//  Оставим этот метод как заглушку, если понадобится.
// ----------------------------------------------------------------------------
int MainWindow::extractChannelNumber(const std::string& address)
{
    // Пока не используется, но можно реализовать парсинг, если нужно
    return -1;
}
