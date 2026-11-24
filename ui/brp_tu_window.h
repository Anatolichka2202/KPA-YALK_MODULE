#pragma once

#include <QWidget>
#include <memory>

class BrpTuWindow final : public QWidget {
public:
    explicit BrpTuWindow(bool simulation, QWidget* parent = nullptr);
    ~BrpTuWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
