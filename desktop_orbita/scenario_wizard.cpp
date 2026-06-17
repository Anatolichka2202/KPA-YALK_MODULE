#include "scenario_wizard.h"
#include "scenario_io.h"
#include "channel_picker_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QFont>
#include <QColor>
#include <QBrush>
#include <QDialog>
#include <QPlainTextEdit>
#include <QFrame>
#include <QFile>
#include <QTextStream>

// Цвета в стиле тёмной темы приложения
static const QColor COLOR_OK   = QColor("#5E93B8");
static const QColor COLOR_FAIL = QColor("#CF5B52");
static const QColor COLOR_CURRENT = QColor("#3a4150");

// Подсветка текста
static const QColor TEXT_OK   = QColor("#c8e6ff");
static const QColor TEXT_FAIL = QColor("#ffd0cc");

// Индексы столбцов таблицы
enum Col {
    ColType   = 0,
    ColDesc   = 1,
    ColExpect = 2,
    ColResult = 3,
    COL_COUNT = 4
};

ScenarioWizard::ScenarioWizard(ValueProvider provider, QWidget* parent)
    : QDialog(parent)
    , provider_(std::move(provider))
{
    setWindowTitle("Сценарий проверки — Орбита-IV");
    resize(900, 600);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);

    setupUi();

    stepTimer_ = new QTimer(this);
    stepTimer_->setSingleShot(true);
    connect(stepTimer_, &QTimer::timeout, this, &ScenarioWizard::processNextStep);
}

void ScenarioWizard::setupUi()
{
    // Общий тёмный фон в духе приложения
    setStyleSheet(
        "QDialog { background: #14171c; color: #dfe6ee; }"
        "QLabel { color: #dfe6ee; }"
        "QPushButton { background: #1e2430; color: #dfe6ee; border: 1px solid #2a313b;"
        "              padding: 4px 10px; border-radius: 3px; }"
        "QPushButton:hover { background: #2a3345; }"
        "QPushButton:disabled { color: #556070; border-color: #1e2430; }"
        "QTableWidget { background: #0e1115; color: #dfe6ee; gridline-color: #2a313b;"
        "                selection-background-color: #3a4150; }"
        "QHeaderView::section { background: #1e2430; color: #aab4c0; border: 1px solid #2a313b; }"
        "QProgressBar { background: #1e2430; border: 1px solid #2a313b; border-radius: 3px;"
        "               color: #dfe6ee; text-align: center; }"
        "QProgressBar::chunk { background: #5E93B8; }"
    );

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // --- Заголовок с именем сценария ---
    lblScenarioHeader_ = new QLabel("Сценарий не загружен");
    lblScenarioHeader_->setStyleSheet(
        "QLabel { font-family:'IBM Plex Sans',sans-serif; font-size:15px; font-weight:600;"
        "         color:#dfe6ee; padding:7px 12px; background:#1B1F26;"
        "         border:1px solid #2a313b; border-radius:4px; }");
    mainLayout->addWidget(lblScenarioHeader_);

    // --- Панель кнопок управления ---
    auto* btnBar = new QHBoxLayout;
    btnLoad_     = new QPushButton("Загрузить .scn…");
    btnSave_     = new QPushButton("Сохранить .scn…");
    btnAddCmd_   = new QPushButton("Добавить команду");
    btnAddCheck_ = new QPushButton("Добавить проверку");
    btnRun_      = new QPushButton("▶ Автопрогон");
    btnReset_    = new QPushButton("Сброс");

    btnBar->addWidget(btnLoad_);
    btnBar->addWidget(btnSave_);
    btnBar->addWidget(btnAddCmd_);
    btnBar->addWidget(btnAddCheck_);
    btnBar->addStretch();
    btnBar->addWidget(btnRun_);
    btnBar->addWidget(btnReset_);
    mainLayout->addLayout(btnBar);

    // --- Таблица шагов ---
    table_ = new QTableWidget(0, COL_COUNT, this);
    table_->setHorizontalHeaderLabels({"Тип", "Описание / Адрес", "Ожидание [lo, hi]", "Результат"});
    table_->horizontalHeader()->setSectionResizeMode(ColDesc, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(ColType, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(ColExpect, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(ColResult, QHeaderView::ResizeToContents);
    table_->verticalHeader()->setDefaultSectionSize(26);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(false);
    mainLayout->addWidget(table_, 1);

    // --- Панель текущей команды оператора ---
    cmdPanel_ = new QFrame;
    cmdPanel_->setStyleSheet(
        "QFrame { background:#1a2535; border:1px solid #3a5072; border-radius:6px; }");
    {
        auto* pl = new QVBoxLayout(cmdPanel_);
        pl->setContentsMargins(14, 10, 14, 10);
        pl->setSpacing(4);
        auto* lbl = new QLabel("▶ Выполните команду:");
        lbl->setStyleSheet("color:#5E93B8; font-size:10px; font-weight:600;"
                           " text-transform:uppercase; letter-spacing:1px;");
        lblCmdText_ = new QLabel;
        lblCmdText_->setStyleSheet(
            "color:#dfe6ee; font-size:14px; font-weight:600; margin-top:2px;");
        lblCmdText_->setWordWrap(true);
        pl->addWidget(lbl);
        pl->addWidget(lblCmdText_);
    }
    cmdPanel_->setVisible(false);
    mainLayout->addWidget(cmdPanel_);

    // --- Кнопка «Выполнено» для CMD-шагов (скрыта пока не идёт прогон) ---
    btnDone_ = new QPushButton("✓ Выполнено");
    btnDone_->setStyleSheet(
        "QPushButton { background: #1e3a28; color: #8fe0a0; border: 1px solid #2e6040;"
        "              padding: 6px 20px; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background: #2a5038; }"
    );
    btnDone_->setVisible(false);
    mainLayout->addWidget(btnDone_);

    // --- Область лога автопрогона ---
    logEdit_ = new QPlainTextEdit;
    logEdit_->setReadOnly(true);
    logEdit_->setPlainText("");
    logEdit_->setStyleSheet(
        "QPlainTextEdit { background: #0a0d10; color: #90ee90; border: 1px solid #2a313b;"
        "                 font-family: 'Courier New'; font-size: 10px; margin-top: 8px; }"
    );
    logEdit_->setFixedHeight(120);
    mainLayout->addWidget(logEdit_);

    // --- Кнопка сохранения лога ---
    btnSaveLog_ = new QPushButton("Сохранить лог…");
    btnSaveLog_->setVisible(false);
    mainLayout->addWidget(btnSaveLog_);
    connect(btnSaveLog_, &QPushButton::clicked, this, &ScenarioWizard::onSaveLog);

    // --- Строка статуса ---
    auto* statusBar = new QHBoxLayout;
    lblPassed_    = new QLabel("Пройдено: 0");
    lblFailed_    = new QLabel("Провалено: 0");
    lblRemaining_ = new QLabel("Осталось: 0");
    progressBar_  = new QProgressBar;
    progressBar_->setMinimum(0);
    progressBar_->setMaximum(1);
    progressBar_->setValue(0);
    progressBar_->setFixedHeight(18);
    progressBar_->setMinimumWidth(200);

    lblPassed_->setStyleSheet("color: #5E93B8; font-weight: bold;");
    lblFailed_->setStyleSheet("color: #CF5B52; font-weight: bold;");
    lblRemaining_->setStyleSheet("color: #aab4c0;");

    statusBar->addWidget(lblPassed_);
    statusBar->addWidget(new QLabel("|"));
    statusBar->addWidget(lblFailed_);
    statusBar->addWidget(new QLabel("|"));
    statusBar->addWidget(lblRemaining_);
    statusBar->addStretch();
    statusBar->addWidget(progressBar_);
    mainLayout->addLayout(statusBar);

    // --- Метка итогового вердикта ---
    lblVerdict_ = new QLabel("");
    lblVerdict_->setAlignment(Qt::AlignCenter);
    lblVerdict_->setFont(QFont("Arial", 18, QFont::Bold));
    lblVerdict_->setFixedHeight(40);
    lblVerdict_->setVisible(false);
    mainLayout->addWidget(lblVerdict_);

    // --- Сигналы ---
    connect(btnLoad_,     &QPushButton::clicked, this, &ScenarioWizard::onLoadScn);
    connect(btnSave_,     &QPushButton::clicked, this, &ScenarioWizard::onSaveScn);
    connect(btnAddCmd_,   &QPushButton::clicked, this, &ScenarioWizard::onAddCommand);
    connect(btnAddCheck_, &QPushButton::clicked, this, &ScenarioWizard::onAddCheck);
    connect(btnRun_,      &QPushButton::clicked, this, &ScenarioWizard::onRunAuto);
    connect(btnReset_,    &QPushButton::clicked, this, &ScenarioWizard::onReset);
    connect(btnDone_,     &QPushButton::clicked, this, &ScenarioWizard::onDoneStep);
}

void ScenarioWizard::setScenario(const Scenario& scenario)
{
    scenario_ = scenario;
    // Сбрасываем результаты
    for (auto& step : scenario_.steps)
        step.result = StepResult::Pending;

    running_     = false;
    currentStep_ = -1;
    waitingCmd_  = false;

    lblScenarioHeader_->setText(scenario_.name.isEmpty() ? "Сценарий (без имени)" : scenario_.name);
    logEdit_->setPlainText("");
    btnSaveLog_->setVisible(false);
    cmdPanel_->setVisible(false);
    refreshTable();
    updateCounters();
    lblVerdict_->setVisible(false);
    btnDone_->setVisible(false);
    btnRun_->setEnabled(!scenario_.steps.isEmpty());
}

void ScenarioWizard::refreshTable()
{
    table_->setRowCount(0);

    QFont monoFont("Courier New", 10);

    for (int i = 0; i < scenario_.steps.size(); ++i) {
        const ScenarioStep& step = scenario_.steps[i];
        table_->insertRow(i);

        // Столбец «Тип»
        auto* itemType = new QTableWidgetItem(
            step.kind == StepKind::Command ? "CMD" : "CHECK");
        itemType->setTextAlignment(Qt::AlignCenter);
        itemType->setFont(monoFont);
        table_->setItem(i, ColType, itemType);

        // Столбец «Описание / Адрес»
        auto* itemDesc = new QTableWidgetItem(
            step.kind == StepKind::Command ? step.text : step.address);
        if (step.kind == StepKind::Check)
            itemDesc->setFont(monoFont);
        table_->setItem(i, ColDesc, itemDesc);

        // Столбец «Ожидание»
        QString expectStr;
        if (step.kind == StepKind::Check)
            expectStr = QString("[%1 … %2]")
                .arg(step.lo, 0, 'g', 6)
                .arg(step.hi, 0, 'g', 6);
        auto* itemExpect = new QTableWidgetItem(expectStr);
        itemExpect->setFont(monoFont);
        itemExpect->setTextAlignment(Qt::AlignCenter);
        table_->setItem(i, ColExpect, itemExpect);

        // Столбец «Результат»
        auto* itemResult = new QTableWidgetItem("—");
        itemResult->setFont(monoFont);
        itemResult->setTextAlignment(Qt::AlignCenter);
        table_->setItem(i, ColResult, itemResult);

        // Применить цвет по текущему результату
        setRowResult(i, step.result);
    }
}

void ScenarioWizard::setRowResult(int row, StepResult result)
{
    if (row < 0 || row >= table_->rowCount()) return;

    QString resultText;
    QColor  bg, fg;

    switch (result) {
    case StepResult::Ok:
        resultText = "ОК";
        bg = QColor("#1a2e1a");
        fg = TEXT_OK;
        break;
    case StepResult::Fail:
        resultText = "НЕ ОК";
        bg = QColor("#2e1a1a");
        fg = TEXT_FAIL;
        break;
    default: // Pending — CMD отличается от CHECK цветом
        resultText = "—";
        if (row < (int)scenario_.steps.size()
                && scenario_.steps[row].kind == StepKind::Command) {
            bg = QColor("#1a1810"); // amber-tint
            fg = QColor("#c8b87a");
        } else {
            bg = QColor("#0e1115");
            fg = QColor("#aab4c0");
        }
        break;
    }

    if (table_->item(row, ColResult))
        table_->item(row, ColResult)->setText(resultText);

    for (int col = 0; col < COL_COUNT; ++col) {
        if (auto* it = table_->item(row, col)) {
            it->setBackground(QBrush(bg));
            it->setForeground(QBrush(fg));
        }
    }
}

void ScenarioWizard::highlightRow(int row)
{
    // Убираем подсветку со всех строк (применяем текущий результат)
    for (int i = 0; i < scenario_.steps.size(); ++i)
        setRowResult(i, scenario_.steps[i].result);

    // Подсвечиваем текущий
    if (row >= 0 && row < table_->rowCount()) {
        for (int col = 0; col < COL_COUNT; ++col) {
            if (auto* it = table_->item(row, col))
                it->setBackground(QBrush(COLOR_CURRENT));
        }
        table_->scrollToItem(table_->item(row, 0));
    }
}

void ScenarioWizard::updateCounters()
{
    int passed   = 0;
    int failed   = 0;
    int total    = scenario_.steps.size();

    for (const auto& step : scenario_.steps) {
        if (step.result == StepResult::Ok)   ++passed;
        if (step.result == StepResult::Fail) ++failed;
    }

    int remaining = total - passed - failed;

    lblPassed_->setText(QString("Пройдено: %1").arg(passed));
    lblFailed_->setText(QString("Провалено: %1").arg(failed));
    lblRemaining_->setText(QString("Осталось: %1").arg(remaining));

    progressBar_->setMaximum(total > 0 ? total : 1);
    progressBar_->setValue(passed + failed);
}

// ---------------------------------------------------------------------------
//  Логирование
// ---------------------------------------------------------------------------

void ScenarioWizard::appendLog(const QString& text)
{
    if (logEdit_) {
        auto cursor = logEdit_->textCursor();
        cursor.movePosition(QTextCursor::End);
        logEdit_->setTextCursor(cursor);
        logEdit_->insertPlainText(text + "\n");
    }
}

// ---------------------------------------------------------------------------
//  Автопрогон
// ---------------------------------------------------------------------------

void ScenarioWizard::onRunAuto()
{
    if (scenario_.steps.isEmpty()) {
        QMessageBox::information(this, "Сценарий", "Список шагов пуст. Загрузите или создайте сценарий.");
        return;
    }

    // Сбрасываем результаты
    for (auto& step : scenario_.steps)
        step.result = StepResult::Pending;

    refreshTable();
    updateCounters();
    lblVerdict_->setVisible(false);

    startAutoRun();
}

void ScenarioWizard::startAutoRun()
{
    running_     = true;
    currentStep_ = 0;
    waitingCmd_  = false;

    logEdit_->setPlainText("");
    btnSaveLog_->setVisible(false);

    btnRun_->setEnabled(false);
    btnAddCmd_->setEnabled(false);
    btnAddCheck_->setEnabled(false);
    btnLoad_->setEnabled(false);

    // Первый шаг — через минимальный таймер, чтобы отрисовался UI
    stepTimer_->start(50);
}

void ScenarioWizard::processNextStep()
{
    if (!running_) return;

    // Все шаги пройдены
    if (currentStep_ >= scenario_.steps.size()) {
        finishRun();
        return;
    }

    ScenarioStep& step = scenario_.steps[currentStep_];
    highlightRow(currentStep_);

    if (step.kind == StepKind::Command) {
        appendLog(QString("[КОМАНДА] %1").arg(step.text));

        // Показываем крупную панель инструкции и кнопку «Выполнено»
        lblCmdText_->setText(step.text);
        cmdPanel_->setVisible(true);
        btnDone_->setVisible(true);
        btnDone_->setFocus();
        waitingCmd_ = true;
        // (processNextStep снова будет вызван из onDoneStep)
    } else {
        // Шаг-ПРОВЕРКА: вызываем провайдер
        auto val = provider_(step.address);
        if (!val.has_value()) {
            step.result = StepResult::Fail;
            appendLog(QString("[НЕ ОК] %1: НЕТ ДАННЫХ, допуск=[%2..%3]")
                .arg(step.address)
                .arg(QString::number(step.lo, 'g', 2))
                .arg(QString::number(step.hi, 'g', 2)));
        } else {
            double code = *val;
            step.result = (code >= step.lo && code <= step.hi)
                ? StepResult::Ok
                : StepResult::Fail;

            if (step.result == StepResult::Ok) {
                double margin = qMin(code - step.lo, step.hi - code);
                appendLog(QString("[ОК]    %1: код=%2, допуск=[%3..%4], запас=%5")
                    .arg(step.address)
                    .arg(QString::number(code, 'g', 2))
                    .arg(QString::number(step.lo, 'g', 2))
                    .arg(QString::number(step.hi, 'g', 2))
                    .arg(QString::number(margin, 'g', 2)));
            } else {
                double excess = (code > step.hi) ? (code - step.hi) : (step.lo - code);
                appendLog(QString("[НЕ ОК] %1: код=%2, допуск=[%3..%4], превышение=%5")
                    .arg(step.address)
                    .arg(QString::number(code, 'g', 2))
                    .arg(QString::number(step.lo, 'g', 2))
                    .arg(QString::number(step.hi, 'g', 2))
                    .arg(QString::number(excess, 'g', 2)));
            }
        }

        setRowResult(currentStep_, step.result);
        updateCounters();

        ++currentStep_;
        // Короткая пауза между шагами — 150 мс, чтобы пользователь видел прогресс
        stepTimer_->start(150);
    }
}

void ScenarioWizard::onDoneStep()
{
    if (!running_ || !waitingCmd_) return;
    if (currentStep_ < 0 || currentStep_ >= scenario_.steps.size()) return;

    ScenarioStep& step = scenario_.steps[currentStep_];
    step.result = StepResult::Ok;
    setRowResult(currentStep_, step.result);
    updateCounters();

    waitingCmd_ = false;
    btnDone_->setVisible(false);
    cmdPanel_->setVisible(false);

    ++currentStep_;
    stepTimer_->start(50);
}

void ScenarioWizard::finishRun()
{
    running_     = false;
    currentStep_ = -1;
    waitingCmd_  = false;

    stepTimer_->stop();
    btnDone_->setVisible(false);
    cmdPanel_->setVisible(false);

    btnRun_->setEnabled(true);
    btnAddCmd_->setEnabled(true);
    btnAddCheck_->setEnabled(true);
    btnLoad_->setEnabled(true);

    // Вердикт: ОК если все шаги Ok, иначе НЕ ОК
    bool allOk = std::all_of(scenario_.steps.begin(), scenario_.steps.end(),
        [](const ScenarioStep& s) { return s.result == StepResult::Ok; });

    // Подсчитаем статистику для лога
    int totalChecks = 0, passedChecks = 0, failedChecks = 0, cmdCount = 0;
    for (const auto& step : scenario_.steps) {
        if (step.kind == StepKind::Check) {
            ++totalChecks;
            if (step.result == StepResult::Ok) ++passedChecks;
            if (step.result == StepResult::Fail) ++failedChecks;
        } else {
            ++cmdCount;
        }
    }

    // Добавляем итоговую строку в лог
    QString verdict = allOk ? "ОК" : "НЕ ОК";
    appendLog(QString("=== ВЕРДИКТ: %1 | проверок: %2, ок: %3, не ок: %4, команд: %5 ===")
        .arg(verdict)
        .arg(totalChecks)
        .arg(passedChecks)
        .arg(failedChecks)
        .arg(cmdCount));

    btnSaveLog_->setVisible(true);

    lblVerdict_->setVisible(true);
    if (allOk) {
        lblVerdict_->setText("✔ ИТОГ: ОК");
        lblVerdict_->setStyleSheet(
            QString("color: %1; font-size: 18px; font-weight: bold;").arg(COLOR_OK.name()));
    } else {
        lblVerdict_->setText("✘ ИТОГ: НЕ ОК");
        lblVerdict_->setStyleSheet(
            QString("color: %1; font-size: 18px; font-weight: bold;").arg(COLOR_FAIL.name()));
    }

    // Снимаем подсветку текущей строки
    for (int i = 0; i < scenario_.steps.size(); ++i)
        setRowResult(i, scenario_.steps[i].result);
}

void ScenarioWizard::onReset()
{
    stepTimer_->stop();
    running_     = false;
    currentStep_ = -1;
    waitingCmd_  = false;

    for (auto& step : scenario_.steps)
        step.result = StepResult::Pending;

    cmdPanel_->setVisible(false);
    refreshTable();
    updateCounters();
    lblVerdict_->setVisible(false);
    btnDone_->setVisible(false);

    btnRun_->setEnabled(!scenario_.steps.isEmpty());
    btnAddCmd_->setEnabled(true);
    btnAddCheck_->setEnabled(true);
    btnLoad_->setEnabled(true);
}

// ---------------------------------------------------------------------------
//  Загрузка / Сохранение
// ---------------------------------------------------------------------------

void ScenarioWizard::onLoadScn()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Открыть сценарий", QString(), "Файлы сценариев (*.scn);;Все файлы (*)");
    if (path.isEmpty()) return;

    auto result = loadScenario(path);
    if (!result.has_value()) {
        QMessageBox::critical(this, "Ошибка", "Не удалось загрузить файл:\n" + path);
        return;
    }

    setScenario(*result);
    setWindowTitle(QString("Сценарий: %1 — Орбита-IV").arg(result->name));
}

void ScenarioWizard::onSaveScn()
{
    QString path = QFileDialog::getSaveFileName(
        this, "Сохранить сценарий", scenario_.name + ".scn",
        "Файлы сценариев (*.scn);;Все файлы (*)");
    if (path.isEmpty()) return;

    if (!saveScenario(scenario_, path)) {
        QMessageBox::critical(this, "Ошибка", "Не удалось сохранить файл:\n" + path);
    }
}

// ---------------------------------------------------------------------------
//  Ручное добавление шагов
// ---------------------------------------------------------------------------

void ScenarioWizard::onAddCommand()
{
    bool ok = false;
    QString text = QInputDialog::getText(
        this, "Добавить команду", "Текст инструкции оператору:", QLineEdit::Normal, "", &ok);
    if (!ok || text.trimmed().isEmpty()) return;

    ScenarioStep step;
    step.kind = StepKind::Command;
    step.text = text.trimmed();
    scenario_.steps.append(step);

    refreshTable();
    updateCounters();
    btnRun_->setEnabled(true);
}

void ScenarioWizard::setChannelContext(const std::vector<orbita::ChannelSpec>& specs,
                                        MetadataService* db)
{
    channelSpecs_ = specs;
    metaDb_       = db;
}

void ScenarioWizard::onAddCheck()
{
    QString addr;

    if (!channelSpecs_.empty()) {
        ChannelPickerDialog dlg(channelSpecs_, metaDb_, this);
        if (dlg.exec() != QDialog::Accepted) return;
        addr = dlg.selectedAddress();
        if (addr.isEmpty()) return;
    } else {
        bool ok = false;
        addr = QInputDialog::getText(
            this, "Добавить проверку", "Адрес канала (напр. M16P1A70B12C10D10T01):",
            QLineEdit::Normal, "", &ok);
        if (!ok || addr.trimmed().isEmpty()) return;
        addr = addr.trimmed();
    }

    // Нижняя граница
    bool ok = false;
    double lo = QInputDialog::getDouble(
        this, "Добавить проверку", "Нижняя граница допуска (lo):", 0, -1e9, 1e9, 2, &ok);
    if (!ok) return;

    // Верхняя граница
    double hi = QInputDialog::getDouble(
        this, "Добавить проверку", "Верхняя граница допуска (hi):", 1023, -1e9, 1e9, 2, &ok);
    if (!ok) return;

    ScenarioStep step;
    step.kind    = StepKind::Check;
    step.address = addr;
    step.text    = addr;
    step.lo      = lo;
    step.hi      = hi;
    scenario_.steps.append(step);

    refreshTable();
    updateCounters();
    btnRun_->setEnabled(true);
}

// ---------------------------------------------------------------------------
//  Сохранение лога
// ---------------------------------------------------------------------------

void ScenarioWizard::onSaveLog()
{
    QString path = QFileDialog::getSaveFileName(
        this, "Сохранить лог", "scenario_log.txt",
        "Текстовые файлы (*.txt);;Все файлы (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Ошибка", "Не удалось открыть файл для записи:\n" + path);
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << logEdit_->toPlainText();
    file.close();

    QMessageBox::information(this, "Успех", "Лог успешно сохранён в:\n" + path);
}
