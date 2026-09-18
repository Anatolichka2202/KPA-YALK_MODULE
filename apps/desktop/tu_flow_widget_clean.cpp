#include "tu_flow_widget.h"

#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QStackedWidget>
#include <QStyle>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

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
        "QPushButton[choiceActive='true']{background:#173b59;border-color:#58a5ff;font-weight:700;}"
        "QPushButton:disabled{color:#61788a;background:#0e1e2c;border-color:#1a3346;}"
        "QComboBox,QLineEdit{background:#0e1e2c;color:#eaf4fb;border:1px solid #264257;border-radius:6px;padding:8px;min-height:22px;}"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    pages_ = new QStackedWidget(this);
    root->addWidget(pages_);

    // TU v0.5 ENTRY: product identity first, no operator.
    selectionPage_ = new QWidget(pages_);
    auto* selection = new QVBoxLayout(selectionPage_);
    selection->setContentsMargins(62, 38, 62, 34);
    selection->setSpacing(18);

    auto* top = new QHBoxLayout;
    auto* home = new QPushButton(QStringLiteral("← Главная"), selectionPage_);
    home->setObjectName(QStringLiteral("tuHomeButton"));
    top->addWidget(home);
    top->addStretch();
    selection->addLayout(top);

    selection->addWidget(title(QStringLiteral("ПРОВЕРКА ПО ТУ"), 23, selectionPage_));
    selection->addWidget(muted(
        QStringLiteral("Один утверждённый маршрут. Выберите зарегистрированное УБСИ или введите заводской номер вручную."),
        selectionPage_));

    auto* choices = new QHBoxLayout;
    choices->setSpacing(16);

    auto* registryCard = panel(selectionPage_);
    registryCard->setObjectName(QStringLiteral("tuRegistryChoice"));
    auto* registryLayout = new QVBoxLayout(registryCard);
    registryLayout->setContentsMargins(22, 20, 22, 20);
    registryLayout->setSpacing(12);
    registryLayout->addWidget(title(QStringLiteral("Зарегистрированное УБСИ"), 16, registryCard));
    registryLayout->addWidget(muted(
        QStringLiteral("Используется существующая запись регистратора. Производственная сессия не создаётся."),
        registryCard));
    registered_ = new QComboBox(registryCard);
    registered_->setObjectName(QStringLiteral("tuRegisteredProducts"));
    registered_->addItem(QStringLiteral("Выберите УБСИ"), QString());
    registryLayout->addWidget(registered_);
    useRegistered_ = new QPushButton(QStringLiteral("Использовать выбранное"), registryCard);
    useRegistered_->setObjectName(QStringLiteral("tuUseRegistered"));
    registryLayout->addWidget(useRegistered_);
    registryLayout->addStretch();
    choices->addWidget(registryCard, 1);

    auto* manualCard = panel(selectionPage_);
    manualCard->setObjectName(QStringLiteral("tuManualChoice"));
    auto* manualLayout = new QVBoxLayout(manualCard);
    manualLayout->setContentsMargins(22, 20, 22, 20);
    manualLayout->setSpacing(12);
    manualLayout->addWidget(title(QStringLiteral("SN вручную"), 16, manualCard));
    manualLayout->addWidget(muted(
        QStringLiteral("Для приёмо-сдаточной проверки заводской номер можно ввести без предварительной регистрации изделия."),
        manualCard));
    manualSerial_ = new QLineEdit(manualCard);
    manualSerial_->setObjectName(QStringLiteral("tuManualSerial"));
    manualSerial_->setPlaceholderText(QStringLiteral("Введите SN…"));
    manualSerial_->setClearButtonEnabled(true);
    manualLayout->addWidget(manualSerial_);
    useManual_ = new QPushButton(QStringLiteral("Использовать введённый SN"), manualCard);
    useManual_->setObjectName(QStringLiteral("tuUseManual"));
    manualLayout->addWidget(useManual_);
    manualLayout->addStretch();
    choices->addWidget(manualCard, 1);
    selection->addLayout(choices, 1);

    auto* footer = new QHBoxLayout;
    scenarioState_ = muted(QString(), selectionPage_);
    scenarioState_->setObjectName(QStringLiteral("tuScenarioState"));
    footer->addWidget(scenarioState_, 1);
    check_ = new QPushButton(QStringLiteral("Проверить стенд"), selectionPage_);
    check_->setObjectName(QStringLiteral("primary"));
    check_->setMinimumWidth(190);
    check_->setMinimumHeight(42);
    footer->addWidget(check_);
    selection->addLayout(footer);
    pages_->addWidget(selectionPage_);

    // TU v0.5 READY: just the serial, stand state and start action.
    readinessPage_ = new QWidget(pages_);
    auto* readiness = new QVBoxLayout(readinessPage_);
    readiness->setContentsMargins(62, 38, 62, 38);
    readiness->setSpacing(18);
    auto* readyTop = new QHBoxLayout;
    auto* readyHome = new QPushButton(QStringLiteral("← Главная"), readinessPage_);
    readyTop->addWidget(readyHome);
    readyTop->addStretch();
    readiness->addLayout(readyTop);
    readiness->addStretch();

    auto* readyCard = panel(readinessPage_);
    readyCard->setMaximumWidth(820);
    auto* readyLayout = new QVBoxLayout(readyCard);
    readyLayout->setContentsMargins(30, 28, 30, 28);
    readyLayout->setSpacing(15);
    serialTitle_ = title(QStringLiteral("УБСИ"), 17, readyCard);
    serialTitle_->setObjectName(QStringLiteral("tuReadySerial"));
    readyLayout->addWidget(serialTitle_);
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
    operatorOuter->setContentsMargins(62, 38, 62, 38);
    operatorOuter->addStretch();
    auto* operatorCard = panel(operatorPage_);
    operatorCard->setMaximumWidth(700);
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
    operatorLayout->addWidget(completionOperator_);
    buildReport_ = new QPushButton(QStringLiteral("СФОРМИРОВАТЬ ОТЧЁТ"), operatorCard);
    buildReport_->setObjectName(QStringLiteral("primary"));
    buildReport_->setMinimumHeight(44);
    buildReport_->setEnabled(false);
    operatorLayout->addWidget(buildReport_);
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
        "#tuReportCard{background:white;border:1px solid #c7d0d8;border-radius:8px;}"
        "#tuReportCard QLabel{color:#17212a;}"));
    auto* reportOuter = new QVBoxLayout(reportPage_);
    reportOuter->setContentsMargins(70, 44, 70, 44);
    auto* reportCard = new QFrame(reportPage_);
    reportCard->setObjectName(QStringLiteral("tuReportCard"));
    auto* reportLayout = new QVBoxLayout(reportCard);
    reportLayout->setContentsMargins(34, 30, 34, 30);
    reportLayout->setSpacing(12);
    reportLayout->addWidget(title(QStringLiteral("ОТЧЁТ"), 25, reportCard));
    auto* date = new QLabel(QDateTime::currentDateTime().toString(QStringLiteral("dd.MM.yyyy HH:mm")), reportCard);
    date->setStyleSheet(QStringLiteral("color:#5c6974;"));
    reportLayout->addWidget(date);
    reportSerial_ = new QLabel(reportCard);
    reportOperator_ = new QLabel(reportCard);
    reportLayout->addWidget(reportSerial_);
    reportLayout->addWidget(reportOperator_);
    reportVerdict_ = title(QStringLiteral("—"), 28, reportCard);
    reportVerdict_->setAlignment(Qt::AlignCenter);
    reportLayout->addSpacing(12);
    reportLayout->addWidget(reportVerdict_);
    reportPaths_ = new QLabel(reportCard);
    reportPaths_->setWordWrap(true);
    reportPaths_->setStyleSheet(QStringLiteral("color:#5c6974;"));
    reportLayout->addWidget(reportPaths_);
    auto* reportActions = new QHBoxLayout;
    auto* newCheck = new QPushButton(QStringLiteral("Новая проверка"), reportCard);
    auto* reportHome = new QPushButton(QStringLiteral("Главная"), reportCard);
    newCheck->setStyleSheet(QStringLiteral("color:#17212a;background:#eef2f5;border:1px solid #b9c4cd;"));
    reportHome->setStyleSheet(QStringLiteral("color:#17212a;background:#eef2f5;border:1px solid #b9c4cd;"));
    reportActions->addStretch();
    reportActions->addWidget(newCheck);
    reportActions->addWidget(reportHome);
    reportLayout->addLayout(reportActions);
    reportOuter->addWidget(reportCard, 1);
    pages_->addWidget(reportPage_);

    connect(home, &QPushButton::clicked, this, &TuFlowWidget::homeRequested);
    connect(readyHome, &QPushButton::clicked, this, &TuFlowWidget::homeRequested);
    connect(reportHome, &QPushButton::clicked, this, &TuFlowWidget::homeRequested);
    connect(newCheck, &QPushButton::clicked, this, &TuFlowWidget::resetToSelection);
    connect(registered_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                if (!registered_->currentData().toString().trimmed().isEmpty()) setSerialMode(false);
                updateSelectionAvailability();
            });
    connect(manualSerial_, &QLineEdit::textChanged, this, [this](const QString&) {
        if (!manualSerial_->text().trimmed().isEmpty()) setSerialMode(true);
        updateSelectionAvailability();
    });
    connect(useRegistered_, &QPushButton::clicked, this, [this] {
        setSerialMode(false);
        updateSelectionAvailability();
    });
    connect(useManual_, &QPushButton::clicked, this, [this] {
        setSerialMode(true);
        updateSelectionAvailability();
    });
    connect(check_, &QPushButton::clicked, this, [this] {
        if (!check_->isEnabled()) return;
        const QString serial = selectedSerial();
        if (!serial.isEmpty()) emit readinessRequested(serial, QString());
    });
    connect(start_, &QPushButton::clicked, this, [this] {
        if (activeSerial_.isEmpty()) return;
        runStarted_ = true;
        completionShown_ = false;
        emit startRequested(activeSerial_, QString());
    });
    connect(retry_, &QPushButton::clicked, this, [this] {
        if (activeSerial_.isEmpty()) return;
        equipmentState_.clear();
        equipmentDetail_.clear();
        readinessState_->setText(QStringLiteral("ПРОВЕРКА СТЕНДА…"));
        readinessState_->setStyleSheet(QStringLiteral("color:#58a5ff;"));
        failureDetail_->hide();
        retry_->hide();
        back_->hide();
        start_->hide();
        emit retryRequested(activeSerial_, QString());
    });
    connect(back_, &QPushButton::clicked, this, &TuFlowWidget::resetToSelection);
    connect(completionOperator_, &QLineEdit::textChanged, this, [this](const QString& value) {
        buildReport_->setEnabled(!value.trimmed().isEmpty());
    });
    connect(buildReport_, &QPushButton::clicked, this, [this] {
        activeOperator_ = completionOperator_->text().trimmed();
        if (activeOperator_.isEmpty()) return;
        applyOperatorToTuProtocol(activeOperator_);
        showReport();
    });

    setSerialMode(false);
    resetToSelection();
    hookRuntimeFinish();
}

void TuFlowWidget::setRegisteredSerials(const QStringList& serials)
{
    const QString selected = registered_->currentData().toString();
    QStringList unique = serials;
    unique.removeDuplicates();
    unique.sort(Qt::CaseInsensitive);
    registered_->blockSignals(true);
    registered_->clear();
    registered_->addItem(QStringLiteral("Выберите УБСИ"), QString());
    for (const auto& serial : unique)
        registered_->addItem(QStringLiteral("SN %1").arg(serial), serial);
    const int index = registered_->findData(selected);
    registered_->setCurrentIndex(index >= 0 ? index : 0);
    registered_->blockSignals(false);
    updateSelectionAvailability();
}

void TuFlowWidget::setOperators(const QStringList& operators)
{
    Q_UNUSED(operators);
}

void TuFlowWidget::setScenarioAvailable(bool available, const QString& detail)
{
    scenarioAvailable_ = available;
    registered_->setEnabled(available);
    manualSerial_->setEnabled(available);
    useRegistered_->setEnabled(available);
    useManual_->setEnabled(available);
    scenarioState_->setText(available ? QString()
                                      : detail.isEmpty() ? QStringLiteral("Проверка по ТУ недоступна") : detail);
    scenarioState_->setStyleSheet(available ? QString()
                                            : QStringLiteral("color:#ef5a5a;font-weight:700;"));
    updateSelectionAvailability();
}

void TuFlowWidget::beginStandCheck(const QString& serial, const QString& operatorName,
                                   const QStringList& requiredEquipment)
{
    Q_UNUSED(operatorName);
    activeSerial_ = serial.trimmed();
    activeOperator_.clear();
    if (activeSerial_.isEmpty()) return;

    requiredEquipment_.clear();
    QSet<QString> seen;
    for (const auto& code : requiredEquipment) {
        if (code == QStringLiteral("R4831") || code == QStringLiteral("SCHEME") || seen.contains(code))
            continue;
        seen.insert(code);
        requiredEquipment_ << code;
    }
    equipmentState_.clear();
    equipmentDetail_.clear();
    for (const auto& code : requiredEquipment_) equipmentState_[code] = -1;

    serialTitle_->setText(QStringLiteral("УБСИ SN %1").arg(activeSerial_));
    readinessState_->setText(QStringLiteral("ПРОВЕРКА СТЕНДА…"));
    readinessState_->setStyleSheet(QStringLiteral("color:#58a5ff;"));
    failureDetail_->hide();
    start_->hide();
    retry_->hide();
    back_->hide();
    pages_->setCurrentWidget(readinessPage_);
    if (requiredEquipment_.isEmpty()) showReady();
}

void TuFlowWidget::setEquipmentChecking(const QString& code)
{
    if (!equipmentState_.contains(code)) return;
    equipmentState_[code] = -1;
    equipmentDetail_.remove(code);
    updateReadiness();
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
    requiredEquipment_.clear();
    equipmentState_.clear();
    equipmentDetail_.clear();
    runStarted_ = false;
    completionShown_ = false;
    completionOperator_->clear();
    pages_->setCurrentWidget(selectionPage_);
    updateSelectionAvailability();
}

QString TuFlowWidget::activeSerial() const
{
    return activeSerial_;
}

QString TuFlowWidget::activeOperator() const
{
    return activeOperator_;
}

QString TuFlowWidget::selectedSerial() const
{
    return manualMode_ ? manualSerial_->text().trimmed()
                       : registered_->currentData().toString().trimmed();
}

void TuFlowWidget::setSerialMode(bool manual)
{
    manualMode_ = manual;
    useManual_->setProperty("choiceActive", manualMode_);
    useRegistered_->setProperty("choiceActive", !manualMode_);
    for (auto* button : {useManual_, useRegistered_}) {
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
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
    readinessState_->setText(QStringLiteral("СТЕНД ГОТОВ"));
    readinessState_->setStyleSheet(QStringLiteral("color:#35cf79;"));
    failureDetail_->hide();
    retry_->hide();
    back_->hide();
    start_->show();
}

void TuFlowWidget::showNotReady(const QString& detail)
{
    readinessState_->setText(QStringLiteral("СТЕНД НЕ ГОТОВ"));
    readinessState_->setStyleSheet(QStringLiteral("color:#ef5a5a;"));
    failureDetail_->setText(detail);
    failureDetail_->show();
    start_->hide();
    retry_->show();
    back_->show();
}

void TuFlowWidget::hookRuntimeFinish()
{
    auto* outer = qobject_cast<QStackedWidget*>(parentWidget());
    auto* host = outer ? outer->parentWidget() : nullptr;
    if (!outer || !host) return;

    QStackedWidget* stages = nullptr;
    for (auto* stack : host->findChildren<QStackedWidget*>()) {
        if (stack != outer && stack != pages_ && stack->count() >= 9) {
            stages = stack;
            break;
        }
    }
    if (!stages) return;

    connect(stages, &QStackedWidget::currentChanged, this, [this, outer, stages](int index) {
        if (index != stages->count() - 1 || !runStarted_ || completionShown_ || activeSerial_.isEmpty()) return;
        completionShown_ = true;
        QTimer::singleShot(0, this, [this, outer] {
            showOperatorEntry();
            outer->setCurrentWidget(this);
        });
    });
}

void TuFlowWidget::showOperatorEntry()
{
    auto* host = parentWidget() ? parentWidget()->parentWidget() : nullptr;
    finalReportPaths_.clear();
    finalVerdict_.clear();
    if (host) {
        if (auto* paths = host->findChild<QLabel*>(QStringLiteral("finishReportPaths")))
            finalReportPaths_ = paths->text();
        static const QStringList verdicts = {
            QStringLiteral("НОРМА"), QStringLiteral("НЕ НОРМА"),
            QStringLiteral("ОШИБКА СТЕНДА"), QStringLiteral("ОШИБКА"),
            QStringLiteral("НЕПОЛНАЯ"), QStringLiteral("ОСТАНОВЛЕНО")};
        for (auto* label : host->findChildren<QLabel*>()) {
            if (verdicts.contains(label->text().trimmed())) {
                finalVerdict_ = label->text().trimmed();
                break;
            }
        }
    }
    if (finalVerdict_.isEmpty()) finalVerdict_ = QStringLiteral("РЕЗУЛЬТАТ ГОТОВ");
    completionOperator_->clear();
    buildReport_->setEnabled(false);
    pages_->setCurrentWidget(operatorPage_);
    completionOperator_->setFocus();
}

void TuFlowWidget::showReport()
{
    reportSerial_->setText(QStringLiteral("УБСИ: SN %1").arg(activeSerial_));
    reportOperator_->setText(QStringLiteral("Оператор: %1").arg(activeOperator_));
    reportVerdict_->setText(finalVerdict_);
    if (finalVerdict_ == QStringLiteral("НОРМА"))
        reportVerdict_->setStyleSheet(QStringLiteral("color:#158a48;font-weight:800;"));
    else if (finalVerdict_ == QStringLiteral("НЕ НОРМА"))
        reportVerdict_->setStyleSheet(QStringLiteral("color:#c53939;font-weight:800;"));
    else
        reportVerdict_->setStyleSheet(QStringLiteral("color:#9a6a12;font-weight:800;"));
    reportPaths_->setText(finalReportPaths_);
    pages_->setCurrentWidget(reportPage_);
}

void TuFlowWidget::applyOperatorToTuProtocol(const QString& operatorName)
{
    const QString prefix = QStringLiteral("Протокол ТУ: ");
    QString path;
    for (const auto& line : finalReportPaths_.split(QLatin1Char('\n'))) {
        if (line.startsWith(prefix)) {
            path = line.mid(prefix.size()).trimmed();
            break;
        }
    }
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString html = QString::fromUtf8(file.readAll());
    file.close();

    const QString marker = QStringLiteral("<tr><th>Оператор</th><td>");
    const qsizetype begin = html.indexOf(marker);
    if (begin < 0) return;
    const qsizetype valueBegin = begin + marker.size();
    const qsizetype valueEnd = html.indexOf(QStringLiteral("</td></tr>"), valueBegin);
    if (valueEnd < 0) return;
    html.replace(valueBegin, valueEnd - valueBegin, operatorName.toHtmlEscaped());

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    output.write(html.toUtf8());
    output.commit();
}
