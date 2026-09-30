#include "station_admin_dialog.h"

#include "orbita_stand/project.h"
#include "orbita_stand/station_session.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <set>

namespace {

QTableWidget* table(const QStringList& headers, QWidget* parent)
{
    auto* value = new QTableWidget(parent);
    value->setColumnCount(headers.size());
    value->setHorizontalHeaderLabels(headers);
    value->setEditTriggers(QAbstractItemView::NoEditTriggers);
    value->setSelectionBehavior(QAbstractItemView::SelectRows);
    value->verticalHeader()->setVisible(false);
    value->horizontalHeader()->setStretchLastSection(true);
    return value;
}

void setRow(QTableWidget* target, int row, const QStringList& columns)
{
    target->insertRow(row);
    for (int column = 0; column < columns.size(); ++column)
        target->setItem(row, column, new QTableWidgetItem(columns[column]));
}

} // namespace

StationAdminDialog::StationAdminDialog(
    const orbita::stand::ProjectDefinition& project,
    const orbita::stand::StationSession& session,
    QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("stationAdminDialog"));
    setWindowTitle(QStringLiteral("Администрирование станции"));
    resize(1050, 720);
    auto* root = new QVBoxLayout(this);
    auto* identity = new QLabel(QStringLiteral("Проект: %1 · версия %2\nПрофиль: %3\n"
        "Сведения доступны только для просмотра; прямые команды приборам отсюда не отправляются.")
        .arg(QString::fromStdString(project.id), QString::fromStdString(project.version),
             QString::fromStdString(session.profile().id)), this);
    identity->setWordWrap(true);
    root->addWidget(identity);

    auto* tabs = new QTabWidget(this);
    auto* connections = table({QStringLiteral("Соединение"), QStringLiteral("Настройка профиля")}, tabs);
    int row = 0;
    for (const auto& [name, value] : session.profile().connections)
        setRow(connections, row++, {QString::fromStdString(name), QString::fromStdString(value)});
    if (!row) setRow(connections, row, {QStringLiteral("—"), QStringLiteral("Не задано")});
    tabs->addTab(connections, QStringLiteral("Соединения"));

    const auto resources = session.equipment().resources();
    std::set<std::string> bound;
    for (const auto& resource : resources) bound.insert(resource.id);
    const auto states = session.leases().states();
    auto* components = table({QStringLiteral("Компонент"), QStringLiteral("Тип"),
        QStringLiteral("Включён"), QStringLiteral("Binding"), QStringLiteral("Состояние")}, tabs);
    row = 0;
    for (const auto& component : session.profile().components) {
        const auto& bindings = component.bindings.empty() ? component.capabilities : component.bindings;
        const QString bindingText = bindings.empty() ? QStringLiteral("—")
            : QString::fromStdString(bindings.front());
        const bool isBound = !bindings.empty() && bound.count(bindings.front()) != 0;
        const auto state = states.find(bindings.empty() ? std::string() : bindings.front());
        const QString diagnostic = !component.enabled ? QStringLiteral("Отключён")
            : state != states.end() ? QString::fromLatin1(orbita::stand::toString(state->second.state))
            : isBound ? QStringLiteral("Привязан") : QStringLiteral("Не проверен / не привязан");
        setRow(components, row++, {QString::fromStdString(component.id),
            QString::fromStdString(component.kind),
            component.enabled ? QStringLiteral("Да") : QStringLiteral("Нет"),
            bindingText, diagnostic});
    }
    if (!row) setRow(components, row, {QStringLiteral("—"), QStringLiteral("—"),
        QStringLiteral("—"), QStringLiteral("—"), QStringLiteral("Компоненты не заданы")});
    tabs->addTab(components, QStringLiteral("Оборудование и ресурсы"));

    auto* diagnostics = new QPlainTextEdit(tabs);
    diagnostics->setObjectName(QStringLiteral("stationAdminDiagnostics"));
    diagnostics->setReadOnly(true);
    QStringList lines;
    for (const auto& descriptor : resources) {
        const auto state = states.find(descriptor.id);
        const QString lifecycle = state == states.end()
            ? QStringLiteral("не использовался")
            : QStringLiteral("%1 · %2").arg(
                QString::fromLatin1(orbita::stand::toString(state->second.state)),
                QString::fromStdString(state->second.reason));
        lines.push_back(QStringLiteral("Ресурс %1: %2")
            .arg(QString::fromStdString(descriptor.id), lifecycle));
    }
    for (const auto& diagnostic : session.equipmentPlugins().diagnostics())
        lines.push_back(QStringLiteral("Плагин: %1").arg(QString::fromStdString(diagnostic)));
    if (lines.isEmpty()) lines.push_back(QStringLiteral("Диагностика пока отсутствует."));
    diagnostics->setPlainText(lines.join('\n'));
    tabs->addTab(diagnostics, QStringLiteral("Диагностика"));
    root->addWidget(tabs, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    root->addWidget(buttons);
}
