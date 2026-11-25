#include "station_admin_dialog.h"

#include "orbita_stand/project.h"
#include "orbita_stand/station_session.h"

#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QSerialPortInfo>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrentRun>

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

    // Читаем только сведения ОС об интерфейсах. Эта проверка не открывает
    // приборы и не предлагает менять адреса адаптеров без явного назначения.
    auto* environmentTable = table({QStringLiteral("Компонент"), QStringLiteral("Проверка"),
        QStringLiteral("Значение")}, tabs);
    environmentTable->setObjectName(QStringLiteral("stationEnvironmentPlan"));
    setRow(environmentTable, 0, {QStringLiteral("—"), QStringLiteral("Проверка интерфейсов…"), QStringLiteral("—")});
    tabs->addTab(environmentTable, QStringLiteral("Окружение"));
    auto* environmentWatcher = new QFutureWatcher<orbita::stand::EnvironmentPlan>(this);
    connect(environmentWatcher, &QFutureWatcher<orbita::stand::EnvironmentPlan>::finished, this,
        [environmentWatcher, environmentTable] {
            environmentTable->setRowCount(0);
            int environmentRow = 0;
            try {
                const auto environment = environmentWatcher->result();
                for (const auto& issue : environment.issues) {
                    QString reason;
                    switch (issue.code) {
                    case orbita::stand::EnvironmentIssueCode::MissingResource: reason = QStringLiteral("Ресурс не объявлен в профиле"); break;
                    case orbita::stand::EnvironmentIssueCode::AmbiguousResource: reason = QStringLiteral("Несколько компонентов для ресурса"); break;
                    case orbita::stand::EnvironmentIssueCode::DisabledResource: reason = QStringLiteral("Компонент отключён"); break;
                    case orbita::stand::EnvironmentIssueCode::CapabilityMismatch: reason = QStringLiteral("Возможность не объявлена"); break;
                    case orbita::stand::EnvironmentIssueCode::MissingLocalAddress: reason = QStringLiteral("Локальный адрес не найден среди активных интерфейсов"); break;
                    case orbita::stand::EnvironmentIssueCode::MissingSerialPort: reason = QStringLiteral("Последовательный порт не обнаружен"); break;
                    case orbita::stand::EnvironmentIssueCode::UncheckedPort: reason = QStringLiteral("Тип порта не распознан; требуется ручная проверка"); break;
                    }
                    setRow(environmentTable, environmentRow++, {QString::fromStdString(issue.component), reason,
                                                                QString::fromStdString(issue.subject)});
                }
                if (!environmentRow) setRow(environmentTable, 0, {QStringLiteral("—"),
                    QStringLiteral("Замечаний к объявленным интерфейсам нет; связь с приборами не проверялась"), QStringLiteral("—")});
            } catch (const std::exception& error) {
                setRow(environmentTable, 0, {QStringLiteral("—"), QStringLiteral("Не удалось проверить интерфейсы"),
                                              QString::fromUtf8(error.what())});
            }
        });
    environmentWatcher->setFuture(QtConcurrent::run([profile = session.profile()] {
        orbita::stand::EnvironmentInventory inventory;
        for (const auto& interface : QNetworkInterface::allInterfaces()) {
            if (!(interface.flags() & QNetworkInterface::IsUp)) continue;
            for (const auto& entry : interface.addressEntries())
                inventory.localAddresses.insert(entry.ip().toString().toUtf8().toStdString());
        }
        for (const auto& port : QSerialPortInfo::availablePorts())
            inventory.serialPorts.insert(port.portName().toUtf8().toStdString());
        return orbita::stand::planEnvironment(profile, {}, inventory);
    }));

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
