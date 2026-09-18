#include <QAbstractItemView>
#include <QApplication>
#include <QBoxLayout>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

namespace {

constexpr auto kBg = "#08131d";
constexpr auto kPanel = "#102333";
constexpr auto kPanel2 = "#132a3d";
constexpr auto kPanel3 = "#0e1e2c";
constexpr auto kLine = "#264257";
constexpr auto kLineSoft = "#1a3346";
constexpr auto kText = "#eaf4fb";
constexpr auto kMuted = "#8ea6b7";
constexpr auto kDim = "#61788a";
constexpr auto kBlue = "#2e7de9";
constexpr auto kBlue2 = "#58a5ff";
constexpr auto kGreen = "#35cf79";
constexpr auto kCyan = "#61d5e8";

QWidget* rootPageFor(QWidget* child, QWidget* root)
{
    if (!child || !root) return nullptr;
    QWidget* current = child;
    while (current && current != root) {
        auto* parent = current->parentWidget();
        if (auto* stack = qobject_cast<QStackedWidget*>(parent)) {
            if (stack->parentWidget() == root) return current;
        }
        current = parent;
    }
    return nullptr;
}

QLayout* layoutContaining(QLayout* root, QWidget* widget)
{
    if (!root || !widget) return nullptr;
    for (int i = 0; i < root->count(); ++i) {
        QLayoutItem* item = root->itemAt(i);
        if (!item) continue;
        if (item->widget() == widget) return root;
        if (auto* nested = item->layout()) {
            if (auto* found = layoutContaining(nested, widget)) return found;
        }
    }
    return nullptr;
}

QPushButton* buttonByText(QWidget* root, const QString& text)
{
    if (!root) return nullptr;
    for (auto* button : root->findChildren<QPushButton*>()) {
        if (button->text() == text) return button;
    }
    return nullptr;
}

QLabel* labelByText(QWidget* root, const QString& text)
{
    if (!root) return nullptr;
    for (auto* label : root->findChildren<QLabel*>()) {
        if (label->text() == text) return label;
    }
    return nullptr;
}

void polish(QWidget* widget)
{
    if (!widget) return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

void styleSession(QWidget* root)
{
    auto* registry = root->findChild<QTableWidget*>(QStringLiteral("productionRegistryTable"));
    auto* queue = root->findChild<QTableWidget*>(QStringLiteral("productionSessionTable"));
    auto* operatorSelector = root->findChild<QComboBox*>(QStringLiteral("productionOperatorSelector"));
    auto* stageSelector = root->findChild<QComboBox*>(QStringLiteral("productionStage"));
    auto* enter = root->findChild<QPushButton*>(QStringLiteral("enterPreparation"));
    if (!registry || !queue || !operatorSelector || !stageSelector || !enter) return;

    QWidget* session = rootPageFor(registry, root);
    auto* outer = session ? qobject_cast<QVBoxLayout*>(session->layout()) : nullptr;
    if (!session || !outer) return;

    session->setObjectName(QStringLiteral("productionSessionPrototype"));
    session->setStyleSheet(QStringLiteral(
        "#productionSessionPrototype{background:#08131d;color:#eaf4fb;}"
        "#productionSessionPrototype QFrame[panel='true']{background:#071927;border:1px solid #264257;border-radius:7px;}"
        "#productionSessionPrototype QLineEdit,#productionSessionPrototype QComboBox{background:#071b2a;color:#eaf4fb;border:1px solid #264257;border-radius:5px;padding:0 10px;}"
        "#productionSessionPrototype QLineEdit:focus,#productionSessionPrototype QComboBox:focus{border-color:#4f9bd0;}"
        "#productionSessionPrototype QTableWidget{background:#071927;color:#eaf4fb;border:0;gridline-color:#1a3346;selection-background-color:#123b58;selection-color:#eaf4fb;}"
        "#productionSessionPrototype QTableWidget::item{border-bottom:1px solid #1a3346;padding:5px 10px;}"
        "#productionSessionPrototype QPushButton{border-radius:5px;}"));

    // v1.3 session uses only 14 px outer horizontal padding and 10 px gaps.
    outer->setContentsMargins(14, 0, 14, 12);
    outer->setSpacing(10);

    if (auto* brandRow = qobject_cast<QHBoxLayout*>(outer->itemAt(0) ? outer->itemAt(0)->layout() : nullptr)) {
        brandRow->setContentsMargins(0, 0, 0, 0);
        brandRow->setSpacing(12);

        if (!session->findChild<QPushButton*>(QStringLiteral("prototypeSessionBack"))) {
            auto* back = new QPushButton(QStringLiteral("← Главная"), session);
            back->setObjectName(QStringLiteral("prototypeSessionBack"));
            back->setFixedHeight(32);
            back->setStyleSheet(QStringLiteral(
                "QPushButton{background:transparent;color:#eaf4fb;border:1px solid #264257;padding:5px 10px;}"
                "QPushButton:hover{border-color:#58a5ff;}"));
            QObject::connect(back, &QPushButton::clicked, root, [root] {
                QMetaObject::invokeMethod(root, "homeRequested", Qt::QueuedConnection);
            });
            brandRow->insertWidget(0, back);

            auto* mark = new QLabel(QStringLiteral("◎"), session);
            mark->setObjectName(QStringLiteral("prototypeBrandMark"));
            mark->setStyleSheet(QStringLiteral("color:#61d5e8;font-size:27px;"));
            brandRow->insertWidget(1, mark);
        }

        QLabel* brand = nullptr;
        QLabel* mode = nullptr;
        for (int i = 0; i < brandRow->count(); ++i) {
            auto* label = qobject_cast<QLabel*>(brandRow->itemAt(i)->widget());
            if (!label || label->objectName() == QStringLiteral("prototypeBrandMark")) continue;
            if (label->text().contains(QStringLiteral("MILTECHSTATION"), Qt::CaseInsensitive)) brand = label;
            if (label->text() == QStringLiteral("ПРОИЗВОДСТВО")) mode = label;
        }
        if (brand) {
            brand->setText(QStringLiteral("MilTechStation / КТМА\nУБСИ · производственный контур"));
            brand->setMinimumHeight(48);
            brand->setStyleSheet(QStringLiteral(
                "color:#eaf4fb;font-size:16px;font-weight:700;border-left:1px solid #264257;padding-left:12px;"));
        }
        if (mode) {
            mode->setStyleSheet(QStringLiteral(
                "color:#5cf0b1;border:1px solid rgba(53,207,121,115);background:rgba(53,207,121,20);"
                "border-radius:4px;padding:5px 9px;font-size:10px;font-weight:700;"));
        }
    }

    if (outer->count() > 1) {
        if (auto* title = qobject_cast<QLabel*>(outer->itemAt(1)->widget())) {
            title->setText(QStringLiteral("Производственная сессия"));
            title->setStyleSheet(QStringLiteral("color:#eaf4fb;font-size:22px;font-weight:700;"));
        }
    }
    if (outer->count() > 2) {
        if (auto* subtitle = qobject_cast<QLabel*>(outer->itemAt(2)->widget())) {
            subtitle->setText(QStringLiteral(
                "Сформируйте очередь зарегистрированных изделий. Один оператор, этап и объём относятся ко всей сессии."));
            subtitle->setStyleSheet(QStringLiteral("color:#8ea6b7;font-size:12px;"));
        }
    }

    if (auto* body = layoutContaining(outer, registry->parentWidget())) body->setSpacing(10);

    auto setupListPanel = [](QTableWidget* table) {
        if (!table) return;
        table->setShowGrid(false);
        table->setAlternatingRowColors(false);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->verticalHeader()->hide();
        table->verticalHeader()->setDefaultSectionSize(42);
        table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    };
    setupListPanel(registry);
    setupListPanel(queue);
    queue->horizontalHeader()->hide();
    queue->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    queue->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    queue->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    if (auto* panel = registry->parentWidget()) {
        if (auto* layout = qobject_cast<QVBoxLayout*>(panel->layout())) {
            layout->setContentsMargins(8, 8, 8, 8);
            layout->setSpacing(6);
        }
    }
    if (auto* panel = queue->parentWidget()) {
        if (auto* layout = qobject_cast<QVBoxLayout*>(panel->layout())) {
            layout->setContentsMargins(8, 8, 8, 8);
            layout->setSpacing(6);
        }
    }

    operatorSelector->setFixedHeight(35);
    stageSelector->setFixedHeight(35);
    QWidget* config = operatorSelector->parentWidget();
    if (config) {
        if (auto* configLayout = qobject_cast<QVBoxLayout*>(config->layout())) {
            configLayout->setContentsMargins(10, 10, 10, 10);
            configLayout->setSpacing(6);

            QPushButton* addOperator = nullptr;
            for (auto* button : config->findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly)) {
                if (button->property("scope").isValid()) continue;
                if (button->text().contains(QStringLiteral("оператора"), Qt::CaseInsensitive)) {
                    addOperator = button;
                    break;
                }
            }
            if (addOperator && !config->findChild<QWidget*>(QStringLiteral("prototypeOperatorRow"))) {
                const int comboIndex = configLayout->indexOf(operatorSelector);
                configLayout->removeWidget(operatorSelector);
                configLayout->removeWidget(addOperator);
                auto* rowHost = new QWidget(config);
                rowHost->setObjectName(QStringLiteral("prototypeOperatorRow"));
                auto* row = new QHBoxLayout(rowHost);
                row->setContentsMargins(0, 0, 0, 0);
                row->setSpacing(5);
                operatorSelector->setParent(rowHost);
                addOperator->setParent(rowHost);
                addOperator->setText(QStringLiteral("+"));
                addOperator->setFixedSize(32, 35);
                row->addWidget(operatorSelector, 1);
                row->addWidget(addOperator);
                configLayout->insertWidget(qMax(0, comboIndex), rowHost);
            }
        }

        for (auto* button : config->findChildren<QPushButton*>()) {
            const QString scope = button->property("scope").toString();
            if (scope.isEmpty()) continue;
            QString title;
            QString description;
            if (scope == QStringLiteral("УБСИ ПО ТУ")) {
                title = QStringLiteral("Полная УБСИ");
                description = QStringLiteral("Питание · ЯЛК-96 · ЯТП · ЯВП-8");
            } else if (scope == QStringLiteral("ЯЛК-96")) {
                title = QStringLiteral("ЯЛК-96");
                description = QStringLiteral("Отдельная ячейка");
            } else if (scope == QStringLiteral("ЯТП")) {
                title = QStringLiteral("ЯТП");
                description = QStringLiteral("30 каналов");
            } else if (scope == QStringLiteral("ЯВП-8")) {
                title = QStringLiteral("ЯВП-8");
                description = QStringLiteral("8 каналов");
            }
            button->setText(title + QStringLiteral("\n") + description);
            button->setMinimumHeight(53);
            button->setStyleSheet(QStringLiteral(
                "QPushButton{background:#091b29;color:#eaf4fb;border:1px solid #264257;border-radius:6px;"
                "padding:7px 10px;text-align:left;font-size:12px;}"
                "QPushButton:hover{border-color:#58a5ff;}"
                "QPushButton:checked{background:#123b58;border-color:#5aa8ff;border-left:3px solid #5aa8ff;}"));
        }
    }

    if (auto* action = enter->parentWidget()) {
        action->setStyleSheet(QStringLiteral(
            "background:#071927;border:1px solid #264257;border-radius:6px;"));
        if (auto* row = qobject_cast<QHBoxLayout*>(action->layout())) {
            row->setContentsMargins(10, 7, 10, 7);
            row->setSpacing(10);
        }
    }
    enter->setMinimumHeight(36);
}

void stylePreparation(QWidget* root)
{
    auto* table = root->findChild<QTableWidget*>(QStringLiteral("equipmentTable"));
    if (!table) return;
    QWidget* prep = rootPageFor(table, root);
    auto* layout = prep ? qobject_cast<QVBoxLayout*>(prep->layout()) : nullptr;
    if (!prep || !layout) return;

    prep->setObjectName(QStringLiteral("productionPreparationPrototype"));
    prep->setStyleSheet(QStringLiteral(
        "#productionPreparationPrototype{background:#08131d;color:#eaf4fb;}"
        "#productionPreparationPrototype QTableWidget{background:#0e1e2c;color:#eaf4fb;border:1px solid #264257;"
        "border-radius:8px;gridline-color:#1a3346;selection-background-color:#123b58;}"
        "#productionPreparationPrototype QTableWidget::item{border-bottom:1px solid #1a3346;padding:0 16px;}"));
    layout->setContentsMargins(18, 12, 18, 18);
    layout->setSpacing(14);

    if (auto* title = labelByText(prep, QStringLiteral("Подготовка"))) {
        title->setText(QStringLiteral("Подготовка к проверке"));
        title->setStyleSheet(QStringLiteral("font-size:24px;font-weight:700;color:#eaf4fb;"));
    }
    for (auto* label : prep->findChildren<QLabel*>()) {
        if (label->text().contains(QStringLiteral("Проверьте только оборудование"))) {
            label->setText(QStringLiteral("Проверяется только оборудование, требуемое выбранным сценарием."));
            label->setStyleSheet(QStringLiteral("color:#8ea6b7;"));
        }
    }

    // The web prototype presents equipment as simple 52 px name/status rows.
    table->horizontalHeader()->hide();
    table->verticalHeader()->hide();
    table->setShowGrid(false);
    table->verticalHeader()->setDefaultSectionSize(52);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table->setColumnWidth(1, 180);
    table->setColumnHidden(2, true);
    table->setSelectionMode(QAbstractItemView::NoSelection);

    if (auto* back = buttonByText(prep, QStringLiteral("← Сессия"))) {
        back->setStyleSheet(QStringLiteral(
            "QPushButton{background:transparent;color:#eaf4fb;border:1px solid #264257;border-radius:5px;padding:7px 10px;}"
            "QPushButton:hover{border-color:#58a5ff;}"));
    }
}

QStackedWidget* sidebarFor(QWidget* root)
{
    auto* sideTitle = root->findChild<QLabel*>(QStringLiteral("frozenSideTitle"));
    QWidget* current = sideTitle;
    while (current) {
        if (auto* stack = qobject_cast<QStackedWidget*>(current)) return stack;
        current = current->parentWidget();
    }
    return nullptr;
}

void styleRuntime(QWidget* root)
{
    auto* context = root->findChild<QLabel*>(QStringLiteral("frozenProcedureContext"));
    auto* footer = root->findChild<QFrame*>(QStringLiteral("productionTelemetryFooter"));
    if (!context || !footer) return;
    QWidget* runtime = rootPageFor(context, root);
    auto* layout = runtime ? qobject_cast<QVBoxLayout*>(runtime->layout()) : nullptr;
    if (!runtime || !layout) return;

    runtime->setObjectName(QStringLiteral("productionRuntimePrototype"));
    runtime->setStyleSheet(QStringLiteral(
        "#productionRuntimePrototype{background:#08131d;color:#eaf4fb;}"
        "#productionRuntimePrototype QFrame[panel='true']{border-radius:0;}"));
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget* header = layout->count() > 0 ? layout->itemAt(0)->widget() : nullptr;
    QWidget* strip = layout->count() > 1 ? layout->itemAt(1)->widget() : nullptr;
    if (header) {
        header->setStyleSheet(QStringLiteral(
            "background:#091722;border:0;border-bottom:1px solid #264257;"));
        if (auto* row = qobject_cast<QHBoxLayout*>(header->layout())) {
            row->setContentsMargins(14, 0, 14, 0);
            row->setSpacing(14);
            if (!header->findChild<QLabel*>(QStringLiteral("prototypeRuntimeOperator"))) {
                auto* operatorLabel = new QLabel(QStringLiteral("Оператор: —"), header);
                operatorLabel->setObjectName(QStringLiteral("prototypeRuntimeOperator"));
                operatorLabel->setStyleSheet(QStringLiteral("color:#8ea6b7;font-size:13px;"));
                int stopIndex = row->count();
                for (int i = 0; i < row->count(); ++i) {
                    auto* button = qobject_cast<QPushButton*>(row->itemAt(i)->widget());
                    if (button && button->text() == QStringLiteral("Остановить")) {
                        stopIndex = i;
                        break;
                    }
                }
                row->insertWidget(stopIndex, operatorLabel);
            }
        }
    }
    if (strip) {
        strip->setStyleSheet(QStringLiteral(
            "background:#0c1a26;border:0;border-bottom:1px solid #264257;"));
    }

    if (auto* sidebar = sidebarFor(root)) {
        sidebar->setStyleSheet(QStringLiteral(
            "QStackedWidget{background:#0a1722;border:0;border-right:1px solid #264257;}"));
    }

    footer->setStyleSheet(QStringLiteral(
        "#productionTelemetryFooter{background:#08141e;border:0;border-top:1px solid #264257;border-radius:0;}"));
    if (auto* row = qobject_cast<QHBoxLayout*>(footer->layout())) {
        row->setContentsMargins(14, 10, 14, 10);
        row->setSpacing(12);
        if (!footer->findChild<QFrame*>(QStringLiteral("prototypeProcedureProgress"))) {
            auto* box = new QFrame(footer);
            box->setObjectName(QStringLiteral("prototypeProcedureProgress"));
            box->setStyleSheet(QStringLiteral(
                "#prototypeProcedureProgress{background:#091722;border:1px solid #1a3346;border-radius:5px;}"));
            auto* boxLayout = new QVBoxLayout(box);
            boxLayout->setContentsMargins(12, 10, 12, 10);
            boxLayout->setSpacing(6);
            auto* cap = new QLabel(QStringLiteral("ХОД ТЕКУЩЕЙ ПРОЦЕДУРЫ"), box);
            cap->setStyleSheet(QStringLiteral(
                "color:#8ea6b7;font-size:10px;font-weight:700;letter-spacing:1px;"));
            auto* procedure = new QLabel(QStringLiteral("Ожидание запуска"), box);
            procedure->setObjectName(QStringLiteral("prototypeProcedureName"));
            procedure->setStyleSheet(QStringLiteral("color:#eaf4fb;font-size:14px;font-weight:700;"));
            procedure->setWordWrap(true);
            auto* progress = new QLabel(QStringLiteral("—"), box);
            progress->setObjectName(QStringLiteral("prototypeProcedureProgressValue"));
            progress->setStyleSheet(QStringLiteral("color:#8ea6b7;font-size:12px;"));
            progress->setWordWrap(true);
            boxLayout->addWidget(cap);
            boxLayout->addWidget(procedure);
            boxLayout->addWidget(progress, 1);
            row->addWidget(box);
        }
    }

    if (!root->findChild<QTimer*>(QStringLiteral("prototypeRuntimeMirrorTimer"))) {
        auto* timer = new QTimer(root);
        timer->setObjectName(QStringLiteral("prototypeRuntimeMirrorTimer"));
        timer->setInterval(150);
        QObject::connect(timer, &QTimer::timeout, root, [root, context] {
            if (auto* name = root->findChild<QLabel*>(QStringLiteral("prototypeProcedureName")))
                name->setText(context->text());

            QLabel* progressSource = nullptr;
            if (auto* parent = context->parentWidget()) {
                for (auto* label : parent->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly)) {
                    if (label == context) continue;
                    if ((label->alignment() & Qt::AlignRight) == Qt::AlignRight) {
                        progressSource = label;
                        break;
                    }
                }
            }
            if (auto* progress = root->findChild<QLabel*>(QStringLiteral("prototypeProcedureProgressValue")))
                progress->setText(progressSource ? progressSource->text() : QStringLiteral("—"));

            const QString operatorName = root->findChild<QLineEdit*>(QStringLiteral("operatorName"))
                ? root->findChild<QLineEdit*>(QStringLiteral("operatorName"))->text().trimmed()
                : QString();
            if (auto* op = root->findChild<QLabel*>(QStringLiteral("prototypeRuntimeOperator")))
                op->setText(operatorName.isEmpty()
                    ? QStringLiteral("Оператор: —")
                    : QStringLiteral("Оператор: %1").arg(operatorName));
        });
        timer->start();
    }
}

void styleFinish(QWidget* root)
{
    auto* power = root->findChild<QLabel*>(QStringLiteral("finishPowerSummary"));
    if (!power) return;
    QWidget* page = power;
    while (page && !qobject_cast<QStackedWidget*>(page->parentWidget()))
        page = page->parentWidget();
    auto* layout = page ? qobject_cast<QVBoxLayout*>(page->layout()) : nullptr;
    if (!page || !layout) return;
    page->setObjectName(QStringLiteral("productionFinishPrototype"));
    page->setStyleSheet(QStringLiteral(
        "#productionFinishPrototype{background:#08131d;color:#eaf4fb;}"
        "#productionFinishPrototype QFrame[panel='true']{background:#0e1e2c;border:1px solid #264257;border-radius:9px;}"));
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
}

void hideLegacyProductionInjection(QWidget* root)
{
    if (auto* panel = root->findChild<QFrame*>(QStringLiteral("productionStagePanel"))) {
        for (auto* combo : panel->findChildren<QComboBox*>()) {
            if (combo->objectName() == QStringLiteral("productionStage"))
                combo->setObjectName(QStringLiteral("legacyProductionStage"));
        }
        panel->hide();
    }
}

void updateResponsiveGeometry(QWidget* root)
{
    if (!root) return;
    const bool compact = root->width() > 0 && root->width() <= 1650;
    if (auto* sidebar = sidebarFor(root)) {
        const int width = compact ? 216 : 240;
        sidebar->setMinimumWidth(width);
        sidebar->setMaximumWidth(width);
    }
    if (auto* footer = root->findChild<QFrame*>(QStringLiteral("productionTelemetryFooter")))
        footer->setFixedHeight(compact ? 150 : 168);
    if (auto* progress = root->findChild<QFrame*>(QStringLiteral("prototypeProcedureProgress")))
        progress->setFixedWidth(compact ? 360 : 420);
}

void applyPrototype(QWidget* root)
{
    if (!root) return;
    hideLegacyProductionInjection(root);
    if (!root->property("productionPrototypePortApplied").toBool()) {
        styleSession(root);
        stylePreparation(root);
        styleRuntime(root);
        styleFinish(root);
        root->setProperty("productionPrototypePortApplied", true);
    }
    updateResponsiveGeometry(root);
}

class PrototypePortFilter final : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (!widget) return QObject::eventFilter(watched, event);

        if (widget->objectName() == QStringLiteral("productionStagePanel")
            && event->type() == QEvent::Show) {
            QPointer<QWidget> guard(widget);
            QTimer::singleShot(0, widget, [guard] { if (guard) guard->hide(); });
        }

        if (widget->objectName() != QStringLiteral("operatorTestPage"))
            return QObject::eventFilter(watched, event);

        if (event->type() == QEvent::Show || event->type() == QEvent::Polish
            || event->type() == QEvent::Resize) {
            QPointer<QWidget> guard(widget);
            QTimer::singleShot(0, widget, [guard] {
                if (guard) applyPrototype(guard.data());
            });
        }
        return QObject::eventFilter(watched, event);
    }
};

void installProductionPrototypePort()
{
    if (!qApp) return;
    auto* filter = new PrototypePortFilter(qApp);
    qApp->installEventFilter(filter);
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(installProductionPrototypePort)
