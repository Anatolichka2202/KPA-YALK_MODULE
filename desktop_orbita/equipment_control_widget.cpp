#include "equipment_control_widget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <stdexcept>

namespace {
QString field(const std::string& response, const std::string& name)
{
    const std::string prefix = name + "=";
    const auto start = response.find(prefix);
    if (start == std::string::npos) return {};
    const auto value = start + prefix.size();
    const auto end = response.find('\n', value);
    return QString::fromStdString(response.substr(value, end - value));
}
}

EquipmentControlWidget::EquipmentControlWidget(Invoke invoke, QWidget* parent)
    : QWidget(parent), invoke_(std::move(invoke))
{
    setWindowTitle(QStringLiteral("Ручное управление оборудованием — Advanced mode"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(720, 650);
    setStyleSheet("QWidget{background:#14171c;color:#dfe6ee;}"
                  "QGroupBox{border:1px solid #2a313b;margin-top:12px;padding-top:8px;}"
                  "QGroupBox::title{subcontrol-origin:margin;left:10px;}"
                  "QComboBox,QSpinBox,QPlainTextEdit{background:#0e1115;border:1px solid #2a313b;padding:4px;}"
                  "QPushButton{background:#223044;border:1px solid #36506d;padding:6px 12px;}"
                  "QPushButton:hover{background:#2b4058;}");

    auto* root = new QVBoxLayout(this);
    auto* warning = new QLabel(QStringLiteral(
        "Инженерное управление воздействует на реальное оборудование. "
        "Каждая команда требует явного подтверждения; сценарий использует те же операции плагинов."));
    warning->setWordWrap(true);
    warning->setStyleSheet("color:#d99a4a;font-weight:bold;");
    root->addWidget(warning);

    auto* adapter = new QGroupBox(QStringLiteral("Ethernet/RS-485 адаптер УБСИ"));
    auto* adapterForm = new QFormLayout(adapter);
    adapterMode_ = new QComboBox;
    adapterMode_->addItem(QStringLiteral("Подготовить ЯЛК 8 кГц (полная последовательность)"),
                          QStringLiteral("prepare_yalk"));
    adapterMode_->addItem(QStringLiteral("Сброс адаптера"), QStringLiteral("reset_adapter"));
    adapterMode_->addItem(QStringLiteral("Передать таблицу адресов ЯЛК"),
                          QStringLiteral("configure_yalk"));
    adapterMode_->addItem(QStringLiteral("Передать таблицу адресов ЯТП"),
                          QStringLiteral("configure_ytp"));
    adapterMode_->addItem(QStringLiteral("Выбрать поток ЯЛК"), QStringLiteral("select_yalk"));
    adapterChannel_ = new QSpinBox;
    adapterChannel_->setRange(1, 16);
    adapterChannel_->setValue(1);
    adapterAddressCount_ = new QSpinBox;
    adapterAddressCount_->setRange(1, 100);
    adapterAddressCount_->setValue(43);
    adapterSlow_ = new QCheckBox(QStringLiteral("медленные параметры"));
    adapterSlow_->setChecked(true);
    adapterFast_ = new QCheckBox(QStringLiteral("быстрые параметры"));
    auto* modeRow = new QHBoxLayout;
    modeRow->addWidget(adapterMode_, 1);
    auto* applyMode = new QPushButton(QStringLiteral("Выполнить"));
    modeRow->addWidget(applyMode);
    adapterForm->addRow(QStringLiteral("Операция ROKOT:"), modeRow);
    adapterForm->addRow(QStringLiteral("Канал адаптера:"), adapterChannel_);
    adapterForm->addRow(QStringLiteral("Адресов ЯЛК:"), adapterAddressCount_);
    auto* parameterKinds = new QHBoxLayout;
    parameterKinds->addWidget(adapterSlow_);
    parameterKinds->addWidget(adapterFast_);
    adapterForm->addRow(QStringLiteral("Параметры:"), parameterKinds);

    adapterAddress_ = new QSpinBox;
    adapterAddress_->setRange(1, 100);
    adapterMask_ = new QComboBox;
    adapterMask_->addItem(QStringLiteral("аналог 9 бит (0x01FF)"), QStringLiteral("0x01FF"));
    adapterMask_->addItem(QStringLiteral("контакт (0x0200)"), QStringLiteral("0x0200"));
    adapterMask_->addItem(QStringLiteral("сырое слово (0xFFFF)"), QStringLiteral("0xFFFF"));
    auto* readWord = new QPushButton(QStringLiteral("Считать"));
    adapterValue_ = new QLabel(QStringLiteral("—"));
    auto* wordRow = new QHBoxLayout;
    wordRow->addWidget(adapterAddress_);
    wordRow->addWidget(adapterMask_, 1);
    wordRow->addWidget(readWord);
    wordRow->addWidget(adapterValue_);
    adapterForm->addRow(QStringLiteral("Адрес УЛК:"), wordRow);
    root->addWidget(adapter);

    auto* isd = new QGroupBox(QStringLiteral("ИСД 192.168.0.101"));
    auto* isdForm = new QFormLayout(isd);
    isdType_ = new QComboBox;
    isdType_->addItem(QStringLiteral("Аналоговый выход (type=1)"), 1);
    isdType_->addItem(QStringLiteral("Ключ (type=2)"), 2);
    isdType_->addItem(QStringLiteral("Шина (type=3)"), 3);
    isdChannel_ = new QSpinBox;
    isdChannel_->setRange(1, 256);
    isdCode_ = new QSpinBox;
    isdCode_->setRange(0, 4095);
    isdEnabled_ = new QCheckBox(QStringLiteral("включено / шина+"));
    auto* applyIsd = new QPushButton(QStringLiteral("Передать команду"));
    auto* reset = new QPushButton(QStringLiteral("Полный сброс ИСД"));
    reset->setStyleSheet("QPushButton{background:#5a2929;border-color:#8a4444;padding:6px 12px;}");
    isdForm->addRow(QStringLiteral("Тип:"), isdType_);
    isdForm->addRow(QStringLiteral("Номер канала:"), isdChannel_);
    isdForm->addRow(QStringLiteral("Код 0…4095:"), isdCode_);
    isdForm->addRow(QString(), isdEnabled_);
    auto* isdButtons = new QHBoxLayout;
    isdButtons->addWidget(applyIsd);
    isdButtons->addWidget(reset);
    isdForm->addRow(QString(), isdButtons);
    root->addWidget(isd);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setPlaceholderText(QStringLiteral("Здесь будут команды и ответы плагинов"));
    root->addWidget(log_, 1);

    connect(applyMode, &QPushButton::clicked, this, &EquipmentControlWidget::applyAdapterMode);
    connect(readWord, &QPushButton::clicked, this, &EquipmentControlWidget::readAdapterWord);
    connect(applyIsd, &QPushButton::clicked, this, &EquipmentControlWidget::applyIsdCommand);
    connect(reset, &QPushButton::clicked, this, &EquipmentControlWidget::resetIsd);
    connect(isdType_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EquipmentControlWidget::updateIsdFields);
    updateIsdFields();
}

bool EquipmentControlWidget::confirm(const QString& text)
{
    return QMessageBox::question(this, QStringLiteral("Подтверждение воздействия"), text,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

void EquipmentControlWidget::run(const std::string& capability, const std::string& operation,
                                 const std::map<std::string, std::string>& arguments)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    try {
        const auto response = invoke_(capability, operation, arguments);
        log_->appendPlainText(QStringLiteral("[%1] %2.%3\n%4")
            .arg(stamp, QString::fromStdString(capability), QString::fromStdString(operation),
                 QString::fromStdString(response).trimmed()));
        if (capability == "ubsi.parameter_source" && operation == "read")
            adapterValue_->setText(field(response, "raw"));
    } catch (const std::exception& error) {
        log_->appendPlainText(QStringLiteral("[%1] ОШИБКА %2.%3: %4")
            .arg(stamp, QString::fromStdString(capability), QString::fromStdString(operation),
                 QString::fromUtf8(error.what())));
    }
}

void EquipmentControlWidget::applyAdapterMode()
{
    const std::string operation = adapterMode_->currentData().toString().toStdString();
    if (!confirm(QStringLiteral("Выполнить операцию адаптера «%1»?")
            .arg(adapterMode_->currentText()))) return;
    run("ubsi.parameter_source", operation, {
        {"adapter_channel", std::to_string(adapterChannel_->value())},
        {"address_count", std::to_string(adapterAddressCount_->value())},
        {"yalk_number", "1"}, {"first_address", "1"},
        {"slow", adapterSlow_->isChecked() ? "true" : "false"},
        {"fast", adapterFast_->isChecked() ? "true" : "false"}});
}

void EquipmentControlWidget::readAdapterWord()
{
    run("ubsi.parameter_source", "read", {
        {"ulk_address", std::to_string(adapterAddress_->value())},
        {"parameter_group", "manual"}, {"mask", adapterMask_->currentData().toString().toStdString()},
        {"sample_count", "1"}});
}

void EquipmentControlWidget::updateIsdFields()
{
    isdCode_->setEnabled(isdType_->currentData().toInt() == 1);
}

void EquipmentControlWidget::applyIsdCommand()
{
    const int type = isdType_->currentData().toInt();
    const int channel = isdChannel_->value();
    if (!confirm(QStringLiteral("Передать ИСД команду type=%1, канал=%2, %3?")
            .arg(type).arg(channel).arg(isdEnabled_->isChecked() ? QStringLiteral("ВКЛ")
                                                                 : QStringLiteral("ВЫКЛ")))) return;
    if (type == 1) {
        run("stand.switch_matrix", "analog", {{"channel", std::to_string(channel)},
            {"value", std::to_string(isdCode_->value())},
            {"enabled", isdEnabled_->isChecked() ? "true" : "false"}});
    } else {
        run("stand.switch_matrix", "switch", {{"type", std::to_string(type)},
            {"channel", std::to_string(channel)},
            {"enabled", isdEnabled_->isChecked() ? "true" : "false"}});
    }
}

void EquipmentControlWidget::resetIsd()
{
    if (!confirm(QStringLiteral("Выполнить штатный полный сброс ИСД (type=4num=1)?"))) return;
    run("stand.switch_matrix", "full_reset");
}
