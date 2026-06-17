#pragma once

#include "scenario_model.h"

#include <QDialog>
#include <QTableWidget>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTimer>
#include <functional>
#include <optional>

/// Провайдер значений: принимает адрес канала, возвращает сырой код или nullopt.
/// Такой интерфейс позволяет тестировать диалог без реального ядра.
using ValueProvider = std::function<std::optional<double>(const QString& addr)>;

/// Диалог «Сценарий проверки» — мастер автопрогона шагов сценария.
class ScenarioWizard : public QDialog
{
    Q_OBJECT

public:
    explicit ScenarioWizard(ValueProvider provider, QWidget* parent = nullptr);

    /// Загрузить сценарий (сбрасывает текущее состояние).
    void setScenario(const Scenario& scenario);

private slots:
    void onLoadScn();
    void onSaveScn();
    void onAddCommand();
    void onAddCheck();
    void onRunAuto();
    void onReset();
    void onDoneStep();        ///< оператор нажал «Выполнено» для текущей команды
    void processNextStep();   ///< продвинуть автопрогон на один шаг
    void onSaveLog();         ///< сохранить лог в файл

private:
    void setupUi();
    void refreshTable();
    void updateCounters();
    void setRowResult(int row, StepResult result);
    void highlightRow(int row);
    void finishRun();
    void startAutoRun();
    void appendLog(const QString& text);  ///< добавить строку в лог

    ValueProvider    provider_;
    Scenario         scenario_;

    // Состояние автопрогона
    bool             running_     = false;
    int              currentStep_ = -1;  ///< индекс текущего шага (-1 = не идём)
    bool             waitingCmd_  = false; ///< ждём нажатия «Выполнено» от оператора

    // UI-элементы
    QTableWidget*  table_        = nullptr;
    QPushButton*   btnLoad_      = nullptr;
    QPushButton*   btnSave_      = nullptr;
    QPushButton*   btnAddCmd_    = nullptr;
    QPushButton*   btnAddCheck_  = nullptr;
    QPushButton*   btnRun_       = nullptr;
    QPushButton*   btnReset_     = nullptr;
    QPushButton*   btnDone_      = nullptr;  ///< кнопка «Выполнено» для CMD-шагов
    QPushButton*   btnSaveLog_   = nullptr;  ///< кнопка «Сохранить лог…»
    QPlainTextEdit* logEdit_     = nullptr;  ///< лог автопрогона
    QLabel*        lblPassed_    = nullptr;
    QLabel*        lblFailed_    = nullptr;
    QLabel*        lblRemaining_ = nullptr;
    QProgressBar*  progressBar_  = nullptr;
    QLabel*        lblVerdict_   = nullptr;

    QTimer*        stepTimer_    = nullptr;  ///< задержка между автошагами (не блокирует UI)
};
