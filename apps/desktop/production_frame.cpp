#include "production_frame.h"

#include <QProgressBar>
#include <QFrame>
#include <QFont>

namespace {
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

ProductionFrame::ProductionFrame(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("productionFrame"));
    setStyleSheet(QStringLiteral(
        "#productionFrame{background:#08131d;color:#eaf4fb;font-family:'Segoe UI';}"
        "QFrame#header{background:#102333;border-bottom:1px solid #264257;}"
        "QFrame#sidebar{background:#0e1e2c;border-right:1px solid #264257;}"
        "QFrame#telemetry{background:#102333;border-top:1px solid #264257;}"
        "QLabel[muted='true']{color:#8ea6b7;}"
        "QPushButton#stopBtn{background:#3d1a1a;color:#ef5a5a;border:1px solid #6e2b2b;border-radius:6px;padding:6px 12px;font-weight:700;}"
        "QPushButton#stopBtn:hover{background:#522525;border-color:#ef5a5a;}"
        "QProgressBar{background:#0e1e2c;border:1px solid #264257;border-radius:4px;text-align:center;color:#eaf4fb;}"
        "QProgressBar::chunk{background:qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2d7fe9, stop:1 #2266c4);}"
    ));

    setupUi();
}

void ProductionFrame::setupUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Header
    auto* header = new QFrame(this);
    header->setObjectName(QStringLiteral("header"));
    header->setMinimumHeight(60);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 0, 20, 0);
    headerLayout->setSpacing(24);

    auto* backBtn = new QPushButton(QStringLiteral("← Сессия"), header);
    backBtn->setStyleSheet(QStringLiteral("background:transparent; color:#eaf4fb; border:none; font-size:14px;"));
    headerLayout->addWidget(backBtn);

    auto* titleGroup = new QWidget(header);
    auto* titleLayout = new QHBoxLayout(titleGroup);
    titleLayout->setSpacing(12);
    titleLayout->setContentsMargins(0, 0, 0, 0);

    serialLabel_ = heading(QStringLiteral("УБСИ ---"), 16, titleGroup);
    scopeLabel_ = muted(QStringLiteral("Производство · ---"), titleGroup);

    titleLayout->addWidget(serialLabel_);
    titleLayout->addWidget(scopeLabel_);
    headerLayout->addWidget(titleGroup);

    headerLayout->addStretch();

    operatorLabel_ = muted(QStringLiteral("оператор: ---"), titleGroup);
    headerLayout->addWidget(operatorLabel_);

    stopButton_ = new QPushButton(QStringLiteral("Остановить"), header);
    stopButton_->setObjectName(QStringLiteral("stopBtn"));
    headerLayout->addWidget(stopButton_);

    root->addWidget(header);

    // Main Area
    auto* center = new QWidget(this);
    auto* centerLayout = new QHBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);

    sidebar_ = new QFrame(center);
    sidebar_->setObjectName(QStringLiteral("sidebar"));
    sidebar_->setFixedWidth(260);
    auto* sidebarLayout = new QVBoxLayout(sidebar_);
    sidebarLayout->setContentsMargins(16, 24, 16, 24);
    sidebarLayout->setSpacing(12);

    // Placeholder for sidebar content (stages)
    auto* sidebarTitle = heading(QStringLiteral("ЭТАПЫ"), 12, sidebar_);
    sidebarTitle->setStyleSheet(QStringLiteral("color:#8ea6b7; letter-spacing:0.1em;"));
    sidebarLayout->addWidget(sidebarTitle);
    sidebarLayout->addStretch();

    workspace_ = new QWidget(center);
    workspace_->setStyleSheet(QStringLiteral("background:#08131d;"));

    centerLayout->addWidget(sidebar_);
    centerLayout->addWidget(workspace_, 1);
    root->addWidget(center, 1);

    // Telemetry
    auto* telemetry = new QFrame(this);
    telemetry->setObjectName(QStringLiteral("telemetry"));
    telemetry->setMinimumHeight(40);
    auto* telemetryLayout = new QHBoxLayout(telemetry);
    telemetryLayout->setContentsMargins(20, 0, 20, 0);
    telemetryLayout->setSpacing(30);

    elapsedLabel_ = new QLabel(QStringLiteral("00:00:00"), telemetry);
    elapsedLabel_->setStyleSheet(QStringLiteral("font-family:'Consolas'; font-size:16px; color:#eaf4fb;"));
    telemetryLayout->addWidget(elapsedLabel_);

    auto* progressGroup = new QWidget(telemetry);
    auto* progressLayout = new QHBoxLayout(progressGroup);
    progressLayout->setSpacing(12);
    progressLayout->setContentsMargins(0, 0, 0, 0);

    progressLabel_ = new QLabel(QStringLiteral("Прогресс"), telemetry);
    progressLabel_->setStyleSheet(QStringLiteral("color:#8ea6b7; font-size:13px;"));

    progressBar_ = new QProgressBar(telemetry);
    progressBar_->setFixedWidth(200);
    progressBar_->setTextVisible(false);

    progressText_ = new QLabel(QStringLiteral("Ожидание запуска..."), telemetry);
    progressText_->setStyleSheet(QStringLiteral("color:#eaf4fb; font-size:13px;"));

    progressLayout->addWidget(progressLabel_);
    progressLayout->addWidget(progressBar_);
    progressLayout->addWidget(progressText_);
    telemetryLayout->addWidget(progressGroup);

    telemetryLayout->addStretch();

    auto* opGroup = new QWidget(telemetry);
    auto* opLayout = new QHBoxLayout(opGroup);
    opLayout->setSpacing(8);
    opLayout->setContentsMargins(0, 0, 0, 0);

    auto* opLabel = new QLabel(QStringLiteral("Операция:"), telemetry);
    opLabel->setStyleSheet(QStringLiteral("color:#8ea6b7; font-size:13px;"));

    auto* opValue = new QLabel(QStringLiteral("---"), telemetry);
    opValue->setObjectName(QStringLiteral("opValue"));
    opValue->setStyleSheet(QStringLiteral("color:#eaf4fb; font-size:13px; font-weight:700;"));

    opLayout->addWidget(opLabel);
    opLayout->addWidget(opValue);
    telemetryLayout->addWidget(opGroup);

    root->addWidget(telemetry);

    connect(stopButton_, &QPushButton::clicked, this, [this] {
        if (stopCallback_) stopCallback_();
    });
}

void ProductionFrame::updateHeader(const QString& serial, const QString& scope, const QString& operatorName)
{
    serialLabel_->setText(QStringLiteral("УБСИ %1").arg(serial));
    scopeLabel_->setText(QStringLiteral("Производство · %1").arg(scope));
    operatorLabel_->setText(QStringLiteral("оператор: %1").arg(operatorName));
}

void ProductionFrame::updateTelemetry(const QString& elapsed, const QString& progressLabel, const QString& progressText, double fraction)
{
    elapsedLabel_->setText(elapsed);
    progressLabel_->setText(progressLabel);
    progressText_->setText(progressText);
    progressBar_->setValue(static_cast<int>(fraction * 100));
}

void ProductionFrame::updateOperation(const QString& label, const QString& value)
{
    // we need to find the opValue label. since we didn't store it as a member, we find it by object name
    auto* opValue = this->findChild<QLabel*>(QStringLiteral("opValue"));
    if (opValue) {
        opValue->setText(value);
    }
}

void ProductionFrame::setStopCallback(std::function<void()> callback)
{
    stopCallback_ = callback;
}
