#include "equipment_control_widget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QtMath>
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

    auto* adapter = new QGroupBox(QStringLiteral("Ethernet/RS-485 адаптер УЛК · 192.168.0.115:1113"));
    auto* adapterForm = new QFormLayout(adapter);
    adapterMode_ = new QComboBox;
    adapterMode_->addItem(QStringLiteral("Запустить ЯЛК / УЛК 8 кГц (режим 6)"),
                          QStringLiteral("start_stream"));
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
    auto* stopMode = new QPushButton(QStringLiteral("Остановить поток"));
    auto* showStats = new QPushButton(QStringLiteral("Статистика кадров"));
    modeRow->addWidget(applyMode);
    modeRow->addWidget(stopMode);
    modeRow->addWidget(showStats);
    adapterForm->addRow(QStringLiteral("Режим:"), modeRow);
    adapterForm->addRow(QStringLiteral("Физический канал адаптера:"), adapterChannel_);
    adapterForm->addRow(QStringLiteral("Адресов в цикле:"), adapterAddressCount_);
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
    auto* readCalibration = new QPushButton(QStringLiteral("Считать 97/99"));
    adapterValue_ = new QLabel(QStringLiteral("—"));
    adapterVoltage_ = new QLabel(QStringLiteral("—"));
    auto* wordRow = new QHBoxLayout;
    wordRow->addWidget(adapterAddress_);
    wordRow->addWidget(adapterMask_, 1);
    wordRow->addWidget(readWord);
    wordRow->addWidget(readCalibration);
    wordRow->addWidget(adapterValue_);
    adapterForm->addRow(QStringLiteral("Адрес УЛК:"), wordRow);
    adapterForm->addRow(QStringLiteral("По калибровкам 97/99:"), adapterVoltage_);
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
    isdVolts_ = new QDoubleSpinBox;
    isdVolts_->setRange(0.0, 6.2);
    isdVolts_->setDecimals(3);
    isdVolts_->setSingleStep(0.1);
    isdUseVolts_ = new QCheckBox(QStringLiteral("задавать в вольтах 0…6,2 В"));
    isdUseVolts_->setChecked(true);
    isdEnabled_ = new QCheckBox(QStringLiteral("включено / шина+"));
    auto* applyIsd = new QPushButton(QStringLiteral("Передать команду"));
    auto* reset = new QPushButton(QStringLiteral("Полный сброс ИСД"));
    reset->setStyleSheet("QPushButton{background:#5a2929;border-color:#8a4444;padding:6px 12px;}");
    isdForm->addRow(QStringLiteral("Тип:"), isdType_);
    isdForm->addRow(QStringLiteral("Номер канала:"), isdChannel_);
    isdForm->addRow(QStringLiteral("Код 0…4095:"), isdCode_);
    isdForm->addRow(QStringLiteral("Напряжение:"), isdVolts_);
    isdForm->addRow(QString(), isdUseVolts_);
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
    connect(readCalibration, &QPushButton::clicked, this, [this] {
        try {
            const auto zero = invoke_("ulk.parameter_source", "read_channel", {
                {"ulk_address", "97"}, {"sample_count", "16"}});
            const auto full = invoke_("ulk.parameter_source", "read_channel", {
                {"ulk_address", "99"}, {"sample_count", "16"}});
            yalkZeroRaw_ = field(zero, "analog_code_mean").toDouble();
            yalkFullRaw_ = field(full, "analog_code_mean").toDouble();
            adapterVoltage_->setText(QStringLiteral("0=%1, 6,2=%2")
                .arg(yalkZeroRaw_, 0, 'f', 2).arg(yalkFullRaw_, 0, 'f', 2));
            log_->appendPlainText(QStringLiteral("Калибровки ЯЛК: %1")
                .arg(adapterVoltage_->text()));
        } catch (const std::exception& error) {
            log_->appendPlainText(QStringLiteral("ОШИБКА калибровок ЯЛК: %1")
                .arg(QString::fromUtf8(error.what())));
        }
    });
    connect(stopMode, &QPushButton::clicked, this, [this] {
        run("ulk.parameter_source", "stop_stream");
    });
    connect(showStats, &QPushButton::clicked, this, [this] {
        run("ulk.parameter_source", "stats");
    });
    connect(applyIsd, &QPushButton::clicked, this, &EquipmentControlWidget::applyIsdCommand);
    connect(reset, &QPushButton::clicked, this, &EquipmentControlWidget::resetIsd);
    connect(isdType_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EquipmentControlWidget::updateIsdFields);
    connect(isdUseVolts_, &QCheckBox::toggled, this, &EquipmentControlWidget::updateIsdFields);
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
        if (capability == "ulk.parameter_source" && operation == "read_channel")
        {
            const double raw = field(response, "analog_code_mean").toDouble();
            adapterValue_->setText(QStringLiteral("raw=%1; code=%2; signal=%3")
                .arg(field(response, "raw_mean"), field(response, "analog_code_mean"),
                     field(response, "signal")));
            if (yalkFullRaw_ > yalkZeroRaw_) {
                adapterVoltage_->setText(QStringLiteral("%1 В")
                    .arg((raw - yalkZeroRaw_) * 6.2 / (yalkFullRaw_ - yalkZeroRaw_), 0, 'f', 4));
            }
        }
    } catch (const std::exception& error) {
        log_->appendPlainText(QStringLiteral("[%1] ОШИБКА %2.%3: %4")
            .arg(stamp, QString::fromStdString(capability), QString::fromStdString(operation),
                 QString::fromUtf8(error.what())));
    }
}

void EquipmentControlWidget::applyAdapterMode()
{
    const std::string operation = adapterMode_->currentData().toString().toStdString();
    if (!confirm(QStringLiteral("Запустить поток ЯЛК / УЛК через адаптер?")
            .arg(adapterMode_->currentText()))) return;
    run("ulk.parameter_source", operation, {
        {"mode", "6"},
        {"adapter_channel", std::to_string(adapterChannel_->value())},
        {"address_count", std::to_string(adapterAddressCount_->value())},
        {"yalk_number", "1"}, {"first_address", "1"},
        {"slow", adapterSlow_->isChecked() ? "true" : "false"},
        {"fast", adapterFast_->isChecked() ? "true" : "false"}});
}

void EquipmentControlWidget::readAdapterWord()
{
    run("ulk.parameter_source", "read_channel", {
        {"ulk_address", std::to_string(adapterAddress_->value())},
        {"parameter_group", "manual"}, {"mask", adapterMask_->currentData().toString().toStdString()},
        {"sample_count", "1"}});
}

void EquipmentControlWidget::updateIsdFields()
{
    const bool analog = isdType_->currentData().toInt() == 1;
    isdCode_->setEnabled(analog && !isdUseVolts_->isChecked());
    isdVolts_->setEnabled(analog && isdUseVolts_->isChecked());
    isdUseVolts_->setEnabled(analog);
}

void EquipmentControlWidget::applyIsdCommand()
{
    const int type = isdType_->currentData().toInt();
    const int channel = isdChannel_->value();
    if (!confirm(QStringLiteral("Передать ИСД команду type=%1, канал=%2, %3?")
            .arg(type).arg(channel).arg(isdEnabled_->isChecked() ? QStringLiteral("ВКЛ")
                                                                 : QStringLiteral("ВЫКЛ")))) return;
    if (type == 1) {
        const int code = isdUseVolts_->isChecked()
            ? qRound(isdVolts_->value() * 4095.0 / 6.2)
            : isdCode_->value();
        run("stand.switch_matrix", "set_analog", {{"channel", std::to_string(channel)},
            {"code", std::to_string(code)},
            {"enabled", isdEnabled_->isChecked() ? "true" : "false"}});
    } else {
        run("stand.switch_matrix", "set_switch", {{"type", std::to_string(type)},
            {"channel", std::to_string(channel)},
            {"enabled", isdEnabled_->isChecked() ? "true" : "false"}});
    }
}

void EquipmentControlWidget::resetIsd()
{
    if (!confirm(QStringLiteral("Выполнить штатный полный сброс ИСД (type=4num=1)?"))) return;
    run("stand.switch_matrix", "full_reset");
}
