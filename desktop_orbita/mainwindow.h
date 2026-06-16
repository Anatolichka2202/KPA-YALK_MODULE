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

#include "orbita.h"
#include "metadata_service.h"
#include "tolerance_resolver.h"
#include "scenario_wizard.h"
#include "main_page.h"
#include "detail_view.h"
#include "parameter_browser.h"
#include "config_manager_widget.h"
#include "watch_set_widget.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

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
    QString nextRecordingPath() const;
    void log(const QString& msg);

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
    MainPage* mainPage_ = nullptr;
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
    enum Mode { ModeMain = 0, ModeDetail = 1, ModeConfig = 2, ModeDb = 3 };

    // Для запоминания активного действия на панели
    QAction* actMain_ = nullptr;
    QAction* actDetail_ = nullptr;
    QAction* actConfig_ = nullptr;
    QAction* actDb_ = nullptr;

    // Сценарий проверки
    QAction* actScenario_ = nullptr;

private slots:
    void onOpenScenario();
};

#endif // MAINWINDOW_H
