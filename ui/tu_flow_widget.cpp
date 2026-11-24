#include "tu_flow_widget.h"

#include "app/tu_report_writer.h"
#include "model/run_types.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include <exception>

namespace {
QFrame* panel(QWidget* parent)
{
    auto* p = new QFrame(parent);
    p->setProperty("panel", true);
    return p;
}

QLabel* title(const QString& text, int point, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    QFont f = l->font();
    f.setPointSize(point);
    f.setBold(true);
    l->setFont(f);
    return l;
}

QLabel* muted(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setProperty("muted", true);
    l->setWordWrap(true);
    return l;
}

QString verdictText(tu::RunVerdict verdict)
{
    using tu::RunVerdict;
    switch (verdict) {
    case RunVerdict::Ok: return QStringLiteral("НОРМА");
    case RunVerdict::Fail: return QStringLiteral("НЕ НОРМА");
    case RunVerdict::Incomplete: return QStringLiteral("НЕПОЛНАЯ");
    case RunVerdict::Error: return QStringLiteral("ОШИБКА СТЕНДА");
    case RunVerdict::Aborted: return QStringLiteral("ОСТАНОВЛЕНО");
    case RunVerdict::NotRun: return QStringLiteral("НЕ ВЫПОЛНЕНО");
    }
    return QStringLiteral("НЕ ВЫПОЛНЕНО");
}

QColor verdictColor(tu::RunVerdict verdict)
{
    using tu::RunVerdict;
    if (verdict == RunVerdict::Ok) return QColor(QStringLiteral("#158a48"));
    if (verdict == RunVerdict::Fail) return QColor(QStringLiteral("#c53939"));
    return QColor(QStringLiteral("#9a6a12"));
}

}

TuFlowWidget::TuFlowWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("tuFlowWidget"));
    setStyleSheet(QStringLiteral(
        "#tuFlowWidget{background:#08131d;color:#eaf4fb;font-family:'Segoe UI';font-size:13px;}"
        "QFrame[panel='true']{background:#102333;border:1px solid #264257;border-radius:8px;}"
        "QLabel[muted='true']{color:#8ea6b7;}"
        "QPushButton{background:#132a3d;color:#eaf4fb;border:1px solid #264257;border-radius:7px;padding:9px 14px;}"
        "QPushButton:hover{border-color:#58a5ff;background:#17334a;}"
        "QPushButton#primary{background:#2e7de9;border-color:#58a5ff;font-weight:700;}"
        "QPushButton:disabled{color:#61788a;background:#0e1e2c;border-color:#1a3346;}"
        "QLineEdit{background:#0e1e2c;color:#eaf4fb;border:1px solid #264257;border-radius:6px;padding:8px;min-height:22px;}"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    pages_ = new QStackedWidget(this);
    root->addWidget(pages_);

    auto topBar = [](const QString& crumb, QWidget* parent) {
        auto* bar = new QHBoxLayout;
        bar->setContentsMargins(0, 0, 0, 0);
        bar->setSpacing(18);
        auto* heading = new QLabel(QStringLiteral("ПРОВЕРКА ПО ТУ"), parent);
        QFont headingFont = heading->font();
        headingFont.setBold(true);
        heading->setFont(headingFont);
        auto* context = muted(crumb, parent);
        auto* clock = muted(QDateTime::currentDateTime().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")), parent);
        bar->addWidget(heading);
        bar->addWidget(context);
        bar->addStretch();
        bar->addWidget(clock);
        return bar;
    };

    // Оператор указывает только серийный номер; в этой поставке нет production registry.
    selectionPage_ = new QWidget(pages_);
    auto* selection = new QVBoxLayout(selectionPage_);
    selection->setContentsMargins(18, 14, 18, 18);
    selection->setSpacing(14);
    selection->addLayout(topBar(QStringLiteral("проверка изделия"), selectionPage_));

    selection->addWidget(title(QStringLiteral("ПРОВЕРКА БРП ПО ТУ"), 18, selectionPage_));
    selection->addWidget(muted(QStringLiteral("Введите заводской номер БРП и проверьте готовность стенда."),
                                selectionPage_));

    auto* serialCard = panel(selectionPage_);
    serialCard->setObjectName(QStringLiteral("tuSerialEntry"));
    serialCard->setMaximumWidth(620);
    auto* serialLayout = new QVBoxLayout(serialCard);
    serialLayout->setContentsMargins(16, 16, 16, 16);
    serialLayout->setSpacing(12);
    serialLayout->addWidget(title(QStringLiteral("Заводской номер"), 14, serialCard));
    manualSerial_ = new QLineEdit(serialCard);
    manualSerial_->setObjectName(QStringLiteral("tuManualSerial"));
    manualSerial_->setPlaceholderText(QStringLiteral("Номер БРП"));
    manualSerial_->setClearButtonEnabled(true);
    serialLayout->addWidget(manualSerial_);
    selection->addWidget(serialCard, 0, Qt::AlignLeft);
    selection->addStretch();

    auto* footer = new QHBoxLayout;
    auto* manual = new QPushButton(QStringLiteral("Ручное управление"), selectionPage_);
    manual->setObjectName(QStringLiteral("brpManualOpen"));
    connect(manual, &QPushButton::clicked, this, &TuFlowWidget::manualRequested);
    footer->addWidget(manual);
    scenarioState_ = muted(QString(), selectionPage_);
    scenarioState_->setObjectName(QStringLiteral("tuScenarioState"));
    footer->addWidget(scenarioState_, 1);
    check_ = new QPushButton(QStringLiteral("Проверить стенд"), selectionPage_);
    check_->setObjectName(QStringLiteral("primary"));
    check_->setProperty("tuStandAction", true);
    check_->setMinimumWidth(190);
    check_->setMinimumHeight(42);
    footer->addWidget(check_);
    selection->addLayout(footer);
    pages_->addWidget(selectionPage_);

    // TU v0.5 READY: just the serial, stand state and start action.
    readinessPage_ = new QWidget(pages_);
    auto* readiness = new QVBoxLayout(readinessPage_);
    readiness->setContentsMargins(18, 14, 18, 30);
    readiness->setSpacing(14);
    readiness->addLayout(topBar(QStringLiteral("БРП"), readinessPage_));
    readiness->addStretch();

    auto* readyCard = panel(readinessPage_);
    readyCard->setMinimumHeight(320);
    readyCard->setMaximumWidth(760);
    auto* readyLayout = new QVBoxLayout(readyCard);
    readyLayout->setContentsMargins(30, 28, 30, 28);
    readyLayout->setSpacing(15);
    auto* serialRow = new QHBoxLayout;
    serialTitle_ = title(QStringLiteral("БРП"), 17, readyCard);
    serialTitle_->setObjectName(QStringLiteral("tuReadySerial"));
    serialRow->addWidget(serialTitle_);
    serialRow->addStretch();
    readyLayout->addLayout(serialRow);
    readinessState_ = title(QStringLiteral("ПРОВЕРКА СТЕНДА…"), 27, readyCard);
    readinessState_->setObjectName(QStringLiteral("tuReadyState"));
    readyLayout->addWidget(readinessState_);
    failureDetail_ = muted(QString(), readyCard);
    failureDetail_->setObjectName(QStringLiteral("tuReadyDetail"));
    failureDetail_->hide();
    readyLayout->addWidget(failureDetail_);
    auto* actions = new QHBoxLayout;
    back_ = new QPushButton(QStringLiteral("Выбрать другой SN"), readyCard);
    retry_ = new QPushButton(QStringLiteral("Повторить проверку"), readyCard);
    start_ = new QPushButton(QStringLiteral("НАЧАТЬ ПРОВЕРКУ"), readyCard);
    start_->setObjectName(QStringLiteral("primary"));
    start_->setProperty("tuStartAction", true);
    actions->addWidget(back_);
    actions->addStretch();
    actions->addWidget(retry_);
    actions->addWidget(start_);
    readyLayout->addLayout(actions);
    auto* readyRow = new QHBoxLayout;
    readyRow->addStretch();
    readyRow->addWidget(readyCard, 1);
    readyRow->addStretch();
    readiness->addLayout(readyRow);
    readiness->addStretch();
    pages_->addWidget(readinessPage_);

    // TU v0.5 OPERATOR: shown only after the automatic route has completed.
    operatorPage_ = new QWidget(pages_);
    auto* operatorOuter = new QVBoxLayout(operatorPage_);
    operatorOuter->setContentsMargins(18, 14, 18, 30);
    operatorOuter->setSpacing(14);
    operatorOuter->addLayout(topBar(QStringLiteral("БРП · завершено"), operatorPage_));
    operatorOuter->addStretch();
    auto* operatorCard = panel(operatorPage_);
    operatorCard->setMaximumWidth(720);
    auto* operatorLayout = new QVBoxLayout(operatorCard);
    operatorLayout->setContentsMargins(30, 28, 30, 28);
    operatorLayout->setSpacing(14);
    operatorLayout->addWidget(title(QStringLiteral("Проверка завершена"), 24, operatorCard));
    operatorLayout->addWidget(muted(
        QStringLiteral("Введите оператора перед формированием итогового протокола ТУ."), operatorCard));
    auto* operatorCaption = new QLabel(QStringLiteral("Оператор"), operatorCard);
    operatorCaption->setStyleSheet(QStringLiteral("color:#8ea6b7;font-weight:700;"));
    operatorLayout->addWidget(operatorCaption);
    completionOperator_ = new QLineEdit(operatorCard);
    completionOperator_->setObjectName(QStringLiteral("tuCompletionOperator"));
    completionOperator_->setPlaceholderText(QStringLiteral("ФИО"));
    completionOperator_->hide();
    operatorLayout->addWidget(completionOperator_);
    buildReport_ = new QPushButton(QStringLiteral("СФОРМИРОВАТЬ ОТЧЁТ"), operatorCard);
    buildReport_->setObjectName(QStringLiteral("primary"));
    buildReport_->setMinimumHeight(44);
    buildReport_->setEnabled(false);
    buildReport_->setProperty("tuReportAction", true);
    operatorLayout->addWidget(buildReport_);
    reportError_ = muted({}, operatorCard);
    reportError_->setTextFormat(Qt::PlainText);
    reportError_->hide();
    operatorLayout->addWidget(reportError_);
    auto* operatorRow = new QHBoxLayout;
    operatorRow->addStretch();
    operatorRow->addWidget(operatorCard, 1);
    operatorRow->addStretch();
    operatorOuter->addLayout(operatorRow);
    operatorOuter->addStretch();
    pages_->addWidget(operatorPage_);

    // TU v0.5 REPORT: deliberately visually separated from the dark work area.
    reportPage_ = new QWidget(pages_);
    reportPage_->setObjectName(QStringLiteral("tuReportPage"));
    reportPage_->setStyleSheet(QStringLiteral(
        "#tuReportPage{background:#e9edf1;color:#17212a;}"
        "#tuReportCard{background:white;border:1px solid #c7d0d8;border-radius:2px;}"
        "#tuReportCard QLabel{color:#17212a;}"
        "#tuVerdictCard{background:#f2fff6;border:1px solid #96d9ad;border-radius:8px;}"
        "#tuReportTable{background:white;color:#17212a;border:1px solid #c5cbd0;gridline-color:#c5cbd0;}"
        "#tuReportTable::item{padding:8px;}"));
    auto* reportOuter = new QVBoxLayout(reportPage_);
    reportOuter->setContentsMargins(28, 28, 28, 28);
    auto* reportCard = new QFrame(reportPage_);
    reportCard->setObjectName(QStringLiteral("tuReportCard"));
    reportCard->setMinimumWidth(980);
    reportCard->setMaximumWidth(980);
    auto* reportLayout = new QVBoxLayout(reportCard);
    reportLayout->setContentsMargins(50, 42, 50, 42);
    reportLayout->setSpacing(12);
    auto* reportHeading = title(QStringLiteral("ОТЧЁТ"), 36, reportCard);
    reportHeading->setAlignment(Qt::AlignCenter);
    reportLayout->addWidget(reportHeading);
    reportDate_ = new QLabel(reportCard);
    reportDate_->setAlignment(Qt::AlignRight);
    reportDate_->setStyleSheet(QStringLiteral("color:#5c6974;"));
    reportLayout->addWidget(reportDate_);
    reportSerial_ = new QLabel(reportCard);
    reportSerial_->setAlignment(Qt::AlignRight);
    reportOperator_ = new QLabel(reportCard);
    reportOperator_->setAlignment(Qt::AlignRight);
    reportLayout->addWidget(reportSerial_);
    reportLayout->addWidget(reportOperator_);

    auto* verdictCard = new QFrame(reportCard);
    verdictCard->setObjectName(QStringLiteral("tuVerdictCard"));
    auto* verdictLayout = new QVBoxLayout(verdictCard);
    verdictLayout->setContentsMargins(28, 20, 28, 20);
    auto* verdictUnit = title(QStringLiteral("БРП"), 22, verdictCard);
    verdictUnit->setAlignment(Qt::AlignCenter);
    verdictLayout->addWidget(verdictUnit);
    reportVerdict_ = title(QStringLiteral("—"), 34, verdictCard);
    reportVerdict_->setAlignment(Qt::AlignCenter);
    verdictLayout->addWidget(reportVerdict_);
    reportLayout->addWidget(verdictCard);

    reportTable_ = new QTableWidget(0, 2, reportCard);
    reportTable_->setObjectName(QStringLiteral("tuReportTable"));
    reportTable_->horizontalHeader()->hide();
    reportTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    reportTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    reportTable_->setColumnWidth(1, 155);
    reportTable_->verticalHeader()->hide();
    reportTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    reportTable_->setSelectionMode(QAbstractItemView::NoSelection);
    reportTable_->setFocusPolicy(Qt::NoFocus);
    reportTable_->setMinimumHeight(300);
    reportLayout->addWidget(reportTable_, 1);

    reportPaths_ = new QLabel(reportCard);
    reportPaths_->setObjectName(QStringLiteral("tuReportPath"));
    reportPaths_->setTextFormat(Qt::PlainText);
    reportPaths_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    reportPaths_->setWordWrap(true);
    reportPaths_->setStyleSheet(QStringLiteral("color:#5c6974;"));
    reportLayout->addWidget(reportPaths_);
    auto* reportActions = new QHBoxLayout;
    openReport_ = new QPushButton(QStringLiteral("Печать"), reportCard);
    auto* newCheck = new QPushButton(QStringLiteral("Новая проверка"), reportCard);
    auto* reportHome = new QPushButton(QStringLiteral("Вернуться"), reportCard);
    openReport_->setStyleSheet(QStringLiteral("color:#17212a;background:#eef2f5;border:1px solid #b9c4cd;"));
    newCheck->setStyleSheet(QStringLiteral("color:#17212a;background:#eef2f5;border:1px solid #b9c4cd;"));
    reportHome->setStyleSheet(QStringLiteral("color:#17212a;background:#eef2f5;border:1px solid #b9c4cd;"));
    reportActions->addStretch();
    reportActions->addWidget(openReport_);
    reportActions->addWidget(newCheck);
    reportActions->addWidget(reportHome);
    reportActions->addStretch();
    reportLayout->addLayout(reportActions);
    reportOuter->addWidget(reportCard, 1, Qt::AlignHCenter);
    pages_->addWidget(reportPage_);

    connect(reportHome, &QPushButton::clicked, this, &TuFlowWidget::resetToSelection);
    connect(newCheck, &QPushButton::clicked, this, &TuFlowWidget::resetToSelection);
    connect(openReport_, &QPushButton::clicked, this, [this] {
        if (!reportPath_.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(reportPath_));
    });
    connect(manualSerial_, &QLineEdit::textChanged, this, [this](const QString&) {
        updateSelectionAvailability();
    });
    connect(check_, &QPushButton::clicked, this, [this] {
        if (!check_->isEnabled()) return;
        const QString serial = selectedSerial();
        if (!serial.isEmpty()) emit readinessRequested(serial);
    });
    connect(start_, &QPushButton::clicked, this, [this] {
        if (activeSerial_.isEmpty()) return;
        emit startRequested(activeSerial_);
    });
    connect(retry_, &QPushButton::clicked, this, [this] {
        if (activeSerial_.isEmpty()) return;
        equipmentState_.clear();
        equipmentDetail_.clear();
        isReady_ = false;
        isNotReady_ = false;
        readinessState_->setText(QStringLiteral("ПРОВЕРКА СТЕНДА…"));
        readinessState_->setStyleSheet(QStringLiteral("color:#58a5ff;"));
        failureDetail_->hide();
        retry_->hide();
        back_->hide();
        start_->hide();
        emit readinessRequested(activeSerial_);
    });
    connect(back_, &QPushButton::clicked, this, &TuFlowWidget::resetToSelection);
    connect(completionOperator_, &QLineEdit::textChanged, this, [this](const QString& value) {
        buildReport_->setEnabled(!value.trimmed().isEmpty());
    });
    connect(buildReport_, &QPushButton::clicked, this, [this] {
        activeOperator_ = completionOperator_->text().trimmed();
        if (activeOperator_.isEmpty()) return;
        try {
            reportPath_ = writeTuReport(result_, activeOperator_);
        } catch (const std::exception& error) {
            reportError_->setText(QString::fromUtf8(error.what()));
            reportError_->show();
            return;
        }
        finalReportPaths_ = QStringLiteral("Протокол ТУ: %1").arg(reportPath_);
        showReport();
    });

    resetToSelection();
}

void TuFlowWidget::setScenarioAvailable(bool available, const QString& detail)
{
    scenarioAvailable_ = available;
    manualSerial_->setEnabled(available);
    scenarioState_->setText(available ? QString()
                                      : detail.isEmpty() ? QStringLiteral("Проверка по ТУ недоступна") : detail);
    scenarioState_->setStyleSheet(available ? QString()
                                            : QStringLiteral("color:#ef5a5a;font-weight:700;"));
    updateSelectionAvailability();
}

void TuFlowWidget::beginStandCheck(const QString& serial,
                                   const QStringList& requiredEquipment)
{
    activeSerial_ = serial.trimmed();
    activeOperator_.clear();
    if (activeSerial_.isEmpty()) return;

    requiredEquipment_.clear();
    QSet<QString> seen;
    for (const auto& code : requiredEquipment) {
        if (seen.contains(code))
            continue;
        seen.insert(code);
        requiredEquipment_ << code;
    }
    equipmentState_.clear();
    equipmentDetail_.clear();
    isReady_ = false;
    isNotReady_ = false;
    for (const auto& code : requiredEquipment_) equipmentState_[code] = -1;

    serialTitle_->setText(QStringLiteral("БРП SN %1").arg(activeSerial_));
    readinessState_->setText(QStringLiteral("ПРОВЕРКА СТЕНДА…"));
    readinessState_->setStyleSheet(QStringLiteral("color:#58a5ff;"));
    failureDetail_->hide();
    start_->hide();
    retry_->hide();
    back_->hide();
    pages_->setCurrentWidget(readinessPage_);
    if (requiredEquipment_.isEmpty()) showReady();
}

void TuFlowWidget::setEquipmentStatus(const QString& code, bool ready, const QString& detail)
{
    if (!equipmentState_.contains(code)) return;
    equipmentState_[code] = ready ? 1 : 0;
    equipmentDetail_[code] = detail;
    updateReadiness();
}

void TuFlowWidget::resetToSelection()
{
    activeSerial_.clear();
    activeOperator_.clear();
    finalVerdict_.clear();
    finalReportPaths_.clear();
    reportPath_.clear();
    requiredEquipment_.clear();
    equipmentState_.clear();
    equipmentDetail_.clear();
    result_ = {};
    completionOperator_->clear();
    completionOperator_->hide();
    reportTable_->setRowCount(0);
    pages_->setCurrentWidget(selectionPage_);
    updateSelectionAvailability();
}

QString TuFlowWidget::selectedSerial() const
{
    return manualSerial_->text().trimmed();
}

void TuFlowWidget::updateSelectionAvailability()
{
    check_->setEnabled(scenarioAvailable_ && !selectedSerial().isEmpty());
}

void TuFlowWidget::updateReadiness()
{
    bool pending = false;
    QStringList failures;
    for (const auto& code : requiredEquipment_) {
        const int state = equipmentState_.value(code, -1);
        if (state < 0) pending = true;
        else if (state == 0) {
            const QString detail = equipmentDetail_.value(code).trimmed();
            failures << (detail.isEmpty() ? code : QStringLiteral("%1: %2").arg(code, detail));
        }
    }
    if (!failures.isEmpty()) {
        showNotReady(failures.join(QLatin1Char('\n')));
        return;
    }
    if (!pending) showReady();
}

void TuFlowWidget::showReady()
{
    if (isReady_) return;
    isReady_ = true;
    isNotReady_ = false;
    readinessState_->setText(QStringLiteral("СТЕНД ГОТОВ"));
    readinessState_->setStyleSheet(QStringLiteral("color:#35cf79;"));
    failureDetail_->hide();
    retry_->hide();
    back_->hide();
    start_->show();
}

void TuFlowWidget::showNotReady(const QString& detail)
{
    if (isNotReady_) {
        if (failureDetail_->text() == detail) return;
    }
    isNotReady_ = true;
    isReady_ = false;
    readinessState_->setText(QStringLiteral("СТЕНД НЕ ГОТОВ"));
    readinessState_->setStyleSheet(QStringLiteral("color:#ef5a5a;"));
    failureDetail_->setText(detail);
    failureDetail_->show();
    start_->hide();
    retry_->show();
    back_->show();
}

void TuFlowWidget::completeRun(const tu::ScenarioRunResult& result)
{
    result_ = result;
    finalVerdict_ = verdictText(result.verdict);
    populateReportRows(result);
    showOperatorEntry();
}

void TuFlowWidget::showOperatorEntry()
{
    if (finalVerdict_.isEmpty()) finalVerdict_ = QStringLiteral("РЕЗУЛЬТАТ ГОТОВ");
    completionOperator_->clear();
    reportError_->hide();
    completionOperator_->show();
    buildReport_->setEnabled(false);
    pages_->setCurrentWidget(operatorPage_);
    completionOperator_->setFocus();
}

void TuFlowWidget::showReport()
{
    reportDate_->setText(QDateTime::currentDateTime().toString(QStringLiteral("dd.MM.yyyy HH:mm")));
    reportSerial_->setText(result_.scenarioId == "brp.m16.sim"
        ? QStringLiteral("БРП: SN %1 · имитация").arg(activeSerial_)
        : QStringLiteral("БРП: SN %1").arg(activeSerial_));
    reportOperator_->setText(QStringLiteral("Оператор: %1").arg(activeOperator_));
    reportVerdict_->setText(finalVerdict_);
    auto* verdictCard = reportVerdict_->parentWidget();
    if (finalVerdict_ == QStringLiteral("НОРМА")) {
        reportVerdict_->setStyleSheet(QStringLiteral("color:#158a48;font-weight:800;"));
        verdictCard->setStyleSheet(QStringLiteral("background:#f2fff6;border:1px solid #96d9ad;border-radius:8px;"));
    } else if (finalVerdict_ == QStringLiteral("НЕ НОРМА")) {
        reportVerdict_->setStyleSheet(QStringLiteral("color:#c53939;font-weight:800;"));
        verdictCard->setStyleSheet(QStringLiteral("background:#fff4f2;border:1px solid #e0a49b;border-radius:8px;"));
    } else {
        reportVerdict_->setStyleSheet(QStringLiteral("color:#9a6a12;font-weight:800;"));
        verdictCard->setStyleSheet(QStringLiteral("background:#fff9e8;border:1px solid #e3c77c;border-radius:8px;"));
    }
    reportPaths_->setText(finalReportPaths_);
    openReport_->setEnabled(!reportPath_.isEmpty() && QFile::exists(reportPath_));
    pages_->setCurrentWidget(reportPage_);
}

void TuFlowWidget::populateReportRows(const tu::ScenarioRunResult& result)
{
    reportTable_->setRowCount(0);

    for (const auto& step : result.steps) {
        const auto verdict = step.verdict;
        const int row = reportTable_->rowCount();
        reportTable_->insertRow(row);
        auto* requirement = new QTableWidgetItem(step.tuRequirement.empty() ? QString::fromStdString(step.title) : QStringLiteral("ТУ %1 — %2")
            .arg(QString::fromStdString(step.tuRequirement), QString::fromStdString(step.title)));
        auto* status = new QTableWidgetItem(step.operatorSkipped ? QStringLiteral("ПРОПУЩЕНО") : verdictText(verdict));
        requirement->setToolTip(QString::fromStdString(step.message).toHtmlEscaped());
        status->setTextAlignment(Qt::AlignCenter);
        status->setForeground(verdictColor(verdict));
        QFont statusFont = status->font();
        statusFont.setBold(true);
        status->setFont(statusFont);
        reportTable_->setItem(row, 0, requirement);
        reportTable_->setItem(row, 1, status);
        reportTable_->setRowHeight(row, 42);
    }
}
