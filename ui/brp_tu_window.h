#pragma once

#include <QWidget>
#include <memory>
#include <functional>
#include <optional>
#include <string>

class BrpTuWindow final : public QWidget {
public:
    struct SimulatedVoltagePoint {
        unsigned frameCode;
        std::optional<double> referenceBefore;
        std::optional<double> referenceAfter;
    };
    using SimulatedVoltageSource = std::function<std::optional<SimulatedVoltagePoint>(const std::string&, double)>;

    explicit BrpTuWindow(bool simulation, QWidget* parent = nullptr);
    explicit BrpTuWindow(SimulatedVoltageSource voltageSource, QWidget* parent = nullptr);
    ~BrpTuWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
