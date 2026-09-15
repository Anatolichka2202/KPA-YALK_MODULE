#include "tu_flow_widget.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
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

bool validOperatorName(const QString& value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[А-ЯЁ][а-яё-]+\\s[А-ЯЁ]\\.[А-ЯЁ]\\.$"));
    return pattern.match(value.trimmed()).hasMatch();
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

    auto* operatorCaption = new QLabel(QStringLiteral("Оператор"), card);
    cardLayout->addWidget(operatorCaption);
    auto* operatorRow = new QHBoxLayout;
    operator_ = new QComboBox(card);
    operator_->setObjectName(QStringLiteral("tuOperator"));
    operator_->addItem(QStringLiteral("Выберите оператора"), QString());
    auto* addOperator = new QPushButton(QStringLiteral("+"), card);
    addOperator->setObjectName(QStringLiteral("tuAddOperator"));
    addOperator->setFixedWidth(42);
    operatorRow->addWidget(operator_, 1);
    operatorRow->addWidget(addOperator);
    cardLayout->addLayout(operatorRow);

    auto* registryCaption = new QLabel(QStringLiteral("Зарегистрированное УБСИ"), card);
    registered_ = new QComboBox(card);
    registered_->setObjectName(QStringLiteral("tuRegisteredProducts"));
    registered_->addItem(QStringLiteral("Выберите УБСИ"), QString());
    cardLayout->addWidget(registryCaption);
    cardLayout->addWidget(registered_);

    auto* note = new QLabel(
        QStringLiteral("Выбор изделия не обращается к оборудованию. Проверка стенда начинается только по кнопке ниже."),
        card);
    note->setWordWrap(true);
    note->setObjectName(QStringLiteral("muted"));
    cardLayout->addWidget(note);

    check_ = new QPushButton(QStringLiteral("Проверить готовность"), card);
    check_->setObjectName(QStringLiteral("primary"));
    check_->setMinimumHeight(44);
    cardLayout->addWidget(check_);

    scenarioState_ = new QLabel(card);
    scenarioState_->setAlignment(Qt::AlignCenter);
    scenarioState_->setWordWrap(true);
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
    connect(operator_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateSelectionAvailability(); });
    connect(registered_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateSelectionAvailability(); });
    connect(addOperator, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString value = QInputDialog::getText(
            this, QStringLiteral("Новый оператор"),
            QStringLiteral("ФИО в формате «Толмачёв А.Е.»"),
            QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || value.isEmpty()) return;
        if (!validOperatorName(value)) {
            QMessageBox::warning(this, QStringLiteral("Оператор"),
                QStringLiteral("Используйте формат: Фамилия И.О., например «Толмачёв А.Е.»"));
            return;
        }
        int index = operator_->findText(value);
        if (index < 0) {
            operator_->addItem(value, value);
            index = operator_->count() - 1;
        }
        operator_->setCurrentIndex(index);
    });
    connect(check_, &QPushButton::clicked, this, [this] {
        if (!check_->isEnabled()) return;
        const QString serial = registered_->currentData().toString().trimmed();
        const QString operatorName = operator_->currentData().toString().trimmed();
        if (!serial.isEmpty() && !operatorName.isEmpty())
            emit readinessRequested(serial, operatorName);
    });
    connect(start_, &QPushButton::clicked, this, [this] {
        if (!activeSerial_.isEmpty() && !activeOperator_.isEmpty())
            emit startRequested(activeSerial_, activeOperator_);
    });
    connect(retry_, &QPushButton::clicked, this, [this] {
        if (activeSerial_.isEmpty() || activeOperator_.isEmpty()) return;
        equipmentState_.clear();
        equipmentDetail_.clear();
        readinessState_->setText(QStringLiteral("ПРОВЕРКА СТЕНДА…"));
        readinessState_->setStyleSheet(QStringLiteral("color:#9ac7ff;"));
        failureDetail_->hide();
        retry_->hide();
        back_->hide();
        start_->hide();
        emit retryRequested(activeSerial_, activeOperator_);
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
    updateSelectionAvailability();
}

void TuFlowWidget::setOperators(const QStringList& operators)
{
    const QString selected = operator_->currentData().toString();
    QStringList unique = operators;
    unique.removeAll(QString());
    unique.removeDuplicates();
    unique.sort(Qt::CaseInsensitive);

    operator_->blockSignals(true);
    operator_->clear();
    operator_->addItem(QStringLiteral("Выберите оператора"), QString());
    for (const auto& value : unique) operator_->addItem(value, value);
    const int previous = operator_->findData(selected);
    operator_->setCurrentIndex(previous >= 0 ? previous : 0);
    operator_->blockSignals(false);
    updateSelectionAvailability();
}

void TuFlowWidget::setScenarioAvailable(bool available, const QString& detail)
{
    scenarioAvailable_ = available;
    operator_->setEnabled(available);
    registered_->setEnabled(available);
    scenarioState_->setText(available ? QString() :
        (detail.isEmpty() ? QStringLiteral("Проверка по ТУ сейчас недоступна") : detail));
    scenarioState_->setStyleSheet(available
        ? QString() : QStringLiteral("color:#e1766d;font-weight:700;"));
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
        if (code == QStringLiteral("R4831") || code == QStringLiteral("SCHEME")) continue;
        if (seen.contains(code)) continue;
        seen.insert(code);
        requiredEquipment_ << code;
    }

    equipmentState_.clear();
    equipmentDetail_.clear();
    for (const auto& code : requiredEquipment_) equipmentState_.insert(code, -1);

    serialTitle_->setText(QStringLiteral("УБСИ SN %1 · оператор %2")
                              .arg(activeSerial_, activeOperator_));
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
    activeOperator_.clear();
    requiredEquipment_.clear();
    equipmentState_.clear();
    equipmentDetail_.clear();
    registered_->setCurrentIndex(0);
    operator_->setCurrentIndex(0);
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

void TuFlowWidget::updateSelectionAvailability()
{
    const bool ready = scenarioAvailable_
        && !operator_->currentData().toString().trimmed().isEmpty()
        && !registered_->currentData().toString().trimmed().isEmpty();
    check_->setEnabled(ready);
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
