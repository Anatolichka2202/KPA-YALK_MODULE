#include "tu_flow_widget.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QLabel* centeredLabel(const QString& text, int pointSize, bool bold = false)
{
    auto* label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    QFont font = label->font();
    font.setPointSize(pointSize);
    font.setBold(bold);
    label->setFont(font);
    label->setWordWrap(true);
    return label;
}
}

TuFlowWidget::TuFlowWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("tuFlowWidget"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    pages_ = new QStackedWidget(this);
    root->addWidget(pages_);

    selectionPage_ = new QWidget(pages_);
    auto* selection = new QVBoxLayout(selectionPage_);
    selection->setContentsMargins(80, 55, 80, 55);
    selection->setSpacing(18);

    auto* nav = new QHBoxLayout;
    auto* home = new QPushButton(QStringLiteral("← КТМА"), selectionPage_);
    home->setObjectName(QStringLiteral("tuHomeButton"));
    nav->addWidget(home);
    nav->addStretch();
    selection->addLayout(nav);

    selection->addStretch();
    auto* title = centeredLabel(QStringLiteral("Проверка УБСИ по ТУ"), 25, true);
    title->setObjectName(QStringLiteral("tuSelectionTitle"));
    selection->addWidget(title);

    auto* card = new QFrame(selectionPage_);
    card->setObjectName(QStringLiteral("panel"));
    card->setMaximumWidth(720);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 22, 24, 22);
    cardLayout->setSpacing(12);

    auto* registryCaption = new QLabel(QStringLiteral("Зарегистрированное УБСИ"), card);
    registered_ = new QComboBox(card);
    registered_->setObjectName(QStringLiteral("tuRegisteredProducts"));
    registered_->addItem(QStringLiteral("Выберите УБСИ"), QString());
    cardLayout->addWidget(registryCaption);
    cardLayout->addWidget(registered_);

    auto* orLabel = new QLabel(QStringLiteral("или введите заводской номер вручную"), card);
    orLabel->setAlignment(Qt::AlignCenter);
    orLabel->setObjectName(QStringLiteral("muted"));
    cardLayout->addWidget(orLabel);

    auto* manual = new QHBoxLayout;
    manualSerial_ = new QLineEdit(card);
    manualSerial_->setObjectName(QStringLiteral("tuManualSerial"));
    manualSerial_->setPlaceholderText(QStringLiteral("SN УБСИ"));
    manualSerial_->setClearButtonEnabled(true);
    manualContinue_ = new QPushButton(QStringLiteral("Продолжить"), card);
    manualContinue_->setObjectName(QStringLiteral("primary"));
    manual->addWidget(manualSerial_, 1);
    manual->addWidget(manualContinue_);
    cardLayout->addLayout(manual);

    scenarioState_ = new QLabel(card);
    scenarioState_->setAlignment(Qt::AlignCenter);
    scenarioState_->setObjectName(QStringLiteral("muted"));
    cardLayout->addWidget(scenarioState_);

    auto* cardRow = new QHBoxLayout;
    cardRow->addStretch();
    cardRow->addWidget(card, 1);
    cardRow->addStretch();
    selection->addLayout(cardRow);
    selection->addStretch(2);
    pages_->addWidget(selectionPage_);

    readinessPage_ = new QWidget(pages_);
    auto* readiness = new QVBoxLayout(readinessPage_);
    readiness->setContentsMargins(90, 70, 90, 70);
    readiness->setSpacing(22);
    readiness->addStretch();

    serialTitle_ = centeredLabel(QStringLiteral("УБСИ"), 20, true);
    serialTitle_->setObjectName(QStringLiteral("tuReadySerial"));
    readiness->addWidget(serialTitle_);

    readinessState_ = centeredLabel(QStringLiteral("ПРОВЕРКА СТЕНДА…"), 32, true);
    readinessState_->setObjectName(QStringLiteral("tuReadyState"));
    readiness->addWidget(readinessState_);

    failureDetail_ = centeredLabel(QString(), 11, false);
    failureDetail_->setObjectName(QStringLiteral("tuReadyDetail"));
    failureDetail_->setVisible(false);
    readiness->addWidget(failureDetail_);

    auto* actions = new QHBoxLayout;
    actions->addStretch();
    start_ = new QPushButton(QStringLiteral("НАЧАТЬ ПРОВЕРКУ"), readinessPage_);
    start_->setObjectName(QStringLiteral("primary"));
    start_->setMinimumSize(260, 54);
    start_->setVisible(false);
    retry_ = new QPushButton(QStringLiteral("Повторить проверку стенда"), readinessPage_);
    retry_->setVisible(false);
    back_ = new QPushButton(QStringLiteral("Выбрать другое УБСИ"), readinessPage_);
    back_->setVisible(false);
    actions->addWidget(back_);
    actions->addWidget(retry_);
    actions->addWidget(start_);
    actions->addStretch();
    readiness->addLayout(actions);
    readiness->addStretch();
    pages_->addWidget(readinessPage_);

    connect(home, &QPushButton::clicked, this, &TuFlowWidget::homeRequested);
    connect(registered_, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        chooseSerial(registered_->itemData(index).toString());
    });
    connect(manualContinue_, &QPushButton::clicked, this, [this] {
        chooseSerial(manualSerial_->text());
    });
    connect(manualSerial_, &QLineEdit::returnPressed, this, [this] {
        chooseSerial(manualSerial_->text());
    });
    connect(start_, &QPushButton::clicked, this, [this] {
        if (!activeSerial_.isEmpty()) emit startRequested(activeSerial_);
    });
    connect(retry_, &QPushButton::clicked, this, [this] {
        if (activeSerial_.isEmpty()) return;
        equipmentState_.clear();
        equipmentDetail_.clear();
        readinessState_->setText(QStringLiteral("ПРОВЕРКА СТЕНДА…"));
        readinessState_->setStyleSheet(QStringLiteral("color:#9ac7ff;"));
        failureDetail_->hide();
        retry_->hide();
        back_->hide();
        start_->hide();
        emit retryRequested(activeSerial_);
    });
    connect(back_, &QPushButton::clicked, this, &TuFlowWidget::resetToSelection);

    resetToSelection();
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
    for (const auto& serial : unique) registered_->addItem(serial, serial);
    const int previous = registered_->findData(selected);
    registered_->setCurrentIndex(previous >= 0 ? previous : 0);
    registered_->blockSignals(false);
}

void TuFlowWidget::setScenarioAvailable(bool available, const QString& detail)
{
    scenarioAvailable_ = available;
    registered_->setEnabled(available);
    manualSerial_->setEnabled(available);
    manualContinue_->setEnabled(available);
    scenarioState_->setText(available ? QString() :
        (detail.isEmpty() ? QStringLiteral("Проверка по ТУ сейчас недоступна") : detail));
    scenarioState_->setStyleSheet(available
        ? QString() : QStringLiteral("color:#e1766d;font-weight:700;"));
}

void TuFlowWidget::beginStandCheck(const QString& serial, const QStringList& requiredEquipment)
{
    activeSerial_ = serial.trimmed();
    if (activeSerial_.isEmpty()) return;

    requiredEquipment_.clear();
    QSet<QString> seen;
    for (const auto& code : requiredEquipment) {
        if (code == QStringLiteral("R4831") || code == QStringLiteral("SCHEME")) continue;
        if (seen.contains(code)) continue;
        seen.insert(code);
        requiredEquipment_ << code;
    }

    equipmentState_.clear();
    equipmentDetail_.clear();
    for (const auto& code : requiredEquipment_) equipmentState_.insert(code, -1);

    serialTitle_->setText(QStringLiteral("УБСИ SN %1").arg(activeSerial_));
    readinessState_->setText(QStringLiteral("ПРОВЕРКА СТЕНДА…"));
    readinessState_->setStyleSheet(QStringLiteral("color:#9ac7ff;"));
    failureDetail_->clear();
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
    requiredEquipment_.clear();
    equipmentState_.clear();
    equipmentDetail_.clear();
    manualSerial_->clear();
    registered_->setCurrentIndex(0);
    pages_->setCurrentWidget(selectionPage_);
}

QString TuFlowWidget::activeSerial() const
{
    return activeSerial_;
}

void TuFlowWidget::chooseSerial(const QString& serial)
{
    if (!scenarioAvailable_) return;
    const QString normalized = serial.trimmed();
    if (normalized.isEmpty()) return;
    activeSerial_ = normalized;
    emit serialChosen(normalized);
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
        showNotReady(failures.join(QStringLiteral("\n")));
        return;
    }
    if (!pending) showReady();
}

void TuFlowWidget::showReady()
{
    readinessState_->setText(QStringLiteral("СТЕНД ГОТОВ"));
    readinessState_->setStyleSheet(QStringLiteral("color:#70d79b;"));
    failureDetail_->hide();
    retry_->hide();
    back_->hide();
    start_->show();
}

void TuFlowWidget::showNotReady(const QString& detail)
{
    readinessState_->setText(QStringLiteral("СТЕНД НЕ ГОТОВ"));
    readinessState_->setStyleSheet(QStringLiteral("color:#e1766d;"));
    failureDetail_->setText(detail);
    failureDetail_->show();
    start_->hide();
    retry_->show();
    back_->show();
}
