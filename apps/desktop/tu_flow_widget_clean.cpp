#include "tu_flow_widget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
QFrame* panel(QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setProperty("panel", true);
    return frame;
}

QLabel* heading(const QString& text, int pointSize, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setPointSize(pointSize);
    font.setBold(true);
    label->setFont(font);
    return label;
}

QLabel* muted(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("muted", true);
    label->setWordWrap(true);
    return label;
}
}

TuFlowWidget::TuFlowWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("tuFlowWidget"));
    setStyleSheet(QStringLiteral(
        "#tuFlowWidget{background:#08131d;color:#eaf4fb;font-family:'Segoe UI';font-size:14px;}"
        "QFrame[panel='true']{background:#102333;border:1px solid #264257;border-radius:9px;}"
        "QLabel[muted='true']{color:#8ea6b7;}"
        "QPushButton{background:#132a3d;color:#eaf4fb;border:1px solid #264257;border-radius:7px;padding:10px 16px;}"
        "QPushButton:hover{border-color:#58a5ff;background:#17334a;}"
        "QPushButton#primary{background:qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2d7fe9, stop:1 #2266c4); border-color:#4e98f0; font-weight:700;}"
        "QPushButton:disabled{color:#61788a;background:#0e1e2c;border-color:#1a3346; opacity: 0.45;}"
        "QLineEdit{background:#0e1e2c;color:#eaf4fb;border:1px solid #264257;border-radius:6px;padding:10px;min-height:26px;}"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    pages_ = new QStackedWidget(this);
    root->addWidget(pages_);

    selectionPage_ = new QWidget(pages_);
    auto* selection = new QVBoxLayout(selectionPage_);
    selection->setContentsMargins(70, 46, 70, 46);
    selection->setSpacing(18);
    selection->addStretch();

    auto* card = panel(selectionPage_);
    card->setMaximumWidth(760);
    auto* form = new QVBoxLayout(card);
    form->setContentsMargins(34, 30, 34, 30);
    form->setSpacing(13);

    form->addWidget(heading(QStringLiteral("ПРОВЕРКА УБСИ ПО ТУ"), 24, card));
    form->addWidget(muted(
        QStringLiteral("Минимальная поставка: заводской номер, оператор, готовность стенда и полный автоматизированный прогон."),
        card));
    form->addSpacing(8);

    auto* serialCaption = new QLabel(QStringLiteral("Заводской номер УБСИ"), card);
    serialCaption->setStyleSheet(QStringLiteral("color:#b9c9d6;font-weight:700;"));
    form->addWidget(serialCaption);
    serial_ = new QLineEdit(card);
    serial_->setObjectName(QStringLiteral("tuSerialInput"));
    serial_->setPlaceholderText(QStringLiteral("Например: 345"));
    serial_->setClearButtonEnabled(true);
    form->addWidget(serial_);

    auto* operatorCaption = new QLabel(QStringLiteral("ФИО оператора"), card);
    operatorCaption->setStyleSheet(QStringLiteral("color:#b9c9d6;font-weight:700;"));
    form->addWidget(operatorCaption);
    operator_ = new QLineEdit(card);
    operator_->setObjectName(QStringLiteral("tuOperatorInput"));
    operator_->setPlaceholderText(QStringLiteral("Иванов И.И."));
    operator_->setClearButtonEnabled(true);
    form->addWidget(operator_);

    scenarioState_ = muted(QString(), card);
    scenarioState_->setObjectName(QStringLiteral("tuScenarioState"));
    form->addWidget(scenarioState_);

    check_ = new QPushButton(QStringLiteral("ПРОВЕРИТЬ СТЕНД"), card);
    check_->setObjectName(QStringLiteral("primary"));
    check_->setMinimumHeight(46);
    check_->setEnabled(false);
    form->addWidget(check_);

    auto* selectionRow = new QHBoxLayout;
    selectionRow->addStretch();
    selectionRow->addWidget(card, 1);
    selectionRow->addStretch();
    selection->addLayout(selectionRow);
    selection->addStretch();
    pages_->addWidget(selectionPage_);

    readinessPage_ = new QWidget(pages_);
    auto* readiness = new QVBoxLayout(readinessPage_);
    readiness->setContentsMargins(70, 46, 70, 46);
    readiness->setSpacing(18);
    readiness->addStretch();

    auto* readyCard = panel(readinessPage_);
    readyCard->setMaximumWidth(760);
    auto* readyLayout = new QVBoxLayout(readyCard);
    readyLayout->setContentsMargins(34, 30, 34, 30);
    readyLayout->setSpacing(14);

    serialTitle_ = heading(QStringLiteral("УБСИ"), 18, readyCard);
    serialTitle_->setObjectName(QStringLiteral("tuReadySerial"));
    readyLayout->addWidget(serialTitle_);
    operatorTitle_ = muted(QString(), readyCard);
    operatorTitle_->setObjectName(QStringLiteral("tuReadyOperator"));
    readyLayout->addWidget(operatorTitle_);

    readinessState_ = heading(QStringLiteral("ПРОВЕРКА СТЕНДА…"), 28, readyCard);
    readinessState_->setObjectName(QStringLiteral("tuReadyState"));
    readinessState_->setStyleSheet(QStringLiteral("color:#58a5ff;"));
    readyLayout->addSpacing(8);
    readyLayout->addWidget(readinessState_);

    failureDetail_ = muted(QString(), readyCard);
    failureDetail_->setObjectName(QStringLiteral("tuReadyDetail"));
    failureDetail_->hide();
    readyLayout->addWidget(failureDetail_);

    auto* actions = new QHBoxLayout;
    back_ = new QPushButton(QStringLiteral("Изменить данные"), readyCard);
    retry_ = new QPushButton(QStringLiteral("Повторить проверку"), readyCard);
    start_ = new QPushButton(QStringLiteral("НАЧАТЬ ПОЛНЫЙ ПРОГОН"), readyCard);
    start_->setObjectName(QStringLiteral("primary"));
    start_->setMinimumHeight(44);
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

    connect(serial_, &QLineEdit::textChanged, this, [this] { updateSelectionAvailability(); });
    connect(operator_, &QLineEdit::textChanged, this, [this] { updateSelectionAvailability(); });
    connect(check_, &QPushButton::clicked, this, [this] {
        const QString serial = serial_->text().trimmed();
        const QString operatorName = operator_->text().trimmed();
        if (serial.isEmpty() || operatorName.isEmpty() || !scenarioAvailable_) return;
        emit readinessRequested(serial, operatorName);
    });
    connect(start_, &QPushButton::clicked, this, [this] {
        if (activeSerial_.isEmpty() || activeOperator_.isEmpty()) return;
        emit startRequested(activeSerial_, activeOperator_);
    });
    connect(retry_, &QPushButton::clicked, this, [this] {
        if (activeSerial_.isEmpty() || activeOperator_.isEmpty()) return;
        equipmentState_.clear();
        equipmentDetail_.clear();
        for (const auto& code : requiredEquipment_) equipmentState_[code] = -1;
        readinessState_->setText(QStringLiteral("ПРОВЕРКА СТЕНДА…"));
        readinessState_->setStyleSheet(QStringLiteral("color:#58a5ff;"));
        failureDetail_->hide();
        retry_->hide();
        start_->hide();
        emit retryRequested(activeSerial_, activeOperator_);
    });
    connect(back_, &QPushButton::clicked, this, &TuFlowWidget::resetToSelection);

    resetToSelection();
}

void TuFlowWidget::setRegisteredSerials(const QStringList& serials)
{
    Q_UNUSED(serials);
}

void TuFlowWidget::setOperators(const QStringList& operators)
{
    Q_UNUSED(operators);
}

void TuFlowWidget::setScenarioAvailable(bool available, const QString& detail)
{
    scenarioAvailable_ = available;
    serial_->setEnabled(available);
    operator_->setEnabled(available);
    scenarioState_->setText(available ? QString()
                                      : detail.isEmpty()
                                            ? QStringLiteral("Сценарий полного ТУ недоступен")
                                            : detail);
    scenarioState_->setStyleSheet(available ? QString()
                                            : QStringLiteral("color:#ef5a5a;font-weight:700;"));
    updateSelectionAvailability();
}

void TuFlowWidget::beginStandCheck(const QString& serial, const QString& operatorName,
                                   const QStringList& requiredEquipment)
{
    activeSerial_ = serial.trimmed();
    activeOperator_ = operatorName.trimmed();
    if (activeSerial_.isEmpty() || activeOperator_.isEmpty()) return;

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

    serialTitle_->setText(QStringLiteral("УБСИ %1").arg(activeSerial_));
    operatorTitle_->setText(QStringLiteral("Оператор: %1").arg(activeOperator_));
    readinessState_->setText(QStringLiteral("ПРОВЕРКА СТЕНДА…"));
    readinessState_->setStyleSheet(QStringLiteral("color:#58a5ff;"));
    failureDetail_->hide();
    retry_->hide();
    start_->hide();
    back_->show();
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
    requiredEquipment_.clear();
    equipmentState_.clear();
    equipmentDetail_.clear();
    pages_->setCurrentWidget(selectionPage_);
    serial_->setFocus();
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

void TuFlowWidget::updateSelectionAvailability()
{
    check_->setEnabled(scenarioAvailable_
        && !serial_->text().trimmed().isEmpty()
        && !operator_->text().trimmed().isEmpty());
}

void TuFlowWidget::updateReadiness()
{
    bool pending = false;
    QStringList failures;
    for (const auto& code : requiredEquipment_) {
        const int state = equipmentState_.value(code, -1);
        if (state < 0) {
            pending = true;
            continue;
        }
        if (state == 0) {
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
}
