#ifndef EQUIPMENT_CONTROL_WIDGET_H
#define EQUIPMENT_CONTROL_WIDGET_H

#include <QWidget>
#include <functional>
#include <map>
#include <string>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QSpinBox;
class QDoubleSpinBox;

class EquipmentControlWidget final : public QWidget
{
    Q_OBJECT
public:
    using Invoke = std::function<std::string(
        const std::string&, const std::string&,
        const std::map<std::string, std::string>&)>;
    explicit EquipmentControlWidget(Invoke invoke, QWidget* parent = nullptr);

private slots:
    void applyAdapterMode();
    void readAdapterWord();
    void applyIsdCommand();
    void resetIsd();
    void updateIsdFields();

private:
    bool confirm(const QString& text);
    void run(const std::string& capability, const std::string& operation,
             const std::map<std::string, std::string>& arguments = {});

    Invoke invoke_;
    QComboBox* adapterMode_ = nullptr;
    QSpinBox* adapterChannel_ = nullptr;
    QSpinBox* adapterAddressCount_ = nullptr;
    QCheckBox* adapterSlow_ = nullptr;
    QCheckBox* adapterFast_ = nullptr;
    QSpinBox* adapterAddress_ = nullptr;
    QComboBox* adapterMask_ = nullptr;
    QLabel* adapterValue_ = nullptr;
    QLabel* adapterVoltage_ = nullptr;
    double yalkZeroRaw_ = 0.0;
    double yalkFullRaw_ = 0.0;
    QComboBox* isdType_ = nullptr;
    QSpinBox* isdChannel_ = nullptr;
    QSpinBox* isdCode_ = nullptr;
    QDoubleSpinBox* isdVolts_ = nullptr;
    QCheckBox* isdUseVolts_ = nullptr;
    QCheckBox* isdEnabled_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
};

#endif
