#include "generic_check_dialog.h"

#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

QString verdictText(orbita::stand::RunVerdict verdict)
{
    using V = orbita::stand::RunVerdict;
    switch (verdict) {
    case V::Ok: return QStringLiteral("НОРМА");
    case V::Fail: return QStringLiteral("НЕ НОРМА");
    case V::Incomplete: return QStringLiteral("НЕПОЛНО");
    case V::Error: return QStringLiteral("ОШИБКА");
    case V::Aborted: return QStringLiteral("ОСТАНОВЛЕНО");
    case V::NotRun: return QStringLiteral("НЕ ВЫПОЛНЕНО");
    }
    return QStringLiteral("НЕИЗВЕСТНО");
}

} // namespace

GenericCheckDialog::GenericCheckDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("genericCheckDialog"));
    setWindowTitle(QStringLiteral("Проверить изделие"));
    resize(1380, 900);
    setModal(false);

    setStyleSheet(QStringLiteral(
        "#genericCheckDialog{background:#08131d;color:#eaf4fb;}"
        "QFrame[panel='true']{background:#0e1e2c;border:1px solid #264257;border-radius:8px;}"
        "QLabel[caption='true']{color:#8ea6b7;font-size:11px;font-weight:700;}"
        "QLineEdit,QComboBox,QPlainTextEdit,QTextBrowser{background:#0a1722;color:#eaf4fb;"
        "border:1px solid #264257;border-radius:6px;padding:7px;}"
        "QPushButton{background:#132a3d;color:#eaf4fb;border:1px solid #264257;"
        "border-radius:6px;padding:8px 13px;}"
        "QPushButton:hover{border-color:#58a5ff;}"
        "QPushButton#runGeneric{background:#2e7de9;border-color:#58a5ff;font-weight:700;}"
        "QPushButton#stopGeneric{background:#3a1d24;border-color:#7e3340;}"
        "QPushButton:disabled{color:#61788a;background:#0e1e2c;}"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 22, 22, 22);
    root->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("ПРОВЕРИТЬ ИЗДЕЛИЕ"), this);
    title->setStyleSheet(QStringLiteral("font-size:25px;font-weight:800;color:#eaf4fb;"));
    root->addWidget(title);

    auto* hint = new QLabel(QStringLiteral(
        "Свободная проверка на текущем стендовом профиле. Она не регистрирует изделие и не создаёт запись "
        "в реестре поставки. Сценарий и макет отчёта задаются здесь; один и тот же тракт и оборудование "
        "можно использовать для разных изделий и проектов."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#8ea6b7;"));
    root->addWidget(hint);

    auto* setup = new QFrame(this);
    setup->setProperty("panel", true);
    auto* setupLayout = new QVBoxLayout(setup);
    setupLayout->setContentsMargins(16, 14, 16, 14);
    setupLayout->setSpacing(10);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    scenario_ = new QComboBox(setup);
    scenario_->setObjectName(QStringLiteral("genericScenario"));
    serial_ = new QLineEdit(setup);
    serial_->setObjectName(QStringLiteral("genericObjectSerial"));
    serial_->setPlaceholderText(QStringLiteral("Необязательно для свободной проверки"));
    form->addRow(QStringLiteral("Сценарий"), scenario_);
    form->addRow(QStringLiteral("Обозначение / заводской №"), serial_);
    setupLayout->addLayout(form);

    auto* descriptionCaption = new QLabel(QStringLiteral("ОПИСАНИЕ ПРОВЕРКИ / ИЗДЕЛИЯ"), setup);
    descriptionCaption->setProperty("caption", true);
    setupLayout->addWidget(descriptionCaption);
    description_ = new QPlainTextEdit(setup);
    description_->setObjectName(QStringLiteral("genericDescription"));
    description_->setMaximumHeight(100);
    description_->setPlaceholderText(QStringLiteral(
        "Что проверяем, в какой схеме и для чего. Текст попадёт в отчёт."));
    setupLayout->addWidget(description_);

    auto* setupActions = new QHBoxLayout;
    equipmentButton_ = new QPushButton(QStringLiteral("Проверить оборудование текущего профиля"), setup);
    editScenarioButton_ = new QPushButton(QStringLiteral("Редактировать сценарий"), setup);
    setupActions->addWidget(equipmentButton_);
    setupActions->addWidget(editScenarioButton_);
    setupActions->addStretch(1);
    setupLayout->addLayout(setupActions);
    root->addWidget(setup);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    auto* templatePanel = new QFrame(splitter);
    templatePanel->setProperty("panel", true);
    auto* templateLayout = new QVBoxLayout(templatePanel);
    templateLayout->setContentsMargins(14, 14, 14, 14);
    auto* templateCaption = new QLabel(QStringLiteral("МАКЕТ ОТЧЁТА (HTML)"), templatePanel);
    templateCaption->setProperty("caption", true);
    templateLayout->addWidget(templateCaption);
    auto* placeholders = new QLabel(QStringLiteral(
        "Поля: {{description}}, {{object_serial}}, {{scenario_id}}, {{scenario_title}}, "
        "{{scenario_version}}, {{profile_version}}, {{verdict}}, {{started_at}}, {{finished_at}}, "
        "{{steps}}, {{events}}"), templatePanel);
    placeholders->setWordWrap(true);
    placeholders->setStyleSheet(QStringLiteral("color:#8ea6b7;font-size:11px;"));
    templateLayout->addWidget(placeholders);
    reportTemplate_ = new QPlainTextEdit(templatePanel);
    reportTemplate_->setObjectName(QStringLiteral("genericReportTemplate"));
    reportTemplate_->setPlainText(defaultReportTemplate());
    QFont reportFont = reportTemplate_->font();
    reportFont.setFamily(QStringLiteral("Consolas"));
    reportTemplate_->setFont(reportFont);
    templateLayout->addWidget(reportTemplate_, 1);

    auto* resultPanel = new QFrame(splitter);
    resultPanel->setProperty("panel", true);
    auto* resultLayout = new QVBoxLayout(resultPanel);
    resultLayout->setContentsMargins(14, 14, 14, 14);
    auto* eventCaption = new QLabel(QStringLiteral("ХОД ПРОВЕРКИ"), resultPanel);
    eventCaption->setProperty("caption", true);
    resultLayout->addWidget(eventCaption);
    eventLog_ = new QTextBrowser(resultPanel);
    eventLog_->setObjectName(QStringLiteral("genericEventLog"));
    eventLog_->setMaximumHeight(220);
    resultLayout->addWidget(eventLog_);
    auto* reportCaption = new QLabel(QStringLiteral("ПРЕДПРОСМОТР ОТЧЁТА"), resultPanel);
    reportCaption->setProperty("caption", true);
    resultLayout->addWidget(reportCaption);
    reportPreview_ = new QTextBrowser(resultPanel);
    reportPreview_->setObjectName(QStringLiteral("genericReportPreview"));
    resultLayout->addWidget(reportPreview_, 1);

    splitter->addWidget(templatePanel);
    splitter->addWidget(resultPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    status_ = new QLabel(QStringLiteral("Готово к настройке проверки"), this);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_->setStyleSheet(QStringLiteral("color:#8ea6b7;"));
    root->addWidget(status_);

    auto* actions = new QHBoxLayout;
    runButton_ = new QPushButton(QStringLiteral("ЗАПУСТИТЬ ПРОВЕРКУ"), this);
    runButton_->setObjectName(QStringLiteral("runGeneric"));
    stopButton_ = new QPushButton(QStringLiteral("ОСТАНОВИТЬ"), this);
    stopButton_->setObjectName(QStringLiteral("stopGeneric"));
    stopButton_->setEnabled(false);
    saveReportButton_ = new QPushButton(QStringLiteral("Сохранить отчёт…"), this);
    saveReportButton_->setEnabled(false);
    auto* closeButton = new QPushButton(QStringLiteral("Закрыть"), this);
    actions->addWidget(runButton_);
    actions->addWidget(stopButton_);
    actions->addStretch(1);
    actions->addWidget(saveReportButton_);
    actions->addWidget(closeButton);
    root->addLayout(actions);

    connect(equipmentButton_, &QPushButton::clicked,
            this, &GenericCheckDialog::equipmentCheckRequested);
    connect(editScenarioButton_, &QPushButton::clicked,
            this, &GenericCheckDialog::editScenario);
    connect(runButton_, &QPushButton::clicked,
            this, &GenericCheckDialog::requestRun);
    connect(stopButton_, &QPushButton::clicked,
            this, &GenericCheckDialog::stopRequested);
    connect(saveReportButton_, &QPushButton::clicked,
            this, &GenericCheckDialog::saveReport);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
}

void GenericCheckDialog::setScenarios(const QVector<GenericScenarioEntry>& scenarios)
{
    scenario_->clear();
    for (const auto& entry : scenarios) {
        const QString label = QStringLiteral("%1  ·  %2  ·  v%3")
            .arg(entry.title, entry.id, entry.version);
        scenario_->addItem(label, entry.path);
    }
    const bool available = scenario_->count() > 0;
    runButton_->setEnabled(available);
    editScenarioButton_->setEnabled(available);
    status_->setText(available
        ? QStringLiteral("Доступно сценариев: %1").arg(scenario_->count())
        : QStringLiteral("Нет валидных сценариев в каталоге scenarios"));
}

void GenericCheckDialog::setRunning(bool running, const QString& detail)
{
    scenario_->setEnabled(!running);
    serial_->setEnabled(!running);
    description_->setEnabled(!running);
    reportTemplate_->setEnabled(!running);
    equipmentButton_->setEnabled(!running);
    editScenarioButton_->setEnabled(!running && scenario_->count() > 0);
    runButton_->setEnabled(!running && scenario_->count() > 0);
    stopButton_->setEnabled(running);
    if (running) {
        eventLog_->clear();
        reportPreview_->clear();
        reportHtml_.clear();
        saveReportButton_->setEnabled(false);
    }
    if (!detail.isEmpty()) status_->setText(detail);
}

void GenericCheckDialog::appendEvent(const orbita::stand::RunEvent& event)
{
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        event.timestamp.time_since_epoch()).count();
    const QString time = QDateTime::fromMSecsSinceEpoch(milliseconds).toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString line = QStringLiteral("%1  [%2]  %3  —  %4")
        .arg(time,
             QString::fromStdString(event.stage),
             verdictText(event.verdict),
             QString::fromStdString(event.message));
    eventLog_->append(line.toHtmlEscaped());
}

void GenericCheckDialog::setReportHtml(const QString& html, const QString& summary)
{
    reportHtml_ = html;
    reportPreview_->setHtml(html);
    saveReportButton_->setEnabled(!reportHtml_.isEmpty());
    setRunning(false, summary);
}

void GenericCheckDialog::requestRun()
{
    const QString path = selectedScenarioPath();
    if (path.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Проверка"),
                                 QStringLiteral("Выберите сценарий."));
        return;
    }
    emit runRequested(path, serial_->text().trimmed(), description_->toPlainText().trimmed(),
                      reportTemplate_->toPlainText());
}

void GenericCheckDialog::editScenario()
{
    const QString path = selectedScenarioPath();
    if (!path.isEmpty()) emit editScenarioRequested(path);
}

void GenericCheckDialog::saveReport()
{
    if (reportHtml_.isEmpty()) return;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Сохранить отчёт"), QStringLiteral("check_report.html"),
        QStringLiteral("HTML (*.html);;Все файлы (*.*)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QStringLiteral("Отчёт"), file.errorString());
        return;
    }
    file.write(reportHtml_.toUtf8());
    if (!file.commit()) {
        QMessageBox::critical(this, QStringLiteral("Отчёт"), file.errorString());
        return;
    }
    status_->setText(QStringLiteral("Отчёт сохранён: %1").arg(path));
}

QString GenericCheckDialog::selectedScenarioPath() const
{
    return scenario_->currentData().toString();
}

QString GenericCheckDialog::defaultReportTemplate() const
{
    return QStringLiteral(
        "<!doctype html>\n"
        "<html><head><meta charset=\"utf-8\"><title>Отчёт проверки</title>\n"
        "<style>body{font-family:Segoe UI,Arial,sans-serif;margin:32px;color:#17202a;}"
        "table{border-collapse:collapse;width:100%;margin:12px 0;}"
        "th,td{border:1px solid #bcc6d0;padding:6px 8px;text-align:left;}"
        "th{background:#eef2f6;}code{font-family:Consolas,monospace;}</style></head><body>\n"
        "<h1>Отчёт проверки</h1>\n"
        "<p><b>Описание:</b> {{description}}</p>\n"
        "<p><b>Объект:</b> {{object_serial}}</p>\n"
        "<p><b>Сценарий:</b> {{scenario_title}} (<code>{{scenario_id}}</code>), версия {{scenario_version}}</p>\n"
        "<p><b>Профиль стенда:</b> {{profile_version}}</p>\n"
        "<p><b>Начало:</b> {{started_at}}<br><b>Окончание:</b> {{finished_at}}</p>\n"
        "<h2>Результат: {{verdict}}</h2>\n"
        "<h2>Этапы</h2>{{steps}}\n"
        "<h2>Журнал</h2>{{events}}\n"
        "</body></html>\n");
}
