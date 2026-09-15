#include "test_page.h"
#define OverloadOverview LegacyOverloadOverview
#include "test_page_ui.h"
#undef OverloadOverview
#include "frozen_ui_widgets.h"
#include "test_page_impl.h"
#include "tu_flow_widget.h"

#include <QEvent>
#include <QInputDialog>
#include <QRegularExpression>

namespace {

QString productionScenarioForScope(const QString& scope)
{
    if (scope == QStringLiteral("ЯЛК-96")) return QStringLiteral("PROD_YALK");
    if (scope == QStringLiteral("ЯТП")) return QStringLiteral("PROD_YTP");
    if (scope == QStringLiteral("ЯВП-8")) return QStringLiteral("PROD_YVP");
    return QStringLiteral("PROD_FULL");
}

QString productionScenarioTitle(const QString& code)
{
    if (code == QStringLiteral("PROD_YALK")) return QStringLiteral("Полная ЯЛК-96");
    if (code == QStringLiteral("PROD_YTP")) return QStringLiteral("Полная ЯТП · 0 / 120 / 240 Ом");
    if (code == QStringLiteral("PROD_YVP")) return QStringLiteral("Полная ЯВП-8 · V7 / ИСД");
    return QStringLiteral("Полная производственная проверка УБСИ");
}

QString routeStageName(int index)
{
    static const QStringList names = {
        QStringLiteral("Подготовка"),
        QStringLiteral("Питание / потребление"),
        QStringLiteral("ЯЛК-96"),
        QStringLiteral("ЯТП"),
        QStringLiteral("ЯВП-8"),
        QStringLiteral("Завершение")
    };
    return index >= 0 && index < names.size() ? names[index] : QStringLiteral("Этап");
}

QString yalkStepText(const QString& node)
{
    if (node.contains(QStringLiteral("stream"))) return QStringLiteral("Инициализация потока");
    if (node.contains(QStringLiteral("calibration"))) return QStringLiteral("Калибровка 97 / 99");
    if (node.contains(QStringLiteral("initial"))) return QStringLiteral("Исходное состояние 80 входов");
    if (node == QStringLiteral("yalk_channels")) return QStringLiteral("80 аналоговых каналов");
    if (node.contains(QStringLiteral("contact"))) return QStringLiteral("Дискретные пороги 0 / 0,9 / 2,5 В");
    if (node.contains(QStringLiteral("overload"))) return QStringLiteral("Перегрузка ±12 В");
    if (node.contains(QStringLiteral("reference"))) return QStringLiteral("Эталон 6,2 В");
    if (node.contains(QStringLiteral("cleanup"))) return QStringLiteral("Безопасное завершение ЯЛК");
    return QStringLiteral("Выполняется");
}

bool validOperatorName(const QString& value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[А-ЯЁ][а-яё-]+\\s[А-ЯЁ]\\.[А-ЯЁ]\\.$"));
    return pattern.match(value.trimmed()).hasMatch();
}

QString scopeDisplayFromPage(TestPage* page)
{
    auto* scope = page->findChild<QComboBox*>(QStringLiteral("testScope"));
    if (!scope) return QStringLiteral("—");
    const QString code = scope->currentData().toString();
    if (code == QStringLiteral("УБСИ ПО ТУ")) return QStringLiteral("Полная УБСИ");
    return code;
}

void updateFrozenSessionAction(TestPage* page)
{
    auto* queue = page->findChild<QTableWidget*>(QStringLiteral("productionSessionTable"));
    auto* operatorBox = page->findChild<QComboBox*>(QStringLiteral("productionOperatorSelector"));
    auto* enter = page->findChild<QPushButton*>(QStringLiteral("enterPreparation"));
    if (!queue || !operatorBox || !enter) return;
    enter->setEnabled(queue->rowCount() > 0
                      && !operatorBox->currentData().toString().trimmed().isEmpty());
}

void addFrozenQueueSerial(TestPage* page, const QString& serial)
{
    auto* queue = page->findChild<QTableWidget*>(QStringLiteral("productionSessionTable"));
    if (!queue || serial.trimmed().isEmpty()) return;
    const QString normalized = serial.trimmed();
    for (int row = 0; row < queue->rowCount(); ++row) {
        if (queue->item(row, 0) && queue->item(row, 0)->text() == normalized) {
            queue->selectRow(row);
            updateFrozenSessionAction(page);
            return;
        }
    }
    const int row = queue->rowCount();
    queue->insertRow(row);
    queue->setItem(row, 0, new QTableWidgetItem(normalized));
    queue->setItem(row, 1, new QTableWidgetItem(scopeDisplayFromPage(page)));
    auto* status = new QTableWidgetItem(QStringLiteral("ОЖИДАЕТ"));
    status->setForeground(QColor("#d7a95b"));
    queue->setItem(row, 2, status);
    queue->selectRow(row);
    if (auto* serialEdit = page->findChild<QLineEdit*>(QStringLiteral("objectSerial")))
        serialEdit->setText(normalized);
    updateFrozenSessionAction(page);
}

void refreshFrozenRegistry(TestPage* page)
{
    auto* table = page->findChild<QTableWidget*>(QStringLiteral("productionRegistryTable"));
    auto* search = page->findChild<QLineEdit*>(QStringLiteral("productionRegistrySearch"));
    if (!table) return;
    const QString filter = search ? search->text().trimmed() : QString();
    const QStringList serials = page->property("productionRegisteredSerials").toStringList();
    table->setRowCount(0);
    for (const auto& serial : serials) {
        if (!filter.isEmpty() && !serial.contains(filter, Qt::CaseInsensitive)) continue;
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(serial));
        auto* add = new QPushButton(QStringLiteral("Добавить →"), table);
        add->setObjectName(QStringLiteral("registryAddButton"));
        QObject::connect(add, &QPushButton::clicked, page,
                         [page, serial] { addFrozenQueueSerial(page, serial); });
        table->setCellWidget(row, 1, add);
    }
}

void freezeProductionSidebar(TestPage* page, const QString& title, const QString& detail)
{
    auto* context = page->findChild<QLabel*>(QStringLiteral("frozenProcedureContext"));
    auto* sideTitle = page->findChild<QLabel*>(QStringLiteral("frozenSideTitle"));
    if (!context || !sideTitle) return;
    sideTitle->setText(title);
    context->setText(detail);
    context->show();
    for (auto* label : page->findChildren<QLabel*>()) {
        if (label->property("routeStageIndex").isValid()) label->hide();
    }
}

YvpOverview* findYvpOverview(TestPage* page)
{
    return dynamic_cast<YvpOverview*>(
        page->findChild<QWidget*>(QStringLiteral("yvpEightChannelOverview")));
}

} // namespace

TestPage::TestPage(QWidget* parent)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>(this))
{
    rebuildScopes();

    // Freeze the production-session composition: registry / explicit queue /
    // session parameters.  Existing hidden controls remain the backend bridge.
    impl_->sessionDataPanel->hide();
    impl_->addProduct->hide();
    impl_->yalkSubPanel->hide();

    auto* sessionLayout = qobject_cast<QVBoxLayout*>(impl_->sessionPage->layout());
    QHBoxLayout* sessionBody = nullptr;
    if (sessionLayout) {
        for (int index = 0; index < sessionLayout->count(); ++index) {
            auto* candidate = qobject_cast<QHBoxLayout*>(sessionLayout->itemAt(index)->layout());
            if (candidate && candidate->indexOf(impl_->productsPanel) >= 0) {
                sessionBody = candidate;
                break;
            }
        }
    }

    if (sessionBody) {
        auto* registryPanel = panel();
        registryPanel->setObjectName(QStringLiteral("productionRegistryPanel"));
        auto* registryLayout = new QVBoxLayout(registryPanel);
        registryLayout->setContentsMargins(11, 11, 11, 11);
        registryLayout->addWidget(sectionLabel(QStringLiteral("Зарегистрированные УБСИ")));
        auto* search = new QLineEdit(registryPanel);
        search->setObjectName(QStringLiteral("productionRegistrySearch"));
        search->setPlaceholderText(QStringLiteral("Поиск по заводскому №"));
        search->setClearButtonEnabled(true);
        registryLayout->addWidget(search);
        auto* registry = new QTableWidget(0, 2, registryPanel);
        registry->setObjectName(QStringLiteral("productionRegistryTable"));
        registry->setHorizontalHeaderLabels({QStringLiteral("Заводской №"), QString()});
        registry->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        registry->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        registry->verticalHeader()->hide();
        registry->setSelectionBehavior(QAbstractItemView::SelectRows);
        registry->setSelectionMode(QAbstractItemView::SingleSelection);
        registry->setEditTriggers(QAbstractItemView::NoEditTriggers);
        registryLayout->addWidget(registry, 1);
        sessionBody->insertWidget(0, registryPanel, 3);
        connect(search, &QLineEdit::textChanged, this, [this] { refreshFrozenRegistry(this); });
    }

    if (auto* productsLayout = qobject_cast<QVBoxLayout*>(impl_->productsPanel->layout())) {
        if (auto* caption = qobject_cast<QLabel*>(productsLayout->itemAt(0)->widget()))
            caption->setText(QStringLiteral("Очередь текущей сессии"));
        auto* toolbar = new QHBoxLayout;
        auto* up = new QPushButton(QStringLiteral("↑"), impl_->productsPanel);
        auto* down = new QPushButton(QStringLiteral("↓"), impl_->productsPanel);
        auto* remove = new QPushButton(QStringLiteral("Убрать"), impl_->productsPanel);
        auto* clear = new QPushButton(QStringLiteral("Очистить"), impl_->productsPanel);
        toolbar->addWidget(up);
        toolbar->addWidget(down);
        toolbar->addWidget(remove);
        toolbar->addWidget(clear);
        toolbar->addStretch();
        productsLayout->addLayout(toolbar);
        const auto moveRow = [this](int delta) {
            auto* table = impl_->productTable;
            const int row = table->currentRow();
            const int target = row + delta;
            if (row < 0 || target < 0 || target >= table->rowCount()) return;
            QStringList values;
            for (int column = 0; column < 3; ++column)
                values << (table->item(row, column) ? table->item(row, column)->text() : QString());
            QList<QColor> colors;
            for (int column = 0; column < 3; ++column)
                colors << (table->item(row, column) ? table->item(row, column)->foreground().color() : QColor());
            table->removeRow(row);
            table->insertRow(target);
            for (int column = 0; column < 3; ++column) {
                auto* item = new QTableWidgetItem(values[column]);
                if (colors[column].isValid()) item->setForeground(colors[column]);
                table->setItem(target, column, item);
            }
            table->selectRow(target);
        };
        connect(up, &QPushButton::clicked, this, [moveRow] { moveRow(-1); });
        connect(down, &QPushButton::clicked, this, [moveRow] { moveRow(1); });
        connect(remove, &QPushButton::clicked, this, [this] {
            const int row = impl_->productTable->currentRow();
            if (row >= 0) impl_->productTable->removeRow(row);
            if (impl_->productTable->rowCount() > 0)
                impl_->productTable->selectRow(std::min(row, impl_->productTable->rowCount() - 1));
            updateFrozenSessionAction(this);
        });
        connect(clear, &QPushButton::clicked, this, [this] {
            impl_->productTable->setRowCount(0);
            impl_->serialEdit->clear();
            updateFrozenSessionAction(this);
        });
    }

    QWidget* scopePanel = impl_->scopeGroup->button(0)
        ? impl_->scopeGroup->button(0)->parentWidget() : nullptr;
    if (auto* scopeLayout = scopePanel ? qobject_cast<QVBoxLayout*>(scopePanel->layout()) : nullptr) {
        QGridLayout* scopeGrid = nullptr;
        for (int index = 0; index < scopeLayout->count(); ++index) {
            if (auto* grid = qobject_cast<QGridLayout*>(scopeLayout->itemAt(index)->layout())) {
                scopeGrid = grid;
                break;
            }
        }
        if (auto* caption = qobject_cast<QLabel*>(scopeLayout->itemAt(0)->widget()))
            caption->setText(QStringLiteral("Параметры сессии"));

        auto* operatorBox = new QWidget(scopePanel);
        auto* operatorLayout = new QVBoxLayout(operatorBox);
        operatorLayout->setContentsMargins(0, 2, 0, 4);
        operatorLayout->setSpacing(4);
        operatorLayout->addWidget(mutedLabel(QStringLiteral("ОПЕРАТОР")));
        auto* operatorRow = new QHBoxLayout;
        auto* operatorSelector = new QComboBox(operatorBox);
        operatorSelector->setObjectName(QStringLiteral("productionOperatorSelector"));
        operatorSelector->addItem(QStringLiteral("Выберите оператора"), QString());
        for (int index = 1; index < impl_->operatorHistory->count(); ++index) {
            const QString value = impl_->operatorHistory->itemText(index).trimmed();
            if (!value.isEmpty() && operatorSelector->findText(value) < 0)
                operatorSelector->addItem(value, value);
        }
        const QString remembered = impl_->operatorEdit->text().trimmed();
        if (!remembered.isEmpty() && operatorSelector->findText(remembered) < 0)
            operatorSelector->addItem(remembered, remembered);
        if (!remembered.isEmpty()) {
            const int index = operatorSelector->findData(remembered);
            if (index >= 0) operatorSelector->setCurrentIndex(index);
        }
        auto* addOperator = new QPushButton(QStringLiteral("+"), operatorBox);
        addOperator->setFixedWidth(42);
        operatorRow->addWidget(operatorSelector, 1);
        operatorRow->addWidget(addOperator);
        operatorLayout->addLayout(operatorRow);
        scopeLayout->insertWidget(1, operatorBox);

        auto* stagePanel = new QFrame(scopePanel);
        stagePanel->setObjectName(QStringLiteral("productionStagePanel"));
        auto* stageLayout = new QVBoxLayout(stagePanel);
        stageLayout->setContentsMargins(0, 2, 0, 4);
        stageLayout->setSpacing(4);
        stageLayout->addWidget(mutedLabel(QStringLiteral("ПРОИЗВОДСТВЕННЫЙ ЭТАП")));
        auto* stage = new QComboBox(stagePanel);
        stage->setObjectName(QStringLiteral("productionStage"));
        stage->addItem(QStringLiteral("Первичная проверка"), QStringLiteral("Primary"));
        stage->addItem(QStringLiteral("Климатические испытания — нормальные условия"), QStringLiteral("ClimateNormal"));
        stage->addItem(QStringLiteral("Климатические испытания — отрицательная температура"), QStringLiteral("ClimateMinus"));
        stage->addItem(QStringLiteral("Климатические испытания — повышенная температура"), QStringLiteral("ClimatePlus"));
        stage->addItem(QStringLiteral("После заливки — нормальные условия"), QStringLiteral("PottingClimateNormal"));
        stage->addItem(QStringLiteral("После заливки — повышенная температура"), QStringLiteral("PottingClimatePlus"));
        stage->addItem(QStringLiteral("После заливки — отрицательная температура"), QStringLiteral("PottingClimateMinus"));
        stageLayout->addWidget(stage);
        scopeLayout->insertWidget(2, stagePanel);
        scopeLayout->insertWidget(3, sectionLabel(QStringLiteral("Объём проверки")));

        if (scopeGrid) {
            for (int id = 0; id < 4; ++id) {
                if (auto* button = impl_->scopeGroup->button(id)) {
                    scopeGrid->removeWidget(button);
                    scopeGrid->addWidget(button, id, 0);
                }
            }
        }
        impl_->yalkSubPanel->hide();

        connect(operatorSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, operatorSelector](int) {
            impl_->operatorEdit->setText(operatorSelector->currentData().toString());
            updateFrozenSessionAction(this);
        });
        connect(addOperator, &QPushButton::clicked, this, [this, operatorSelector] {
            bool ok = false;
            const QString value = QInputDialog::getText(
                this, QStringLiteral("Новый оператор"),
                QStringLiteral("ФИО в формате «Толмачёв А.Е.»"),
                QLineEdit::Normal, QString(), &ok).trimmed();
            if (!ok || value.isEmpty()) return;
            if (!validOperatorName(value)) {
                QMessageBox::warning(this, QStringLiteral("Оператор"),
                    QStringLiteral("Используйте формат: Фамилия И.О., например «Толмачёв А.Е.»"));
                return;
            }
            int index = operatorSelector->findText(value);
            if (index < 0) {
                operatorSelector->addItem(value, value);
                impl_->operatorHistory->addItem(value);
                index = operatorSelector->count() - 1;
            }
            operatorSelector->setCurrentIndex(index);
        });
    }
    updateFrozenSessionAction(this);

    // Power keeps the common live 80-channel plane while the supply changes.
    if (auto* powerPage = impl_->workStack->widget(static_cast<int>(TopStage::Power))) {
        if (auto* powerLayout = qobject_cast<QVBoxLayout*>(powerPage->layout())) {
            auto* status = subtitleLabel(QStringLiteral("ЯЛК · ожидание свежего снимка 80 каналов"));
            status->setObjectName(QStringLiteral("powerYalkStatus"));
            auto* overview = new ChannelOverview(powerPage);
            overview->setObjectName(QStringLiteral("powerYalkOverview"));
            overview->configure(80, QStringLiteral("В"));
            overview->setMinimumHeight(175);
            const int beforeSteps = std::max(0, powerLayout->count() - 1);
            powerLayout->insertWidget(beforeSteps, status);
            powerLayout->insertWidget(beforeSteps + 1, overview, 1);
        }
    }

    // Replace the legacy sequential YVP trend with the frozen eight-channel plane.
    if (auto* yvpPage = impl_->workStack->widget(static_cast<int>(TopStage::Yvp))) {
        if (auto* yvpLayout = qobject_cast<QVBoxLayout*>(yvpPage->layout())) {
            const int trendIndex = yvpLayout->indexOf(impl_->yvpTrend);
            QWidget* note = trendIndex >= 0 && trendIndex + 1 < yvpLayout->count()
                ? yvpLayout->itemAt(trendIndex + 1)->widget() : nullptr;
            auto* overview = new YvpOverview(yvpPage);
            if (trendIndex >= 0) yvpLayout->insertWidget(trendIndex, overview, 1);
            else yvpLayout->addWidget(overview, 1);
            impl_->yvpTrend->hide();
            if (note) note->hide();
        }
    }

    // Frozen contextual left side: the global route is not operator navigation.
    impl_->sideTitle->setObjectName(QStringLiteral("frozenSideTitle"));
    if (auto* sideLayout = qobject_cast<QVBoxLayout*>(impl_->sideTitle->parentWidget()->layout())) {
        auto* context = subtitleLabel(QStringLiteral("Ожидание запуска"));
        context->setObjectName(QStringLiteral("frozenProcedureContext"));
        context->setMinimumHeight(48);
        sideLayout->insertWidget(1, context);
    }
    for (int index = 0; index < impl_->stageLabels.size(); ++index) {
        auto* label = impl_->stageLabels[index];
        label->setProperty("routeStageIndex", index);
        label->setCursor(Qt::ArrowCursor);
        label->setToolTip(QString());
    }
    connect(impl_->enterPreparation, &QPushButton::clicked, this, [this] {
        if (auto* overview = findYvpOverview(this)) overview->clear();
        freezeProductionSidebar(this, QStringLiteral("Подготовка"),
                                QStringLiteral("Проверка оборудования выбранного сценария"));
    });

    // TU has its own explicit entry/readiness shell. Selection alone never probes equipment.
    tuFlow_ = new TuFlowWidget(impl_->pages);
    impl_->pages->addWidget(tuFlow_);
    connect(tuFlow_, &TuFlowWidget::homeRequested, this, &TestPage::homeRequested);

    const auto beginTuStandCheck = [this](const QString& serial, const QString& operatorName) {
        impl_->serialEdit->setText(serial);
        const QStringList required = currentRequiredEquipment();
        for (const auto& code : required) {
            if (code == QStringLiteral("R4831") || code == QStringLiteral("SCHEME")) continue;
            const auto row = impl_->equipmentRows.find(code);
            if (row == impl_->equipmentRows.end() || row->operatorConfirmation) continue;
            row->ready = false;
            if (auto* state = impl_->equipmentTable->item(row->row, 3)) {
                state->setText(QStringLiteral("ПРОВЕРКА…"));
                state->setForeground(QColor("#d7a95b"));
            }
        }
        tuFlow_->beginStandCheck(serial, operatorName, required);
        emit equipmentCheckRequested();
    };
    connect(tuFlow_, &TuFlowWidget::readinessRequested, this, beginTuStandCheck);
    connect(tuFlow_, &TuFlowWidget::retryRequested, this, beginTuStandCheck);
    connect(tuFlow_, &TuFlowWidget::startRequested, this,
            [this](const QString& serial, const QString& operatorName) {
        impl_->activeSerial = serial.trimmed();
        impl_->activeOperator = operatorName.trimmed();
        impl_->serialEdit->setText(impl_->activeSerial);
        impl_->resetWorkspace();
        if (auto* serialLabel = findChild<QLabel*>(QStringLiteral("tuRuntimeSerial")))
            serialLabel->setText(QStringLiteral("УБСИ SN %1 · %2")
                                     .arg(impl_->activeSerial, impl_->activeOperator));
        if (auto* stateLabel = findChild<QLabel*>(QStringLiteral("tuRuntimeState")))
            stateLabel->setText(QStringLiteral("ПРОВЕРКА ПО ТУ · выполняется"));
        if (auto* header = findChild<QFrame*>(QStringLiteral("tuRuntimeHeader"))) header->show();
        impl_->pages->setCurrentWidget(impl_->workspacePage);
        impl_->setTopStage(TopStage::Power);
        startSelectedTest();
    });

    if (auto* workspaceLayout = qobject_cast<QVBoxLayout*>(impl_->workspacePage->layout())) {
        auto* header = panel();
        header->setObjectName(QStringLiteral("tuRuntimeHeader"));
        auto* row = new QHBoxLayout(header);
        row->setContentsMargins(12, 7, 12, 7);
        auto* serial = new QLabel(QStringLiteral("УБСИ"), header);
        serial->setObjectName(QStringLiteral("tuRuntimeSerial"));
        QFont serialFont = serial->font();
        serialFont.setBold(true);
        serialFont.setPointSize(13);
        serial->setFont(serialFont);
        auto* state = new QLabel(QStringLiteral("ПРОВЕРКА ПО ТУ"), header);
        state->setObjectName(QStringLiteral("tuRuntimeState"));
        state->setStyleSheet(QStringLiteral("color:#9ac7ff;font-weight:700;"));
        auto* stop = new QPushButton(QStringLiteral("Остановить"), header);
        stop->setObjectName(QStringLiteral("danger"));
        connect(stop, &QPushButton::clicked, this, &TestPage::stopRequested);
        row->addWidget(serial);
        row->addSpacing(18);
        row->addWidget(state);
        row->addStretch();
        row->addWidget(stop);
        header->hide();
        workspaceLayout->insertWidget(0, header);
    }
}

TestPage::~TestPage() = default;

bool TestPage::eventFilter(QObject* watched, QEvent* event)
{
    return QWidget::eventFilter(watched, event);
}

void TestPage::setEquipmentInvoker(EquipmentInvoke invoke)
{
    impl_->equipmentInvoke = std::move(invoke);
}

void TestPage::registerEquipmentRow(const QString& code,
                                    const QString& name,
                                    const QString& connection,
                                    const QString& initialDetail,
                                    bool operatorConfirmation)
{
    impl_->addEquipment(code, name, connection, initialDetail, operatorConfirmation);
}

void TestPage::setEquipmentStatus(const QString& code, bool ready, const QString& detail)
{
    if (!impl_->productionMode && tuFlow_) tuFlow_->setEquipmentStatus(code, ready, detail);
    const auto it = impl_->equipmentRows.find(code);
    if (it == impl_->equipmentRows.end()) return;
    if (!it->operatorConfirmation) it->ready = ready;
    auto* state = impl_->equipmentTable->item(it->row, 3);
    state->setText(ready ? QStringLiteral("ГОТОВО") : QStringLiteral("НЕ ГОТОВО"));
    state->setForeground(ready ? QColor("#70d79b") : QColor("#e1766d"));
    impl_->equipmentTable->item(it->row, 4)->setText(detail);
    updateStartAvailability();
}

void TestPage::setEquipmentConnection(const QString& code, const QString& connection)
{
    const auto it = impl_->equipmentRows.find(code);
    if (it == impl_->equipmentRows.end()) return;
    it->connection = connection;
    impl_->equipmentTable->item(it->row, 1)->setText(connection);
}

void TestPage::setEquipmentMissingPlugin(const QString& code, const QString& detail)
{
    setEquipmentStatus(code, false, QStringLiteral("НЕТ ПЛАГИНА · ") + detail);
}

void TestPage::setEquipmentChecking(const QString& code, const QString& detail)
{
    if (!impl_->productionMode && tuFlow_) tuFlow_->setEquipmentChecking(code);
    const auto it = impl_->equipmentRows.find(code);
    if (it == impl_->equipmentRows.end() || it->operatorConfirmation) return;
    it->ready = false;
    auto* state = impl_->equipmentTable->item(it->row, 3);
    state->setText(QStringLiteral("ПРОВЕРКА…"));
    state->setForeground(QColor("#d7a95b"));
    impl_->equipmentTable->item(it->row, 4)->setText(detail);
    updateStartAvailability();
}

void TestPage::setScenarioInfo(const QString& code,
                               bool available,
                               bool diagnostic,
                               const QStringList& requiredEquipment,
                               const QString& detail)
{
    impl_->scenarios.insert(code, {available, diagnostic, requiredEquipment, detail});
    if (tuFlow_ && code == QStringLiteral("ULK_COMBINED_CHECK"))
        tuFlow_->setScenarioAvailable(available, detail);
    updateSelectionSummary();
}

void TestPage::setEngineerMode(bool enabled)
{
    impl_->engineerMode = enabled;
    impl_->engineerBridgePanel->setVisible(enabled);
}

bool TestPage::isEngineerMode() const
{
    return impl_->engineerMode;
}

void TestPage::setProductionMode(bool enabled)
{
    impl_->productionMode = enabled;

    impl_->sessionTitle->setText(enabled
        ? QStringLiteral("Производственная сессия")
        : QStringLiteral("Проверка УБСИ по ТУ"));
    impl_->sessionSubtitle->setText(enabled
        ? QStringLiteral("Сформируйте очередь зарегистрированных УБСИ. Оператор, этап и объём относятся ко всей сессии.")
        : QStringLiteral("Проверка по ТУ"));
    impl_->workflowBadge->setText(enabled
        ? QStringLiteral("ПРОИЗВОДСТВО")
        : QStringLiteral("ПРОВЕРКА ПО ТУ"));
    impl_->workflowBadge->setStyleSheet(enabled
        ? QStringLiteral("background:#14251c;color:#70d79b;border:1px solid #315c43;border-radius:5px;padding:8px 12px;font-weight:700;")
        : QStringLiteral("background:#132033;color:#9ac7ff;border:1px solid #27466c;border-radius:5px;padding:8px 12px;font-weight:700;"));

    impl_->sessionDataPanel->setVisible(false);
    impl_->productsPanel->setVisible(enabled);
    if (auto* registry = findChild<QFrame*>(QStringLiteral("productionRegistryPanel"))) registry->setVisible(enabled);
    if (auto* scopePanel = impl_->scopeGroup->button(0)->parentWidget()) scopePanel->setVisible(enabled);
    impl_->scopeButtons.value(QStringLiteral("ЯВП-8"))->setVisible(enabled);
    impl_->yalkSubPanel->setVisible(false);
    impl_->includeYvpCheck->setChecked(enabled);

    impl_->backSession->setVisible(enabled);
    impl_->workspaceTitle->setVisible(enabled);
    impl_->workspaceSubtitle->setVisible(enabled);
    impl_->operatorBadge->setVisible(enabled);
    impl_->stopButton->setVisible(enabled);
    if (auto* header = findChild<QFrame*>(QStringLiteral("tuRuntimeHeader"))) header->hide();
    if (impl_->elapsed && impl_->elapsed->parentWidget())
        impl_->elapsed->parentWidget()->setVisible(enabled);

    const auto setMetricVisible = [](QLabel* value) {
        if (value && value->parentWidget()) value->parentWidget()->setVisible(true);
    };
    for (auto* value : {
             impl_->powerSet, impl_->powerActual, impl_->powerCurrent, impl_->powerHold,
             impl_->yalkStream, impl_->yalkSequence, impl_->yalkCalZero, impl_->yalkCalFull,
             impl_->yalkChannel, impl_->yalkPoint, impl_->yalkV7,
             impl_->yalkDiscretePoint, impl_->yalkExpected, impl_->yalkDiscreteChannel,
             impl_->overloadChannel, impl_->overloadPolarity, impl_->overloadDelta,
             impl_->referenceV7, impl_->referenceYalk, impl_->referenceDelta,
             impl_->ytpStream, impl_->ytpEndpoint, impl_->ytpCalZero, impl_->ytpCalFull,
             impl_->ytpChannel, impl_->ytpReference, impl_->ytpMeasured,
             impl_->yvpChannel, impl_->yvpFrequency, impl_->yvpGain, impl_->yvpResult,
             impl_->finishPower, impl_->finishYalk, impl_->finishYtp, impl_->finishYvp}) {
        setMetricVisible(value);
    }

    impl_->yalkPhaseStrip->hide();
    impl_->yalkPhaseTitle->setVisible(true);
    impl_->ytpPhaseTitle->setVisible(true);
    impl_->yvpStatus->setVisible(true);
    impl_->ytpOperatorBanner->setVisible(false);
    impl_->finishDetail->setVisible(enabled);
    impl_->nextProduct->setVisible(enabled);
    impl_->reportButton->setText(enabled ? QStringLiteral("Открыть отчёт")
                                         : QStringLiteral("Открыть протокол ТУ"));

    rebuildScopes();
    updateSelectionSummary();
    impl_->configureRouteVisibility();
    if (enabled) {
        impl_->pages->setCurrentWidget(impl_->sessionPage);
        refreshFrozenRegistry(this);
        updateFrozenSessionAction(this);
    } else {
        QStringList operators;
        if (auto* selector = findChild<QComboBox*>(QStringLiteral("productionOperatorSelector"))) {
            for (int index = 1; index < selector->count(); ++index)
                operators << selector->itemData(index).toString();
        }
        for (int index = 1; index < impl_->operatorHistory->count(); ++index)
            operators << impl_->operatorHistory->itemText(index);
        operators.removeAll(QString());
        operators.removeDuplicates();
        tuFlow_->setOperators(operators);
        tuFlow_->resetToSelection();
        impl_->pages->setCurrentWidget(tuFlow_);
    }
}

void TestPage::setAvailableProductionProducts(const QStringList& serials)
{
    QStringList unique = serials;
    unique.removeDuplicates();
    unique.sort(Qt::CaseInsensitive);
    setProperty("productionRegisteredSerials", unique);
    if (tuFlow_) tuFlow_->setRegisteredSerials(unique);
    refreshFrozenRegistry(this);
    if (!impl_->productionMode) return;
    impl_->scenarioInfo->setText(unique.isEmpty()
        ? QStringLiteral("В registrar.db нет зарегистрированных УБСИ. Регистрация выполняется в «Администрирование».")
        : QStringLiteral("Доступно УБСИ из registrar.db: %1").arg(unique.size()));
    updateFrozenSessionAction(this);
}

QStringList TestPage::currentRequiredEquipment() const
{
    return impl_->scenarios.value(currentScenarioCode()).required;
}

void TestPage::rebuildScopes()
{
    const QString previous = impl_->scopeCombo->currentData().toString();
    impl_->scopeCombo->blockSignals(true);
    impl_->scopeCombo->clear();
    impl_->scopeCombo->addItem(impl_->productionMode ? QStringLiteral("УБСИ · полная")
                                                     : QStringLiteral("УБСИ по ТУ"),
                              QStringLiteral("УБСИ ПО ТУ"));
    impl_->scopeCombo->addItem(QStringLiteral("ЯЛК-96"), QStringLiteral("ЯЛК-96"));
    impl_->scopeCombo->addItem(QStringLiteral("ЯТП"), QStringLiteral("ЯТП"));
    if (impl_->productionMode)
        impl_->scopeCombo->addItem(QStringLiteral("ЯВП-8"), QStringLiteral("ЯВП-8"));
    const int index = impl_->scopeCombo->findData(previous);
    impl_->scopeCombo->setCurrentIndex(index >= 0 ? index : 0);
    impl_->scopeCombo->blockSignals(false);
    if (!impl_->productionMode) rebuildTests();
    updateSelectionSummary();
}

void TestPage::rebuildTests()
{
    if (impl_->productionMode) return;
    const QString scope = impl_->scopeCombo->currentData().toString();
    impl_->testCombo->blockSignals(true);
    impl_->testCombo->clear();
    if (scope == QStringLiteral("УБСИ ПО ТУ")) {
        impl_->testCombo->addItem(QStringLiteral("Полная проверка УБСИ · ТУ"),
                                  QStringLiteral("ULK_COMBINED_CHECK"));
    } else if (scope == QStringLiteral("ЯЛК-96")) {
        impl_->testCombo->addItem(QStringLiteral("Полная проверка ЯЛК-96"),
                                  QStringLiteral("YALK_FULL_5_6"));
        impl_->testCombo->addItem(QStringLiteral("Контактные пороги"),
                                  QStringLiteral("YALK_CONTACT_THRESHOLDS"));
    } else if (scope == QStringLiteral("ЯТП")) {
        impl_->testCombo->addItem(QStringLiteral("Полная ЯТП · 0 / 120 / 240 Ом"),
                                  QStringLiteral("YTP_FULL_5_6"));
        impl_->testCombo->addItem(QStringLiteral("Быстрый контроль 120 Ом"),
                                  QStringLiteral("YTP_120_CHECK"));
    }
    impl_->testCombo->setCurrentIndex(0);
    impl_->testCombo->blockSignals(false);
    updateSelectionSummary();
}

void TestPage::updateSelectionSummary()
{
    const QString scope = impl_->scopeCombo->currentData().toString();
    for (auto it = impl_->scopeButtons.begin(); it != impl_->scopeButtons.end(); ++it)
        it.value()->setChecked(it.key() == scope);
    impl_->yalkSubPanel->setVisible(false);

    if (impl_->productionMode) {
        const QString code = productionScenarioForScope(scope);
        if (impl_->testCombo->count() != 1 || impl_->testCombo->currentData().toString() != code) {
            impl_->testCombo->blockSignals(true);
            impl_->testCombo->clear();
            impl_->testCombo->addItem(productionScenarioTitle(code), code);
            impl_->testCombo->setCurrentIndex(0);
            impl_->testCombo->blockSignals(false);
        }
        for (int row = 0; row < impl_->productTable->rowCount(); ++row)
            if (impl_->productTable->item(row, 1))
                impl_->productTable->item(row, 1)->setText(impl_->scopeDisplay());
    }

    const QString code = currentScenarioCode();
    const auto info = impl_->scenarios.value(code);
    if (!code.isEmpty()) {
        impl_->scenarioInfo->setText(info.detail.isEmpty()
            ? QStringLiteral("Сценарий: %1").arg(code) : info.detail);
    }
    impl_->includeYvpCheck->setChecked(scope == QStringLiteral("УБСИ ПО ТУ")
                                       || scope == QStringLiteral("ЯВП-8"));

    const bool scenarioChanged = code != lastScenarioCode_;
    if (scenarioChanged) lastScenarioCode_ = code;
    const QSet<QString> required(info.required.cbegin(), info.required.cend());
    for (auto it = impl_->equipmentRows.begin(); it != impl_->equipmentRows.end(); ++it) {
        const bool visible = required.contains(it.key());
        impl_->equipmentTable->setRowHidden(it->row, !visible);
        if (scenarioChanged && visible && !it->operatorConfirmation) {
            it->ready = false;
            if (auto* state = impl_->equipmentTable->item(it->row, 3)) {
                state->setText(QStringLiteral("НЕ ПРОВЕРЕНО"));
                state->setForeground(QColor("#d7a95b"));
            }
        }
    }
    impl_->configureRouteVisibility();
    if (scope == QStringLiteral("ЯВП-8")) {
        impl_->stageLabels[static_cast<int>(TopStage::Power)]->setProperty("includedInRoute", true);
        impl_->updateStageLabels();
    }
    updateStartAvailability();
    updateFrozenSessionAction(this);
}

void TestPage::updateStartAvailability()
{
    const QString code = currentScenarioCode();
    const auto info = impl_->scenarios.value(code);
    const bool available = info.available;
    bool ready = available;
    if (ready) {
        for (const auto& equipmentCode : info.required) {
            if (equipmentCode == QStringLiteral("SCHEME") || equipmentCode == QStringLiteral("R4831")) continue;
            const auto row = impl_->equipmentRows.constFind(equipmentCode);
            if (row == impl_->equipmentRows.cend() || !row->ready) {
                ready = false;
                break;
            }
        }
    }

    impl_->checkButton->setEnabled(!impl_->runInProgress && available);
    impl_->stopButton->setEnabled(impl_->runInProgress);
    impl_->startButton->setEnabled(!impl_->runInProgress && available && ready);
    if (impl_->runInProgress) {
        impl_->readiness->setText(QStringLiteral("Проверка выполняется"));
        impl_->readiness->setStyleSheet(QStringLiteral("color:#69aee6;font-weight:700;"));
    } else if (!available) {
        impl_->readiness->setText(info.detail.isEmpty() ? QStringLiteral("Исполняемый сценарий не готов") : info.detail);
        impl_->readiness->setStyleSheet(QStringLiteral("color:#e1766d;font-weight:700;"));
    } else if (!ready) {
        impl_->readiness->setText(QStringLiteral("Проверьте оборудование, требуемое выбранным сценарием"));
        impl_->readiness->setStyleSheet(QStringLiteral("color:#d7a95b;font-weight:700;"));
    } else {
        impl_->readiness->setText(QStringLiteral("Оборудование выбранного сценария готово. Можно запускать проверку."));
        impl_->readiness->setStyleSheet(QStringLiteral("color:#70d79b;font-weight:700;"));
    }
}

void TestPage::startSelectedTest()
{
    if (impl_->modeCombo->currentIndex() == kDemoMode) {
        QMessageBox::information(this, QStringLiteral("Демонстрация"),
            QStringLiteral("Для дизайнерского просмотра используйте протокольный имитатор стенда: UI подключён к реальным RunEvent."));
        return;
    }
    if (impl_->productionMode) {
        if (impl_->productTable->currentRow() < 0) {
            QMessageBox::warning(this, QStringLiteral("УБСИ"), QStringLiteral("Выберите УБСИ из очереди сессии."));
            return;
        }
        impl_->activeRow = impl_->productTable->currentRow();
        impl_->activeSerial = impl_->productTable->item(impl_->activeRow, 0)->text();
        impl_->serialEdit->setText(impl_->activeSerial);
        impl_->productTable->item(impl_->activeRow, 2)->setText(QStringLiteral("В РАБОТЕ"));
        impl_->productTable->item(impl_->activeRow, 2)->setForeground(QColor("#69aee6"));
    } else {
        impl_->activeSerial = impl_->serialEdit->text().trimmed();
    }
    if (impl_->activeSerial.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("УБСИ"), QStringLiteral("Выберите заводской номер УБСИ."));
        return;
    }
    impl_->appendSessionRecord(QStringLiteral("START"));
    emit runRequested(currentScenarioCode(), impl_->activeSerial, false);
}

void TestPage::advanceDemo() {}

void TestPage::setRunInProgress(bool running, const QString& stage)
{
    impl_->runInProgress = running;
    if (running) {
        impl_->runClock.restart();
        impl_->runClockTimer->start();
        impl_->stopButton->setEnabled(true);
        impl_->progress->setRange(0, 100);
        impl_->footerStage->setText(stage.isEmpty() ? QStringLiteral("Выполняется…") : stage);
    } else {
        impl_->runClockTimer->stop();
        impl_->stopButton->setEnabled(false);
    }
    updateStartAvailability();
}

void TestPage::setRunEvent(const orbita::stand::RunEvent& event)
{
    if (!impl_->runInProgress) return;
    const QString node = QString::fromStdString(event.nodeId);
    auto setRouteDetail = [this](int index, const QString& detail) {
        if (impl_->productionMode) {
            freezeProductionSidebar(this, routeStageName(index), detail);
            return;
        }
        if (index < 0 || index >= impl_->stageLabels.size()) return;
        auto* label = impl_->stageLabels[index];
        if (label->isHidden()) return;
        const QString prefix = index == static_cast<int>(impl_->topStage)
            ? QStringLiteral("▶") : index < static_cast<int>(impl_->topStage) ? QStringLiteral("✓") : QStringLiteral("○");
        label->setText(QStringLiteral("%1  %2. %3\n%4")
            .arg(prefix).arg(impl_->visibleRouteNumber(index)).arg(routeStageName(index), detail));
    };

    if (event.stage == "START") {
        if (impl_->productionMode) {
            impl_->mapNode(node);
        } else {
            if (node == QStringLiteral("readiness") || node == QStringLiteral("supply_range")
                || node == QStringLiteral("supply_status")) {
                impl_->setTopStage(TopStage::Power);
            } else if (node.startsWith(QStringLiteral("yalk_")) && !node.startsWith(QStringLiteral("yvp_"))) {
                impl_->setTopStage(TopStage::Yalk);
                if (node.contains(QStringLiteral("contact"))) impl_->setYalkPhase(YalkPhase::Discrete);
                else if (node.contains(QStringLiteral("overload"))) impl_->setYalkPhase(YalkPhase::Overload);
                else if (node.contains(QStringLiteral("reference"))) impl_->setYalkPhase(YalkPhase::Reference);
                else impl_->setYalkPhase(YalkPhase::Analog);
            } else if (node.startsWith(QStringLiteral("ytp_"))) {
                impl_->setTopStage(TopStage::Ytp);
                impl_->setYtpPhase(YtpPhase::Channels);
            } else if (node.startsWith(QStringLiteral("yvp_"))) {
                impl_->setTopStage(TopStage::Yvp);
            } else {
                impl_->mapNode(node);
            }
            if (auto* state = findChild<QLabel*>(QStringLiteral("tuRuntimeState")))
                state->setText(QStringLiteral("ПРОВЕРКА ПО ТУ · %1").arg(QString::fromStdString(event.message)));
        }
        if (node.startsWith(QStringLiteral("yalk_")) && !node.startsWith(QStringLiteral("yvp_")))
            setRouteDetail(static_cast<int>(TopStage::Yalk), yalkStepText(node));
        else if (node.startsWith(QStringLiteral("ytp_")))
            setRouteDetail(static_cast<int>(TopStage::Ytp), node.contains(QStringLiteral("channels"))
                ? QStringLiteral("30 каналов · 0 / 120 / 240 Ом")
                : node.contains(QStringLiteral("calibration")) ? QStringLiteral("Калибровка")
                                                               : QStringLiteral("Подготовка потока"));
        else if (node.startsWith(QStringLiteral("yvp_")))
            setRouteDetail(static_cast<int>(TopStage::Yvp), QStringLiteral("V7 / ИСД · 8 каналов"));
        else if (impl_->productionMode)
            setRouteDetail(static_cast<int>(impl_->topStage), QString::fromStdString(event.message));
        impl_->updateProgressByStage();
        return;
    }

    if (event.stage == "FINISH") {
        impl_->updateProgressByStage();
        return;
    }

    if (event.stage == "SUPPLY") {
        impl_->setTopStage(TopStage::Power);
        const double set = eventValue(event, "setpoint_v").toDouble();
        const double actual = eventValue(event, "volts").toDouble();
        const double amperes = eventValue(event, "amperes").toDouble();
        const int elapsed = eventValue(event, "elapsed_s").toInt();
        const int duration = eventValue(event, "duration_s").toInt();
        impl_->powerSet->setText(QStringLiteral("%1 В").arg(set, 0, 'f', 1));
        impl_->powerActual->setText(QStringLiteral("%1 В").arg(actual, 0, 'f', 3));
        impl_->powerCurrent->setText(QStringLiteral("%1 А").arg(amperes, 0, 'f', 3));
        impl_->powerHold->setText(duration > 0 ? QStringLiteral("%1 / %2 с").arg(elapsed).arg(duration)
                                                : QStringLiteral("рабочая точка"));
        impl_->powerTrend->append(set, actual);
        impl_->powerSteps->setActiveValue(set);
        impl_->consumption->append(amperes);
        setRouteDetail(static_cast<int>(TopStage::Power),
            QStringLiteral("%1 В · %2 А").arg(actual, 0, 'f', 2).arg(amperes, 0, 'f', 3));
        impl_->updateProgressByStage();
        return;
    }

    if (event.stage == "POWER_YALK") {
        impl_->setTopStage(TopStage::Power);
        auto* status = findChild<QLabel*>(QStringLiteral("powerYalkStatus"));
        auto* overview = dynamic_cast<ChannelOverview*>(findChild<QWidget*>(QStringLiteral("powerYalkOverview")));
        const bool fresh = eventValue(event, "fresh") == QStringLiteral("true");
        const QString setpoint = eventValue(event, "setpoint_v");
        if (overview) {
            overview->setProperty("fresh", fresh);
            overview->setEnabled(fresh);
        }
        if (fresh) {
            const QVector<double> values = csvNumbers(eventValue(event, "values_v"));
            if (overview && values.size() == 80) overview->setBackground(values, values, values);
            if (status) {
                status->setText(QStringLiteral("ЯЛК · свежий снимок 80 каналов · питание %1 В").arg(setpoint));
                status->setStyleSheet(QStringLiteral("color:#70d79b;"));
            }
        } else if (status) {
            const QString detail = eventValue(event, "detail");
            status->setText(QStringLiteral("ЯЛК · НЕТ СВЕЖИХ ДАННЫХ · питание %1 В%2")
                .arg(setpoint, detail.isEmpty() ? QString() : QStringLiteral(" · ") + detail));
            status->setStyleSheet(QStringLiteral("color:#d7a95b;font-weight:700;"));
        }
        setRouteDetail(static_cast<int>(TopStage::Power), QStringLiteral("Питание %1 В · 80 каналов ЯЛК live").arg(setpoint));
        return;
    }

    if (event.stage == "OVERLOAD") {
        impl_->setTopStage(TopStage::Yalk);
        impl_->setYalkPhase(YalkPhase::Overload);
        const QString polarity = eventValue(event, "polarity");
        const QString channel = eventValue(event, "stressed_channel");
        const QString count = eventValue(event, "target_count");
        const int impactIndex = eventValue(event, "impact_index").toInt();
        const int impactCount = eventValue(event, "impact_count").toInt();
        const int settleMs = eventValue(event, "settle_ms").toInt();
        impl_->overloadChannel->setText(channel.isEmpty() ? QStringLiteral("—") : channel);
        impl_->overloadPolarity->setText(polarity.isEmpty() ? QStringLiteral("±12 В") : polarity);
        impl_->overloadProgress->setText(impactCount > 0
            ? QStringLiteral("%1 / %2 · %3 с").arg(impactIndex).arg(impactCount).arg(settleMs / 1000.0, 0, 'f', 1)
            : QStringLiteral("ожидание данных"));
        impl_->overloadDelta->setText(QStringLiteral("—"));
        impl_->overloadOverview->beginImpact(channel.toInt(), polarity, impactIndex, impactCount, settleMs);
        setRouteDetail(static_cast<int>(TopStage::Yalk),
            QStringLiteral("Перегрузка %1 · канал %2 / %3")
                .arg(polarity.isEmpty() ? QStringLiteral("±12 В") : polarity,
                     channel.isEmpty() ? QStringLiteral("—") : channel,
                     count.isEmpty() ? QStringLiteral("88") : count));
        return;
    }

    if (event.stage == "OPERATOR") {
        impl_->setTopStage(TopStage::Ytp);
        impl_->setYtpPhase(YtpPhase::Channels);
        const double resistance = eventValue(event, "target_resistance_ohm").toDouble();
        impl_->ytpOperatorBanner->setText(
            QStringLiteral("Р4831: установите %1 Ом · подтверждение откроется отдельным диалогом")
                .arg(resistance, 0, 'f', 3));
        impl_->ytpOperatorBanner->setVisible(impl_->productionMode);
        impl_->ytpResistanceSteps->setActiveValue(resistance);
        setRouteDetail(static_cast<int>(TopStage::Ytp),
            QStringLiteral("Р4831 · %1 Ом").arg(resistance, 0, 'f', 0));
        impl_->updateProgressByStage();
        return;
    }

    if (event.stage == "YVP_V7_POINT") {
        impl_->setTopStage(TopStage::Yvp);
        const QString channel = eventValue(event, "yvp_channel");
        const QString frequencyText = eventValue(event, "set_frequency_hz");
        const QString measuredFrequencyText = eventValue(event, "measured_frequency_hz");
        const QString gainText = eventValue(event, "gain_mv_per_pc");
        const QString calculatedGainText = eventValue(event, "calculated_gain_mv_per_pc");
        const QString v7RmsText = eventValue(event, "v7_output_vrms");
        const QString inputVppText = eventValue(event, "rigol_input_vpp");
        const QString acceptance = eventValue(event, "acceptance");

        bool frequencyOk = false, measuredFrequencyOk = false, gainOk = false;
        bool calculatedGainOk = false, v7Ok = false, inputVppOk = false;
        const double frequency = frequencyText.toDouble(&frequencyOk);
        const double measuredFrequency = measuredFrequencyText.toDouble(&measuredFrequencyOk);
        const double gain = gainText.toDouble(&gainOk);
        const double calculatedGain = calculatedGainText.toDouble(&calculatedGainOk);
        const double v7Rms = v7RmsText.toDouble(&v7Ok);
        const double inputVpp = inputVppText.toDouble(&inputVppOk);

        impl_->yvpChannel->setText(channel.isEmpty() ? QStringLiteral("—") : QStringLiteral("%1 / 8").arg(channel));
        if (frequencyOk) {
            impl_->yvpFrequency->setText(measuredFrequencyOk
                ? QStringLiteral("%1 Гц · В7 %2 Гц").arg(frequency, 0, 'g', 8).arg(measuredFrequency, 0, 'g', 8)
                : QStringLiteral("%1 Гц").arg(frequency, 0, 'g', 8));
        } else impl_->yvpFrequency->setText(QStringLiteral("—"));
        impl_->yvpGain->setText(gainOk ? QStringLiteral("%1 мВ/пКл").arg(gain, 0, 'g', 8) : QStringLiteral("—"));
        impl_->yvpResult->setText(calculatedGainOk
            ? QStringLiteral("%1 мВ/пКл").arg(calculatedGain, 0, 'g', 8) : QStringLiteral("—"));
        impl_->yvpStatus->setText(QStringLiteral("Rigol → ЯВП-8 → ИСД → В7 · %1%2")
            .arg(v7Ok ? QStringLiteral("В7 %1 Vrms").arg(v7Rms, 0, 'g', 8) : QStringLiteral("измерение В7"),
                 acceptance == QStringLiteral("not_applied")
                    ? QStringLiteral(" · критерий приёмки не применён") : QString()));
        if (auto* overview = findYvpOverview(this);
            overview && gainOk && calculatedGainOk && frequencyOk) {
            overview->setPoint(channel.toInt(), gain, calculatedGain, v7Ok ? v7Rms : 0.0,
                               frequency, inputVppOk ? inputVpp : 0.0, acceptance);
        }
        setRouteDetail(static_cast<int>(TopStage::Yvp),
            QStringLiteral("Канал %1 / 8 · Kу %2 · %3 Гц")
                .arg(channel.isEmpty() ? QStringLiteral("—") : channel,
                     gainOk ? QString::number(gain, 'g', 8) : QStringLiteral("—"),
                     frequencyOk ? QString::number(frequency, 'g', 8) : QStringLiteral("—")));
        impl_->updateProgressByStage();
        return;
    }

    if (event.stage == "BACKGROUND") {
        const auto values = [&event](const char* key) {
            const auto found = event.data.find(key);
            return found == event.data.end() ? QVector<double>()
                                             : csvNumbers(QString::fromStdString(found->second));
        };
        const QString section = eventValue(event, "section");
        if (section == QStringLiteral("YALK")) {
            auto mean = values("background_mean");
            auto minimum = values("background_min");
            auto maximum = values("background_max");
            impl_->yalkOverview->setBackground(mean, minimum, maximum);
            impl_->yalkContacts->setBackground(std::move(mean), std::move(minimum), std::move(maximum));
        } else if (section == QStringLiteral("YTP")) {
            impl_->ytpOverview->setBackground(values("background_mean"), values("background_min"), values("background_max"));
        }
        return;
    }
    if (event.stage != "MEASUREMENT") return;

    if (!eventValue(event, "observed_channel").isEmpty() && !eventValue(event, "delta_code").isEmpty()) {
        impl_->setTopStage(TopStage::Yalk);
        impl_->setYalkPhase(YalkPhase::Overload);
        const int observed = eventValue(event, "observed_channel").toInt();
        const double baseline = eventValue(event, "baseline_code").toDouble();
        const double current = eventValue(event, "current_code").toDouble();
        const double delta = eventValue(event, "delta_code").toDouble();
        const double lower = eventValue(event, "lower_delta_code").toDouble();
        const double upper = eventValue(event, "upper_delta_code").toDouble();
        impl_->overloadOverview->setMeasurement(observed, baseline, current, delta, lower, upper,
            event.verdict == orbita::stand::RunVerdict::Ok);
        impl_->overloadDelta->setText(QStringLiteral("%1 кода")
            .arg(impl_->overloadOverview->maximumAbsoluteDelta(), 0, 'f', 1));
        setRouteDetail(static_cast<int>(TopStage::Yalk),
            QStringLiteral("Перегрузка · max |Δcode| %1")
                .arg(impl_->overloadOverview->maximumAbsoluteDelta(), 0, 'f', 1));
        return;
    }

    if (!eventValue(event, "ytp_channel").isEmpty()) {
        impl_->setTopStage(TopStage::Ytp);
        impl_->setYtpPhase(YtpPhase::Channels);
        impl_->ytpOperatorBanner->setVisible(false);
        const QString channel = eventValue(event, "ytp_channel");
        const double ref = eventValue(event, "actual_reference_ohm").toDouble();
        const double measured = eventValue(event, "measured_resistance_ohm").toDouble();
        impl_->ytpChannel->setText(channel + QStringLiteral(" / 30"));
        impl_->ytpReference->setText(QStringLiteral("%1 Ом").arg(ref, 0, 'f', 3));
        impl_->ytpMeasured->setText(QStringLiteral("%1 Ом").arg(measured, 0, 'f', 3));
        impl_->ytpResistanceSteps->setActiveValue(ref);
        ChannelSample sample{channel, QStringLiteral("%1 Ом").arg(ref, 0, 'f', 0), ref, measured,
                             false, event.verdict == orbita::stand::RunVerdict::Ok, false,
                             csvNumbers(eventValue(event, "value_samples"))};
        impl_->ytpOverview->add(std::move(sample));
        setRouteDetail(static_cast<int>(TopStage::Ytp),
            QStringLiteral("Канал %1 / 30 · Р4831 %2 Ом").arg(channel).arg(ref, 0, 'f', 0));
        impl_->updateProgressByStage();
        return;
    }

    if (!eventValue(event, "ulk_address").isEmpty()) {
        impl_->setTopStage(TopStage::Yalk);
        if (node.contains(QStringLiteral("contact"))) impl_->setYalkPhase(YalkPhase::Discrete);
        else impl_->setYalkPhase(YalkPhase::Analog);

        const QString address = eventValue(event, "ulk_address");
        const double command = eventValue(event, "command_v").toDouble();
        const double v7 = eventValue(event, "v7_v").toDouble();
        const double yalk = eventValue(event, "yalk_v").toDouble();
        const int signal = eventValue(event, "signal").toInt();
        const QVector<double> valueSamples = csvNumbers(eventValue(event, "value_samples"));
        const QString lowerText = eventValue(event, "lower_limit_v");
        const QString upperText = eventValue(event, "upper_limit_v");
        const auto sampleRange = std::minmax_element(valueSamples.cbegin(), valueSamples.cend());
        const bool warning = event.verdict == orbita::stand::RunVerdict::Ok
            && !valueSamples.isEmpty() && !lowerText.isEmpty() && !upperText.isEmpty()
            && (*sampleRange.first < lowerText.toDouble() || *sampleRange.second > upperText.toDouble());

        impl_->yalkChannel->setText(address);
        impl_->yalkPoint->setText(QStringLiteral("%1 В").arg(command, 0, 'f', 1));
        impl_->yalkV7->setText(QStringLiteral("%1 В").arg(v7, 0, 'f', 3));
        ChannelSample sample{address, QStringLiteral("%1 В").arg(command, 0, 'f', 1), v7, yalk,
                             signal != 0, event.verdict == orbita::stand::RunVerdict::Ok,
                             warning, valueSamples};
        impl_->yalkOverview->add(std::move(sample));

        if (impl_->yalkPhase == YalkPhase::Discrete) {
            const int expected = command >= 2.0 ? 1 : 0;
            impl_->yalkDiscretePoint->setText(QStringLiteral("%1 В").arg(command, 0, 'f', 1));
            impl_->yalkExpected->setText(QString::number(expected));
            impl_->yalkDiscreteChannel->setText(address);
            impl_->yalkContacts->setCurrent(address.toInt(), command, signal, expected);
            setRouteDetail(static_cast<int>(TopStage::Yalk),
                QStringLiteral("Дискретные пороги · канал %1 · %2 В").arg(address).arg(command, 0, 'f', 1));
        } else {
            setRouteDetail(static_cast<int>(TopStage::Yalk),
                QStringLiteral("Аналоговые · канал %1 · %2 В").arg(address).arg(command, 0, 'f', 1));
        }
        impl_->updateProgressByStage();
    }
}

void TestPage::setRunResult(const orbita::stand::ScenarioRunResult& result,
                            const QString& tuReportPath,
                            const QString& productionReportPath)
{
    impl_->runInProgress = false;
    impl_->runClockTimer->stop();
    impl_->tuReportPath = tuReportPath;
    impl_->productionReportPath = productionReportPath;
    impl_->setTopStage(TopStage::Finish);
    impl_->progress->setValue(100);

    const QString verdict = verdictText(result.verdict);
    impl_->finishVerdict->setText(impl_->productionMode ? verdict : QStringLiteral("ТУ · %1").arg(verdict));
    impl_->finishVerdict->setStyleSheet(
        QStringLiteral("font-size:31px;font-weight:800;color:%1;").arg(verdictColor(result.verdict).name()));
    impl_->finishDetail->setText(QStringLiteral("SN %1 · run_id %2")
        .arg(impl_->activeSerial, QString::fromStdString(result.runId)));
    impl_->finishPower->setText(QStringLiteral("завершено"));
    impl_->finishYalk->setText(QStringLiteral("завершено"));
    impl_->finishYtp->setText(QStringLiteral("завершено"));
    impl_->finishYvp->setText(impl_->includeYvpCheck->isChecked() ? QStringLiteral("по сценарию") : QStringLiteral("—"));
    impl_->reportButton->setEnabled(!tuReportPath.isEmpty() || !productionReportPath.isEmpty());

    if (impl_->productionMode && impl_->activeRow >= 0 && impl_->activeRow < impl_->productTable->rowCount()) {
        auto* item = impl_->productTable->item(impl_->activeRow, 2);
        item->setText(verdict);
        item->setForeground(verdictColor(result.verdict));
    }
    if (impl_->productionMode)
        freezeProductionSidebar(this, QStringLiteral("Завершение"),
                                QStringLiteral("Результат изделия · %1").arg(verdict));

    impl_->appendSessionRecord(verdict, QString::fromStdString(result.runId));
    updateStartAvailability();
}

QString TestPage::currentScenarioCode() const
{
    if (impl_->productionMode)
        return productionScenarioForScope(impl_->scopeCombo->currentData().toString());
    return impl_->testCombo->currentData().toString();
}

bool TestPage::includeYvp() const
{
    return impl_->includeYvpCheck->isChecked();
}

bool TestPage::includeProductionOverload() const
{
    return impl_->includeOverload->isChecked();
}

bool TestPage::includeProductionSurvival() const
{
    return impl_->includeSurvival->isChecked();
}
