#pragma once

#include <QWidget>
#include <memory>

class BrpTuWindow final : public QWidget {
public:
    explicit BrpTuWindow(bool simulation, QWidget* parent = nullptr);
    ~BrpTuWindow() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
