#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QTextEdit>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolBar>
#include <QDockWidget>
#include <QStatusBar>
#include <QComboBox>
#include <QDir>
#include <QFutureWatcher>
#include <QHash>

#include "orbita_stand/config.h"
#include "orbita_stand/equipment_runtime.h"
#include "orbita_stand/run_store.h"
#include "orbita_stand/report_writer.h"
#include "registrar.h"

#include "orbita.h"
#include "metadata_service.h"
#include "tolerance_resolver.h"
#include "main_page.h"
#include "detail_view.h"
#include "parameter_browser.h"
#include "config_manager_widget.h"
#include "watch_set_widget.h"
#include "test_page.h"
#include "home_page.h"
#include "registrar_page.h"

class QMenu;
class QCloseEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

    // Narrow integration surface for product packages layered on top of the
    // reusable Station/Orbita shell. It intentionally exposes stand services,
    // not Orbita telemetry internals. UBSI uses this from KtmaMainWindow while
    // future BSI/RPU packages may compose Orbita intentionally.
    bool integrationProductionWorkflowActive() const
    {
        return activeWorkflow_ == Workflow::Production;
    }
    bool integrationTuWorkflowActive() const
    {
        return activeWorkflow_ == Workflow::Tu;
    }
    TestPage* integrationTestPage() const { return testPage_; }
    HomePage* integrationHomePage() const { return homePage_; }
    RegistrarPage* integrationRegistrarPage() const { return registrarPage_; }
    ktma::registrar::Registrar* integrationRegistrar() const { return registrar_.get(); }
    orbita::stand::ScenarioEngine* integrationScenarioEngine() const { return scenarioEngine_.get(); }
    orbita::stand::EquipmentRegistry* integrationEquipmentRegistry() const { return equipmentRegistry_.get(); }
    orbita::stand::EquipmentPluginManager* integrationEquipmentPlugins() const { return equipmentPlugins_.get(); }
    std::vector<std::shared_ptr<orbita::stand::EquipmentDevice>>& integrationEquipmentDevices()
    {
        return equipmentDevices_;
    }
    orbita::stand::StandProfile& integrationStandProfile() { return standProfile_; }
    QHash<QString, orbita::stand::ScenarioDefinition>& integrationScenarios() { return scenarios_; }
    QHash<QString, QString>& integrationScenarioPaths() { return scenarioPaths_; }
    QFutureWatcher<orbita::stand::ScenarioRunResult>* integrationScenarioWatcher() const
    {
        return scenarioWatcher_;
    }
    bool integrationStandRuntimeReady() const { return standRuntimeReady_; }
    void integrationEnsureStandRuntime()
    {
        if (!standRuntimeReady_) initializeStandRuntime();
    }
    void integrationLegacyEquipmentCheck() { onCheckTestEquipment(); }
    void integrationDisableBaseScenarioRunner()
    {
        QObject::disconnect(scenarioRunConnection_);
    }
    void integrationLog(const QString& message) { log(message); }
    void integrationUseUbsiEngineering() { ubsiEngineering_ = true; setEngineerMode(false); }
    void integrationOpenRegistrar() { setMode(ModeAdmin); }
    void integrationOpenTests() { setMode(ModeTests); }
    bool integrationResultSaved() const { return lastResultSaved_; }
    virtual bool integrationUsesDedicatedProductionFinalizer() const
    {
        return false;
    }

private slots:
    // Управление сбором
    void onStart();
    void onStop();
    void onToggleRecording();

    // Периодическое обновление
    void updateData();

    // Метаданные
    void onRefreshMetadata();

    // Конфигурация
    void applyConfiguration(const QString& fileName);

    // Навигация по режимам
    void setMode(int mode); // 0=Сбор, 1=Детальный, 2=Конфиг, 3=БД

    // Выбор канала
    void onChannelSelected(int index);
    void onChannelDoubleClicked(int index);
    void onNavigateChannel(int delta);

    // Активный набор
    void onWatchSetChanged(const std::vector<orbita::ChannelSpec>& specs);

private:
    void setupUi();
    void setupToolBar();
    void setupDockWidgets();
    void refreshConfigCombo();
    QString nextRecordingPath() const;
    void log(const QString& msg);
    void updateStatusBar(const orbita::Snapshot& snap);
    void initializeStandRuntime();
    void setEngineerMode(bool enabled);
    std::string invokeOrbitaParameterSource(
        const std::string& operation,
        const std::map<std::string, std::string>& arguments);

    // Вспомогательные методы
    static int extractChannelNumber(const std::string& address);

    // Ядро и БД
    std::unique_ptr<orbita::Orbita> orbita_;
    std::unique_ptr<MetadataService> dbProvider_;
    ToleranceResolver toleranceResolver_;

    // Таймер
    QTimer* updateTimer_;
    QElapsedTimer elapsedTimer_;
    bool isRunning_ = false;
    bool isRecording_ = false;

    // Центральный стек
    QStackedWidget* centralStack_;

    // Страницы
    HomePage* homePage_ = nullptr;
    RegistrarPage* registrarPage_ = nullptr;
    MainPage* mainPage_ = nullptr;
    TestPage* testPage_ = nullptr;
    DetailView* detailView_ = nullptr;
    ConfigManagerWidget* configPage_ = nullptr;
    ParameterBrowser* dbPage_ = nullptr;

    // Док-виджеты (используются в режиме Сбор)
    QDockWidget* configDock_ = nullptr;
    QDockWidget* paramDock_ = nullptr;
    QDockWidget* watchSetDock_ = nullptr;
    QDockWidget* logDock_ = nullptr;

    ConfigManagerWidget* configDockWidget_ = nullptr;
    ParameterBrowser* paramDockWidget_ = nullptr;
    WatchSetWidget* watchSetDockWidget_ = nullptr;

    // Элементы панели инструментов
    QLabel* mtvLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* startBtn_ = nullptr;
    QPushButton* stopBtn_ = nullptr;
    QPushButton* recordBtn_ = nullptr;
    QLabel* recordingLabel_ = nullptr;
    QCheckBox* invertCheck_ = nullptr;
    QLabel* errPhraseLabel_ = nullptr;
    QLabel* errGroupLabel_ = nullptr;

    // Лог
    QTextEdit* logEdit_ = nullptr;

    // Выбранный канал
    int selectedChannelIndex_ = -1;
    std::vector<orbita::ChannelSpec> currentSpecs_;

    // Режимы
    enum Mode { ModeHome = 0, ModeTests = 1, ModeMain = 2, ModeDetail = 3, ModeConfig = 4, ModeDb = 5, ModeAdmin = 6 };
    enum class Workflow { None, Production, Tu };

    // Для запоминания активного действия на панели
    QAction* actTests_ = nullptr;
    QAction* actMain_ = nullptr;
    QAction* actDetail_ = nullptr;
    QAction* actConfig_ = nullptr;
    QAction* actDb_ = nullptr;

    // Нижняя строка статуса
    QLabel* m_statusBarLabel = nullptr;

    // Быстрый выбор конфига в тулбаре
    QComboBox* configCombo_ = nullptr;
    QComboBox* accessModeCombo_ = nullptr;
    QToolBar* mainToolbar_ = nullptr;
    QAction* themeAction_ = nullptr;
    bool lightTheme_ = false;

    // Сценарий проверки
    QAction* actScenario_ = nullptr;
    QMenu* toolsMenu_ = nullptr;

    bool e20Available_ = false;
    bool ubsiEngineering_ = false;
    bool lastResultSaved_ = false;

    std::unique_ptr<orbita::stand::EquipmentPluginManager> equipmentPlugins_;
    std::unique_ptr<orbita::stand::EquipmentRegistry> equipmentRegistry_;
    std::vector<std::shared_ptr<orbita::stand::EquipmentDevice>> equipmentDevices_;
    std::unique_ptr<orbita::stand::ScenarioEngine> scenarioEngine_;
    std::unique_ptr<orbita::stand::RunStore> runStore_;
    std::unique_ptr<ktma::registrar::Registrar> registrar_;
    orbita::stand::StandProfile standProfile_;
    QHash<QString, orbita::stand::ScenarioDefinition> scenarios_;
    QHash<QString, QString> scenarioPaths_;
    QFutureWatcher<orbita::stand::ScenarioRunResult>* scenarioWatcher_ = nullptr;
    Workflow activeWorkflow_ = Workflow::None;
    std::string pendingProductionStageAttemptId_;
    orbita::stand::ProductionReportMetadata pendingProductionReportMetadata_;
    bool closeAfterScenario_ = false;
    bool standRuntimeReady_ = false;
    QMetaObject::Connection scenarioRunConnection_;

private slots:
    void onOpenScenario();
    void onOpenCatalog();
    void onOpenStandProfile();
    void onCheckTestEquipment();
    void onRunScenario(const QString& scenarioCode, const QString& objectSerial,
                       bool allowPartial);
    void onStopScenario();
    void toggleTheme();
    void applyTheme(bool light);
};

#endif // MAINWINDOW_H
