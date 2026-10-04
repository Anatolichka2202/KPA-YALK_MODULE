#pragma once

#include <QWidget>

#include <functional>
#include <map>
#include <string>
#include <vector>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

struct FreeWorkspaceCapability
{
    QString id;
    QString title;
};

// Консоль свободного контура не знает транспортов и моделей приборов. Она
// передаёт выбранную операцию в capability, а проверка безопасности остаётся
// обязанностью плагина и явного подтверждения инженера.
class FreeWorkspaceWidget final : public QWidget
{
    Q_OBJECT

public:
    using Invoke = std::function<std::string(
        const std::string&, const std::string&,
        const std::map<std::string, std::string>&)>;
    using Confirm = std::function<bool(const QString&)>;

    explicit FreeWorkspaceWidget(std::vector<FreeWorkspaceCapability> capabilities,
                                 Invoke invoke,
                                 Confirm confirm = {},
                                 QWidget* parent = nullptr);

signals:
    // Адаптерный монитор остаётся отдельным представлением, потому что он
    // непрерывно декодирует кадры, а не показывает единичный ответ команды.
    void adapterMonitoringRequested(const QString& capability);

private slots:
    void invokeSelectedOperation();
    void updateCapabilityState();

private:
    static std::map<std::string, std::string> parseArguments(const QString& text);

    std::vector<FreeWorkspaceCapability> capabilities_;
    Invoke invoke_;
    Confirm confirm_;
    QComboBox* capability_ = nullptr;
    QLineEdit* operation_ = nullptr;
    QPlainTextEdit* arguments_ = nullptr;
    QPlainTextEdit* output_ = nullptr;
    QPushButton* monitorAdapter_ = nullptr;
};
