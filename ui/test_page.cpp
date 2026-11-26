#include "test_page.h"
#include "history_plot.h"
#include "run_journal_overlay.h"
#include "tu_flow_widget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

using namespace brp::ui;

namespace {

QString appStyle()
{
    return QStringLiteral(R"QSS(
QWidget#operatorTestPage { background:#08131d; color:#eaf4fb; font-family:"Segoe UI"; font-size:13px; }
QWidget { color:#eaf4fb; }
QFrame[panel="true"] { background:#102333; border:1px solid #264257; border-radius:8px; }
QFrame[flatPanel="true"] { background:#0e1e2c; border:0; border-right:1px solid #1a3346; }
QFrame[tuStageHeader="true"] { background:#0a1721; border:0; border-bottom:1px solid #1a3346; border-radius:0; }
QFrame[tuRow="true"] { background:#0a1722; border:1px solid #1a3346; border-radius:6px; }
QFrame[tuRowActive="true"] { background:#102c46; border:1px solid #58a5ff; border-radius:6px; }
QLabel[muted="true"] { color:#8ea6b7; }
QLabel[caption="true"] { color:#8ea6b7; font-size:11px; font-weight:700; }
QLabel[tuId="true"] { color:#67d8eb; font-size:11px; font-weight:800; }
QLabel[tuTitle="true"] { color:#9cb1c0; font-size:11px; }
QPushButton { background:#132a3d; border:1px solid #264257; border-radius:7px; padding:8px 13px; color:#eaf4fb; }
QPushButton:hover { border-color:#58a5ff; background:#17334a; }
QPushButton#primary { background:#2e7de9; border-color:#58a5ff; font-weight:700; }
QPushButton#danger { background:#3a1d24; border-color:#7e3340; color:#ffd8dc; }
QPushButton#skipStage { background:#352b16; border-color:#8d6a1e; color:#ffe3a1; font-weight:700; }
QPushButton:disabled { color:#61788a; background:#0e1e2c; border-color:#1a3346; }
QLineEdit,QComboBox { background:#0e1e2c; border:1px solid #264257; border-radius:6px; padding:8px; min-height:20px; }
QTableWidget { background:#0e1e2c; alternate-background-color:#102333; border:1px solid #264257; gridline-color:#1a3346; selection-background-color:#123b58; selection-color:#eaf4fb; }
QHeaderView::section { background:#102333; color:#8ea6b7; border:0; border-bottom:1px solid #264257; padding:8px; font-weight:700; }
QStackedWidget { background:#08131d; }
)QSS");
}

QFrame* makePanel(QWidget* parent = nullptr)
{
    auto* frame = new QFrame(parent);
    frame->setProperty("panel", true);
    return frame;
}

QLabel* caption(const QString& text, QWidget* parent = nullptr)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("caption", true);
    return label;
}

QLabel* muted(const QString& text, QWidget* parent = nullptr)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("muted", true);
    label->setWordWrap(true);
    return label;
}

QLabel* heading(const QString& text, int point, QWidget* parent = nullptr)
{
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setPointSize(point);
    font.setBold(true);
    label->setFont(font);
    return label;
}


QString verdictText(tu::RunVerdict verdict)
{
    switch (verdict) {
    case tu::RunVerdict::Ok: return QStringLiteral("НОРМА");
    case tu::RunVerdict::Fail: return QStringLiteral("НЕ НОРМА");
    case tu::RunVerdict::Incomplete: return QStringLiteral("НЕПОЛНАЯ");
    case tu::RunVerdict::Error: return QStringLiteral("ОШИБКА СТЕНДА");
    case tu::RunVerdict::Aborted: return QStringLiteral("ОСТАНОВЛЕНО");
    case tu::RunVerdict::NotRun: return QStringLiteral("НЕ ВЫПОЛНЕНО");
    }
    return {};
}

QColor verdictColor(tu::RunVerdict verdict)
{
    switch (verdict) {
    case tu::RunVerdict::Ok: return palette::green;
    case tu::RunVerdict::Fail:
    case tu::RunVerdict::Error: return palette::red;
    case tu::RunVerdict::Aborted:
    case tu::RunVerdict::Incomplete: return palette::amber;
    case tu::RunVerdict::NotRun: return palette::dim;
    }
    return palette::dim;
}

} // namespace

struct TestPage::Impl {
    explicit Impl(TestPage* page) : q(page)
    {
        q->setObjectName(QStringLiteral("operatorTestPage"));
        q->setStyleSheet(appStyle());
        auto* root = new QVBoxLayout(q);
        root->setContentsMargins(0, 0, 0, 0);
        pages = new QStackedWidget(q);
        root->addWidget(pages);
        tuFlow = new TuFlowWidget(pages);
        pages->addWidget(tuFlow);
#if BRP_ENABLE_MANUAL_TESTS
        manualPage = new QWidget(pages);
        pages->addWidget(manualPage);
        QObject::connect(tuFlow, &TuFlowWidget::manualRequested, q, [this] {
            pages->setCurrentWidget(manualPage);
        });
#endif
        buildRuntime();
    }

    void buildRuntime()
    {
        runtimePageWidget = new QWidget(pages);
        auto* layout = new QVBoxLayout(runtimePageWidget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* header = new QFrame(runtimePageWidget);
        header->setFixedHeight(52);
        header->setProperty("panel", true);
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(16, 8, 16, 8);
        runtimeBack = new QPushButton(QStringLiteral("← Сессия"), header);
        runTitle = heading(QStringLiteral("БРП"), 14, header);
        runSubtitle = muted(QStringLiteral("Проверка по ТУ"), header);
        stopButton = new QPushButton(QStringLiteral("Остановить"), header);
        stopButton->setObjectName(QStringLiteral("danger"));
        stopButton->setProperty("tuStopAction", true);
        bbPrSignal = new QPushButton(QStringLiteral("ББ ПР · выдать сигнал"), header);
        bbPrSignal->setObjectName(QStringLiteral("brpIssueBbPrRuntime"));
        bbPrSignal->setEnabled(false);
        skipHotspot = new QPushButton(header);
        skipHotspot->setObjectName(QStringLiteral("tuSkipRevealHotspot"));
        skipHotspot->setFixedSize(24, 24);
        skipHotspot->setFlat(true);
        skipHotspot->setStyleSheet(QStringLiteral(
            "QPushButton{background:transparent;border:0;}"));
        skipButton = new QPushButton(QStringLiteral("Принудительный ОК"), header);
        skipButton->setObjectName(QStringLiteral("skipStage"));
        skipButton->setToolTip(QStringLiteral(
            "Ctrl+Shift+Q: установить итог ОК, не прерывая измерения"));
        skipButton->hide();
        headerLayout->addWidget(runtimeBack);
        headerLayout->addWidget(runTitle);
        headerLayout->addWidget(runSubtitle);
        headerLayout->addStretch();
        headerLayout->addWidget(bbPrSignal);
        headerLayout->addWidget(skipHotspot);
        headerLayout->addWidget(skipButton);
        headerLayout->addWidget(stopButton);
        layout->addWidget(header);

        runtimeContextStrip = new QFrame(runtimePageWidget);
        runtimeContextStrip->setFixedHeight(78);
        runtimeContextStrip->setProperty("panel", true);
        auto* contextLayout = new QHBoxLayout(runtimeContextStrip);
        contextLayout->setContentsMargins(16, 10, 16, 10);
        auto* contextColumn = new QVBoxLayout;
        contextColumn->setSpacing(2);
        contextCaption = caption(QStringLiteral("ТЕКУЩИЙ ЭТАП"), runtimeContextStrip);
        contextColumn->addWidget(contextCaption);
        context = new QLabel(QStringLiteral("Ожидание запуска"), runtimeContextStrip);
        context->setObjectName(QStringLiteral("frozenProcedureContext"));
        QFont contextFont = context->font();
        contextFont.setPointSize(13);
        contextFont.setBold(true);
        context->setFont(contextFont);
        contextColumn->addWidget(context);
        contextLayout->addLayout(contextColumn, 1);
        auto* progressColumn = new QVBoxLayout;
        progressColumn->setSpacing(2);
        progressCaption = caption(QStringLiteral("ХОД / ДАННЫЕ"), runtimeContextStrip);
        progressCaption->setObjectName(QStringLiteral("tuRuntimeProgressCaption"));
        progressCaption->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        progressColumn->addWidget(progressCaption);
        progressText = muted(QStringLiteral("—"), runtimeContextStrip);
        progressText->setObjectName(QStringLiteral("tuRuntimeProgress"));
        progressText->setWordWrap(true);
        progressText->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        progressColumn->addWidget(progressText);
        contextLayout->addLayout(progressColumn, 1);

        auto* body = new QHBoxLayout;
        body->setSpacing(0);

        sidebarStack = new QStackedWidget(runtimePageWidget);
        sidebarStack->setFixedWidth(270);

        tuSidebar = new QFrame(sidebarStack);
        tuSidebar->setProperty("flatPanel", true);
        auto* tuLayout = new QVBoxLayout(tuSidebar);
        tuLayout->setContentsMargins(12, 12, 12, 12);
        tuLayout->setSpacing(8);
        tuTitle = heading(QStringLiteral("ЭТАПЫ ПРОВЕРКИ"), 12, tuSidebar);
        tuTitle->setObjectName(QStringLiteral("tuRuntimeTitle"));
        tuLayout->addWidget(tuTitle);
        tuSerial = muted(QStringLiteral("БРП"), tuSidebar);
        tuSerial->setObjectName(QStringLiteral("tuRuntimeSerial"));
        tuLayout->addWidget(tuSerial);
        tuLayout->addSpacing(4);
        auto* stepScroll = new QScrollArea(tuSidebar);
        stepScroll->setObjectName(QStringLiteral("brpStepScroll"));
        stepScroll->setWidgetResizable(true);
        stepScroll->setFrameShape(QFrame::NoFrame);
        stepScroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;border:0;}"));
        auto* stepContent = new QWidget(stepScroll);
        tuRequirementLayout = new QVBoxLayout(stepContent);
        tuRequirementLayout->setContentsMargins(0, 0, 0, 0);
        tuRequirementLayout->setSpacing(5);
        stepScroll->setWidget(stepContent);
        tuLayout->addWidget(stepScroll, 1);
        sidebarStack->addWidget(tuSidebar);

        body->addWidget(sidebarStack);

        stageStack = new QStackedWidget(runtimePageWidget);
        buildStagePages();
        auto* stageColumn = new QVBoxLayout;
        stageColumn->setContentsMargins(0, 0, 0, 0);
        stageColumn->setSpacing(0);
        stageColumn->addWidget(runtimeContextStrip);
        stageColumn->addWidget(stageStack, 1);
        body->addLayout(stageColumn, 1);
        layout->addLayout(body, 1);


        pages->addWidget(runtimePageWidget);
        journal = new RunJournalOverlay(runtimePageWidget);
        QObject::connect(runtimeBack, &QPushButton::clicked, q, [this] {
            if (!stopButton->isEnabled()) pages->setCurrentWidget(tuFlow);
        });
        QObject::connect(bbPrSignal, &QPushButton::clicked, q, &TestPage::bbPrSignalRequested);
        QObject::connect(stopButton, &QPushButton::clicked, q, &TestPage::stopRequested);
        QObject::connect(skipHotspot, &QPushButton::clicked, q, [this] {
            if (skipButton->isEnabled()) skipButton->setVisible(!skipButton->isVisible());
        });
        QObject::connect(skipButton, &QPushButton::clicked, q, &TestPage::skipCurrentStepRequested);
        auto* skipShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Q")), runtimePageWidget);
        QObject::connect(skipShortcut, &QShortcut::activated, q, [this] {
            if (skipButton->isEnabled()) emit q->skipCurrentStepRequested();
        });
    }

    void buildStagePages()
    {
        auto* stagePage = new QWidget(stageStack);
        auto* stagePageLayout = new QVBoxLayout(stagePage);
        stagePageLayout->setContentsMargins(18, 14, 18, 14);
        stagePageLayout->setSpacing(10);
        stageHeading = heading({}, 18, stagePage);
        stageHeading->setObjectName(QStringLiteral("brpStageHeading"));
        stagePageLayout->addWidget(stageHeading);
        auto* stagePageMetrics = new QHBoxLayout;
        auto metric = [stagePage, stagePageMetrics](const QString& name, QLabel*& target) {
            auto* frame = makePanel(stagePage);
            auto* layout = new QVBoxLayout(frame);
            layout->setContentsMargins(12, 8, 12, 8);
            layout->addWidget(caption(name, frame));
            target = heading(QStringLiteral("—"), 15, frame);
            layout->addWidget(target);
            stagePageMetrics->addWidget(frame, 1);
        };
        metric(QStringLiteral("В7 ДО"), referenceBefore);
        metric(QStringLiteral("В7 ПОСЛЕ"), referenceAfter);
        metric(QStringLiteral("МАКС. РАСХОЖДЕНИЕ"), maximumError);
        stagePageLayout->addLayout(stagePageMetrics);
        plot = new HistoryPlot(stagePage);
        plot->setObjectName(QStringLiteral("tuStageHistory"));
        stagePageLayout->addWidget(plot, 1);

        confirm = new QPushButton(QStringLiteral("Точка готова · измерить"), stagePage);
        confirm->setObjectName(QStringLiteral("brpMeasureAction"));
        confirm->setProperty("brpPointAction", true);
        stagePageLayout->addWidget(confirm, 0, Qt::AlignRight);
        auto* retryActions = new QHBoxLayout;
        retry = new QPushButton(QStringLiteral("Повторить пункт"), stagePage);
        retry->setObjectName(QStringLiteral("brpRetryStage"));
        proceed = new QPushButton(QStringLiteral("Продолжить с НЕ НОРМА"), stagePage);
        proceed->setObjectName(QStringLiteral("brpContinueStage"));
        retry->hide();
        proceed->hide();
        retryActions->addStretch();
        retryActions->addWidget(retry);
        retryActions->addWidget(proceed);
        stagePageLayout->addLayout(retryActions);
        QObject::connect(retry, &QPushButton::clicked, q, &TestPage::retryStageRequested);
        QObject::connect(proceed, &QPushButton::clicked, q, &TestPage::continueStageRequested);
        QObject::connect(confirm, &QPushButton::clicked, q, &TestPage::pointReady);
        stageStack->addWidget(stagePage);
    }

    void addTuRequirement(const QString& key, const QString& id, const QString& title)
    {
        TuRequirementRow row;
        row.frame = new QFrame(tuSidebar);
        row.frame->setProperty("tuRow", true);
        row.frame->setMinimumHeight(42);
        auto* layout = new QVBoxLayout(row.frame);
        layout->setContentsMargins(9, 6, 9, 6);
        layout->setSpacing(1);
        auto* top = new QHBoxLayout;
        row.status = new QLabel(QStringLiteral("ОЖИДАЕТ"), row.frame);
        row.status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row.status->setStyleSheet(QStringLiteral("color:#61788a;font-size:10px;font-weight:700;"));
        auto* point = new QLabel(id.isEmpty() ? QStringLiteral("СТЕНД") : id, row.frame);
        point->setProperty("tuId", true);
        top->addWidget(point);
        top->addStretch();
        top->addWidget(row.status);
        layout->addLayout(top);
        auto* titleLabel = new QLabel(title, row.frame);
        titleLabel->setProperty("tuTitle", true);
        titleLabel->setWordWrap(true);
        layout->addWidget(titleLabel);
        row.frame->setObjectName(QStringLiteral("tuRequirement_%1").arg(key));
        row.status->setObjectName(QStringLiteral("tuRequirementStatus_%1").arg(key));
        tuRows.push_back(row);
        tuRequirementLayout->addWidget(row.frame);
    }


    void refreshTuRow(int index)
    {
        if (index < 0 || index >= tuRows.size()) return;
        auto& row = tuRows[index];
        row.frame->setProperty("tuRowActive", row.active);
        row.frame->setProperty("tuRow", !row.active);
        row.frame->style()->unpolish(row.frame);
        row.frame->style()->polish(row.frame);

        QString text;
        QColor color = palette::dim;
        if (row.active) {
            text = QStringLiteral("ВЫПОЛНЯЕТСЯ");
            color = palette::blue2;
        } else if (row.forcedOk) {
            text = QStringLiteral("ОК · ПРИНУД.");
            color = palette::amber;
        } else if (row.skipped) {
            text = QStringLiteral("ПРОПУЩЕНО");
            color = palette::amber;
        } else if (row.verdict == tu::RunVerdict::NotRun) {
            text = QStringLiteral("ОЖИДАЕТ");
        } else {
            text = verdictText(row.verdict);
            color = verdictColor(row.verdict);
        }
        row.status->setText(text);
        row.status->setStyleSheet(QStringLiteral("color:%1;font-size:10px;font-weight:700;")
                                      .arg(color.name()));
    }


    struct TuRequirementRow {
        QFrame* frame = nullptr;
        QLabel* status = nullptr;
        tu::RunVerdict verdict = tu::RunVerdict::NotRun;
        bool active = false;
        bool skipped = false;
        bool forcedOk = false;
    };

    TestPage* q;
    QStackedWidget *pages = nullptr, *sidebarStack = nullptr, *stageStack = nullptr;
    TuFlowWidget* tuFlow = nullptr;
    QWidget *manualPage = nullptr, *runtimePageWidget = nullptr;
    QFrame *runtimeContextStrip = nullptr, *tuSidebar = nullptr;
    QPushButton *runtimeBack = nullptr, *stopButton = nullptr, *skipHotspot = nullptr,
                *skipButton = nullptr, *confirm = nullptr, *retry = nullptr, *proceed = nullptr,
                *bbPrSignal = nullptr;
    QLabel *runTitle = nullptr, *runSubtitle = nullptr, *contextCaption = nullptr, *context = nullptr,
           *progressCaption = nullptr, *progressText = nullptr, *tuTitle = nullptr, *tuSerial = nullptr,
           *stageHeading = nullptr, *referenceBefore = nullptr, *referenceAfter = nullptr, *maximumError = nullptr;
    QVBoxLayout* tuRequirementLayout = nullptr;
    QVector<TuRequirementRow> tuRows;
    HistoryPlot* plot = nullptr;
    RunJournalOverlay* journal = nullptr;
    int activeStage = -1;
};

TestPage::TestPage(QWidget* parent) : QWidget(parent), impl_(std::make_unique<Impl>(this)) {}
TestPage::~TestPage() = default;

TuFlowWidget* TestPage::flow() const { return impl_->tuFlow; }
QWidget* TestPage::manualPage() const { return impl_->manualPage; }

void TestPage::showFlow()
{
    impl_->pages->setCurrentWidget(impl_->tuFlow);
}

void TestPage::beginRun(const tu::ScenarioRunResult& result)
{
    for (const auto& row : std::as_const(impl_->tuRows)) delete row.frame;
    impl_->tuRows.clear();
    impl_->activeStage = -1;
    const auto serial = QString::fromStdString(result.objectSerial);
    impl_->runTitle->setText(QStringLiteral("БРП SN %1").arg(serial));
    impl_->runSubtitle->setText(QString::fromStdString(result.scenarioTitle));
    impl_->tuSerial->setText(QStringLiteral("БРП · %1").arg(serial));
    for (const auto& step : result.steps) {
        impl_->addTuRequirement(QString::fromStdString(step.nodeId), QString::fromStdString(step.tuRequirement),
                                QString::fromStdString(step.title));
    }
    setRetryAvailable(false);
    impl_->runtimeBack->setEnabled(false);
    impl_->stopButton->setEnabled(true);
    impl_->pages->setCurrentWidget(impl_->runtimePageWidget);
    impl_->journal->beginRun();
}

void TestPage::setStage(int index, const QString& title, const QString& graphTitle,
                        const QString& unit, const QString& first, const QString& second)
{
    if (impl_->activeStage >= 0) {
        impl_->tuRows[impl_->activeStage].active = false;
        impl_->refreshTuRow(impl_->activeStage);
    }
    impl_->activeStage = index;
    impl_->tuRows[index].active = true;
    impl_->refreshTuRow(index);
    impl_->context->setText(QStringLiteral(
        "<span style='color:#67d8eb;font-size:14px;font-weight:800'>ЭТАП</span>"
        "&nbsp;&nbsp;<span style='color:#eaf4fb;font-size:20px;font-weight:700'>%1</span>")
        .arg(title.toHtmlEscaped()));
    if (!graphTitle.isEmpty()) {
        impl_->stageHeading->setText(graphTitle);
        impl_->plot->configure(graphTitle, unit, first, second);
        impl_->plot->clear();
    }
    auto* metrics = impl_->referenceBefore->parentWidget();
    const bool voltage = unit == QStringLiteral("В");
    metrics->setVisible(voltage);
    impl_->referenceAfter->parentWidget()->setVisible(voltage);
    impl_->maximumError->parentWidget()->setVisible(voltage);
    setVoltage({}, {}, {});
}

void TestPage::setStageResult(int index, const tu::StepRunResult& result)
{
    auto& row = impl_->tuRows[index];
    row.active = false;
    row.verdict = result.verdict;
    row.skipped = result.operatorSkipped;
    row.forcedOk = result.forcedOk;
    row.frame->setToolTip(QString::fromStdString(result.message).toHtmlEscaped());
    impl_->refreshTuRow(index);
}

void TestPage::setProgress(const QString& detail, bool pointCanStart, bool canForceOk)
{
    impl_->progressText->setText(detail);
    impl_->confirm->setVisible(pointCanStart);
    impl_->skipButton->setEnabled(canForceOk);
    impl_->skipHotspot->setEnabled(canForceOk);
    if (!canForceOk) impl_->skipButton->hide();
}

void TestPage::setRetryAvailable(bool available)
{
    impl_->retry->setVisible(available);
    impl_->proceed->setVisible(available);
}

void TestPage::setBbPrSignalAvailable(bool available)
{
    impl_->bbPrSignal->setEnabled(available);
}

void TestPage::setSeries(const QVector<double>& first, const QVector<double>& second)
{
    impl_->plot->setSeries(first, second);
}

void TestPage::setVoltage(const QString& before, const QString& after, const QString& error)
{
    impl_->referenceBefore->setText(before.isEmpty() ? QStringLiteral("—") : before);
    impl_->referenceAfter->setText(after.isEmpty() ? QStringLiteral("—") : after);
    impl_->maximumError->setText(error.isEmpty() ? QStringLiteral("—") : error);
}

void TestPage::appendRunEvent(const tu::RunEvent& event)
{
    impl_->journal->appendRunEvent(event);
}

void TestPage::finishRun(const tu::ScenarioRunResult& result)
{
    impl_->journal->finishRun();
    impl_->stopButton->setEnabled(false);
    impl_->runtimeBack->setEnabled(true);
    setProgress({}, false);
    impl_->tuFlow->completeRun(result);
    showFlow();
}
