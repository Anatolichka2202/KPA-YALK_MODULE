#include "test_page.h"
#include "test_page_ui.h"
#include "test_page_impl.h"

#include <QEvent>

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
    if (code == QStringLiteral("PROD_YVP")) return QStringLiteral("Полная ЯВП-8 · ROKT");
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

} // namespace

TestPage::TestPage(QWidget* parent)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>(this))
{
    rebuildScopes();

    // The route at the left is navigation through one persistent test window.
    // Backend RunEvent remains the only authority that advances the real run.
    for (int i = 0; i < impl_->stageLabels.size(); ++i) {
        auto* label = impl_->stageLabels[i];
        label->setProperty("routeStageIndex", i);
        label->setCursor(Qt::PointingHandCursor);
        label->setToolTip(QStringLiteral("Открыть экран «%1»").arg(routeStageName(i)));
        label->installEventFilter(this);
    }

    // Agreed timing belongs to the upper status line as a separate text block,
    // not inside the consumption chart. Until ScenarioEngine exposes a planned
    // duration, total/remaining are explicitly shown as estimates from run
    // progress rather than invented fixed numbers.
    if (auto* workspaceLayout = qobject_cast<QVBoxLayout*>(impl_->workspacePage->layout())) {
        auto* status = new QFrame(impl_->workspacePage);
        status->setObjectName(QStringLiteral("testWindowStatus"));
        status->setStyleSheet(QStringLiteral(
            "#testWindowStatus{background:#10151b;border:1px solid #27313c;border-radius:5px;}"
            "#testWindowStatus QLabel{color:#aebdcb;padding:4px 8px;}"));
        auto* line = new QHBoxLayout(status);
        line->setContentsMargins(8, 3, 8, 3);
        line->setSpacing(14);
        auto makeTime = [status, line](const QString& name, const QString& text) {
            auto* label = new QLabel(text, status);
            label->setObjectName(name);
            line->addWidget(label);
            return label;
        };
        makeTime(QStringLiteral("runtimeElapsed"), QStringLiteral("Текущее время: 00:00:00"));
        makeTime(QStringLiteral("runtimeTotal"), QStringLiteral("Общая длительность: —"));
        makeTime(QStringLiteral("runtimeRemaining"), QStringLiteral("Осталось: —"));
        line->addStretch();
        workspaceLayout->insertWidget(1, status);
    }

    // Time is no longer duplicated in the bottom telemetry strip.
    if (impl_->elapsed && impl_->elapsed->parentWidget())
        impl_->elapsed->parentWidget()->hide();

    QObject::connect(impl_->runClockTimer, &QTimer::timeout, this, [this] {
        auto* current = findChild<QLabel*>(QStringLiteral("runtimeElapsed"));
        auto* total = findChild<QLabel*>(QStringLiteral("runtimeTotal"));
        auto* remaining = findChild<QLabel*>(QStringLiteral("runtimeRemaining"));
        if (!current || !total || !remaining || !impl_->runClock.isValid()) return;
        const qint64 elapsedMs = std::max<qint64>(0, impl_->runClock.elapsed());
        current->setText(QStringLiteral("Текущее время: %1").arg(elapsedText(elapsedMs)));
        const int percent = impl_->progress->value();
        if (percent >= 5 && percent < 100) {
            const qint64 estimatedTotal = elapsedMs * 100 / percent;
            total->setText(QStringLiteral("Общая длительность: ≈ %1")
                .arg(elapsedText(estimatedTotal)));
            remaining->setText(QStringLiteral("Осталось: ≈ %1")
                .arg(elapsedText(std::max<qint64>(0, estimatedTotal - elapsedMs))));
        } else if (percent >= 100) {
            total->setText(QStringLiteral("Общая длительность: %1").arg(elapsedText(elapsedMs)));
            remaining->setText(QStringLiteral("Осталось: 00:00:00"));
        } else {
            total->setText(QStringLiteral("Общая длительность: —"));
            remaining->setText(QStringLiteral("Осталось: —"));
        }
    });
}

TestPage::~TestPage() = default;

bool TestPage::eventFilter(QObject* watched, QEvent* event)
{
    auto* label = qobject_cast<QLabel*>(watched);
    if (label && event->type() == QEvent::MouseButtonRelease) {
        bool ok = false;
        const int index = label->property("routeStageIndex").toInt(&ok);
        if (ok && index >= 0 && index < impl_->workStack->count() && label->isVisible()) {
            const int runtimeStage = static_cast<int>(impl_->topStage);
            if (impl_->runInProgress && index > runtimeStage) {
                impl_->footerStage->setText(
                    QStringLiteral("Этап «%1» ещё не начат · выполняется: %2")
                        .arg(routeStageName(index), routeStageName(runtimeStage)));
                return true;
            }

            impl_->workStack->setCurrentIndex(index);
            if (impl_->runInProgress && index != runtimeStage) {
                impl_->footerStage->setText(
                    QStringLiteral("Просмотр: %1 · выполняется: %2")
                        .arg(routeStageName(index), routeStageName(runtimeStage)));
            } else {
                impl_->footerStage->setText(QStringLiteral("Просмотр: %1").arg(routeStageName(index)));
            }
            return true;
        }
    }
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
        ? QStringLiteral("Выберите зарегистрированное УБСИ из registrar.db. Один оператор может последовательно проверить несколько изделий.")
        : QStringLiteral("Проверка по ТУ: оператор видит измерительные графики и итоговый вердикт; служебные калибровки остаются внутри сценария."));
    impl_->workflowBadge->setText(enabled
        ? QStringLiteral("ПРОИЗВОДСТВО")
        : QStringLiteral("ПРОВЕРКА ПО ТУ"));
    impl_->workflowBadge->setStyleSheet(enabled
        ? QStringLiteral("background:#14251c;color:#70d79b;border:1px solid #315c43;border-radius:5px;padding:8px 12px;font-weight:700;")
        : QStringLiteral("background:#132033;color:#9ac7ff;border:1px solid #27466c;border-radius:5px;padding:8px 12px;font-weight:700;"));

    impl_->operatorCaption->setVisible(enabled);
    impl_->operatorEdit->setVisible(enabled);
    // Production never registers products from the test screen. The queue is
    // populated from registrar.db by KtmaMainWindow. TU keeps one serial input.
    impl_->serialCaption->setVisible(!enabled);
    impl_->serialEdit->setVisible(!enabled);
    impl_->addProduct->setVisible(false);
    impl_->productsPanel->setVisible(enabled);
    impl_->scopeButtons.value(QStringLiteral("ЯВП-8"))->setVisible(enabled);
    impl_->yalkSubPanel->setVisible(false);
    impl_->includeYvpCheck->setChecked(enabled);

    const bool showEngineeringDetail = enabled;
    const auto setMetricVisible = [showEngineeringDetail](QLabel* value) {
        if (value && value->parentWidget()) value->parentWidget()->setVisible(showEngineeringDetail);
    };

    for (auto* value : {
             impl_->powerSet, impl_->powerActual, impl_->powerCurrent, impl_->powerHold,
             impl_->yalkStream, impl_->yalkSequence,
             impl_->yalkCalZero, impl_->yalkCalFull,
             impl_->yalkChannel, impl_->yalkPoint, impl_->yalkV7,
             impl_->yalkDiscretePoint, impl_->yalkExpected, impl_->yalkDiscreteChannel,
             impl_->overloadChannel, impl_->overloadPolarity, impl_->overloadDelta,
             impl_->referenceV7, impl_->referenceYalk, impl_->referenceDelta,
             impl_->ytpStream, impl_->ytpEndpoint,
             impl_->ytpCalZero, impl_->ytpCalFull,
             impl_->ytpChannel, impl_->ytpReference, impl_->ytpMeasured,
             impl_->yvpChannel, impl_->yvpFrequency, impl_->yvpGain, impl_->yvpResult,
             impl_->finishPower, impl_->finishYalk, impl_->finishYtp, impl_->finishYvp}) {
        setMetricVisible(value);
    }

    impl_->yalkPhaseStrip->setVisible(enabled);
    impl_->yalkPhaseTitle->setVisible(enabled);
    impl_->ytpPhaseTitle->setVisible(enabled);
    impl_->yvpStatus->setVisible(enabled);
    impl_->ytpOperatorBanner->setVisible(false);
    impl_->finishDetail->setVisible(enabled);
    impl_->nextProduct->setVisible(enabled);
    impl_->reportButton->setText(enabled
        ? QStringLiteral("Открыть отчёт")
        : QStringLiteral("Открыть протокол ТУ"));

    impl_->pages->setCurrentWidget(impl_->sessionPage);
    rebuildScopes();
    updateSelectionSummary();
}

void TestPage::setAvailableProductionProducts(const QStringList& serials)
{
    if (!impl_->productionMode) return;
    const QString selected = impl_->productTable->currentRow() >= 0
        ? impl_->productTable->item(impl_->productTable->currentRow(), 0)->text()
        : QString();
    QStringList unique = serials;
    unique.removeDuplicates();
    unique.sort(Qt::CaseInsensitive);

    impl_->productTable->setRowCount(0);
    int selectedRow = -1;
    for (const QString& serial : unique) {
        const int row = impl_->productTable->rowCount();
        impl_->productTable->insertRow(row);
        impl_->productTable->setItem(row, 0, new QTableWidgetItem(serial));
        impl_->productTable->setItem(row, 1, new QTableWidgetItem(impl_->scopeDisplay()));
        auto* status = new QTableWidgetItem(QStringLiteral("ОЖИДАЕТ"));
        status->setForeground(QColor("#d7a95b"));
        impl_->productTable->setItem(row, 2, status);
        if (serial == selected) selectedRow = row;
    }
    if (selectedRow < 0 && impl_->productTable->rowCount() > 0) selectedRow = 0;
    if (selectedRow >= 0) {
        impl_->productTable->selectRow(selectedRow);
        impl_->serialEdit->setText(impl_->productTable->item(selectedRow, 0)->text());
    } else {
        impl_->serialEdit->clear();
    }
    impl_->scenarioInfo->setText(unique.isEmpty()
        ? QStringLiteral("В registrar.db нет зарегистрированных УБСИ. Регистрация выполняется в «Администрирование».")
        : QStringLiteral("Доступно УБСИ из registrar.db: %1").arg(unique.size()));
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
    impl_->scopeCombo->addItem(impl_->productionMode
                                  ? QStringLiteral("УБСИ · полная")
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
    impl_->yalkSubPanel->setVisible(impl_->productionMode && scope == QStringLiteral("ЯЛК-96"));

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
            impl_->productTable->item(row, 1)->setText(impl_->scopeDisplay());
    }

    const QString code = currentScenarioCode();
    const auto info = impl_->scenarios.value(code);
    if (!code.isEmpty()) {
        impl_->scenarioInfo->setText(info.detail.isEmpty()
            ? QStringLiteral("Сценарий: %1").arg(code)
            : info.detail);
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
    updateStartAvailability();
}

void TestPage::updateStartAvailability()
{
    const QString code = currentScenarioCode();
    const auto info = impl_->scenarios.value(code);
    const bool available = info.available;

    bool ready = available;
    if (ready) {
        for (const auto& equipmentCode : info.required) {
            if (equipmentCode == QStringLiteral("SCHEME") || equipmentCode == QStringLiteral("R4831"))
                continue;
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
        impl_->readiness->setText(info.detail.isEmpty()
            ? QStringLiteral("Исполняемый сценарий не готов")
            : info.detail);
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
            QMessageBox::warning(this, QStringLiteral("УБСИ"),
                QStringLiteral("Выберите зарегистрированное УБСИ из registrar.db."));
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
        QMessageBox::warning(this, QStringLiteral("УБСИ"),
                             QStringLiteral("Выберите заводской номер УБСИ."));
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
        if (auto* label = findChild<QLabel*>(QStringLiteral("runtimeElapsed")))
            label->setText(QStringLiteral("Текущее время: 00:00:00"));
        if (auto* label = findChild<QLabel*>(QStringLiteral("runtimeTotal")))
            label->setText(QStringLiteral("Общая длительность: —"));
        if (auto* label = findChild<QLabel*>(QStringLiteral("runtimeRemaining")))
            label->setText(QStringLiteral("Осталось: —"));
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
        if (index < 0 || index >= impl_->stageLabels.size()) return;
        auto* label = impl_->stageLabels[index];
        if (!label->isVisible()) return;
        const QString prefix = index == static_cast<int>(impl_->topStage)
            ? QStringLiteral("▶")
            : index < static_cast<int>(impl_->topStage) ? QStringLiteral("✓") : QStringLiteral("○");
        label->setText(QStringLiteral("%1  %2. %3\n%4")
            .arg(prefix).arg(index + 1).arg(routeStageName(index), detail));
    };

    if (event.stage == "START") {
        if (impl_->productionMode) {
            impl_->mapNode(node);
        } else {
            if (node == QStringLiteral("readiness")
                || node == QStringLiteral("supply_range")
                || node == QStringLiteral("supply_status")) {
                impl_->setTopStage(TopStage::Power);
            } else if (node.startsWith(QStringLiteral("yalk_"))
                       && !node.startsWith(QStringLiteral("yvp_"))) {
                impl_->setTopStage(TopStage::Yalk);
                if (node.contains(QStringLiteral("contact")))
                    impl_->setYalkPhase(YalkPhase::Discrete);
                else if (node.contains(QStringLiteral("overload")))
                    impl_->setYalkPhase(YalkPhase::Overload);
                else if (node.contains(QStringLiteral("reference")))
                    impl_->setYalkPhase(YalkPhase::Reference);
                else
                    impl_->setYalkPhase(YalkPhase::Analog);
            } else if (node.startsWith(QStringLiteral("ytp_"))) {
                impl_->setTopStage(TopStage::Ytp);
                impl_->setYtpPhase(YtpPhase::Channels);
            } else if (node.startsWith(QStringLiteral("yvp_"))) {
                impl_->setTopStage(TopStage::Yvp);
            } else {
                impl_->mapNode(node);
            }
        }
        if (node.startsWith(QStringLiteral("yalk_")) && !node.startsWith(QStringLiteral("yvp_")))
            setRouteDetail(static_cast<int>(TopStage::Yalk), yalkStepText(node));
        else if (node.startsWith(QStringLiteral("ytp_")))
            setRouteDetail(static_cast<int>(TopStage::Ytp), node.contains(QStringLiteral("channels"))
                ? QStringLiteral("30 каналов · 0 / 120 / 240 Ом")
                : node.contains(QStringLiteral("calibration"))
                    ? QStringLiteral("Калибровка") : QStringLiteral("Подготовка потока"));
        else if (node.startsWith(QStringLiteral("yvp_")))
            setRouteDetail(static_cast<int>(TopStage::Yvp), QStringLiteral("ROKT · 8 каналов"));
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
        impl_->powerHold->setText(duration > 0
            ? QStringLiteral("%1 / %2 с").arg(elapsed).arg(duration)
            : QStringLiteral("рабочая точка"));
        impl_->powerTrend->append(set, actual);
        impl_->powerSteps->setActiveValue(set);
        impl_->consumption->append(amperes);
        setRouteDetail(static_cast<int>(TopStage::Power),
            QStringLiteral("%1 В · %2 А").arg(actual, 0, 'f', 2).arg(amperes, 0, 'f', 3));
        impl_->updateProgressByStage();
        return;
    }

    if (event.stage == "OVERLOAD") {
        impl_->setTopStage(TopStage::Yalk);
        impl_->setYalkPhase(YalkPhase::Overload);
        const QString polarity = eventValue(event, "polarity");
        const QString channel = eventValue(event, "stressed_channel");
        const QString count = eventValue(event, "target_count");
        impl_->overloadChannel->setText(channel.isEmpty() ? QStringLiteral("—") : channel);
        impl_->overloadPolarity->setText(polarity.isEmpty() ? QStringLiteral("±12 В") : polarity);
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

    if (event.stage == "BACKGROUND") return;
    if (event.stage != "MEASUREMENT") return;

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
        ChannelSample sample{channel,
                             QStringLiteral("%1 Ом").arg(ref, 0, 'f', 0),
                             ref,
                             measured,
                             false,
                             event.verdict == orbita::stand::RunVerdict::Ok,
                             csvNumbers(eventValue(event, "value_samples"))};
        impl_->ytpOverview->add(std::move(sample));
        setRouteDetail(static_cast<int>(TopStage::Ytp),
            QStringLiteral("Канал %1 / 30 · Р4831 %2 Ом")
                .arg(channel).arg(ref, 0, 'f', 0));
        impl_->updateProgressByStage();
        return;
    }

    if (!eventValue(event, "ulk_address").isEmpty()) {
        impl_->setTopStage(TopStage::Yalk);
        if (node.contains(QStringLiteral("contact")))
            impl_->setYalkPhase(YalkPhase::Discrete);
        else
            impl_->setYalkPhase(YalkPhase::Analog);

        const QString address = eventValue(event, "ulk_address");
        const double command = eventValue(event, "command_v").toDouble();
        const double v7 = eventValue(event, "v7_v").toDouble();
        const double yalk = eventValue(event, "yalk_v").toDouble();
        const int signal = eventValue(event, "signal").toInt();

        if (impl_->yalkPhase == YalkPhase::Discrete) {
            const int expected = command >= 2.0 ? 1 : 0;
            impl_->yalkDiscretePoint->setText(QStringLiteral("%1 В").arg(command, 0, 'f', 1));
            impl_->yalkExpected->setText(QString::number(expected));
            impl_->yalkDiscreteChannel->setText(address);
            impl_->yalkDiscrete->setCurrent(address, signal, expected);
            impl_->yalkDiscreteSteps->setActiveValue(command);
            setRouteDetail(static_cast<int>(TopStage::Yalk),
                QStringLiteral("Дискретные пороги · канал %1 · %2 В")
                    .arg(address).arg(command, 0, 'f', 1));
        } else {
            impl_->yalkChannel->setText(address);
            impl_->yalkPoint->setText(QStringLiteral("%1 В").arg(command, 0, 'f', 1));
            impl_->yalkV7->setText(QStringLiteral("%1 В").arg(v7, 0, 'f', 3));
            ChannelSample sample{address,
                                 QStringLiteral("%1 В").arg(command, 0, 'f', 1),
                                 v7,
                                 yalk,
                                 signal != 0,
                                 event.verdict == orbita::stand::RunVerdict::Ok,
                                 csvNumbers(eventValue(event, "value_samples"))};
            impl_->yalkOverview->add(std::move(sample));
            setRouteDetail(static_cast<int>(TopStage::Yalk),
                QStringLiteral("Аналоговые · канал %1 · %2 В")
                    .arg(address).arg(command, 0, 'f', 1));
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

    const qint64 elapsedMs = impl_->runClock.isValid() ? impl_->runClock.elapsed() : 0;
    if (auto* label = findChild<QLabel*>(QStringLiteral("runtimeElapsed")))
        label->setText(QStringLiteral("Текущее время: %1").arg(elapsedText(elapsedMs)));
    if (auto* label = findChild<QLabel*>(QStringLiteral("runtimeTotal")))
        label->setText(QStringLiteral("Общая длительность: %1").arg(elapsedText(elapsedMs)));
    if (auto* label = findChild<QLabel*>(QStringLiteral("runtimeRemaining")))
        label->setText(QStringLiteral("Осталось: 00:00:00"));

    const QString verdict = verdictText(result.verdict);
    impl_->finishVerdict->setText(impl_->productionMode
        ? verdict
        : QStringLiteral("ТУ · %1").arg(verdict));
    impl_->finishVerdict->setStyleSheet(
        QStringLiteral("font-size:31px;font-weight:800;color:%1;")
            .arg(verdictColor(result.verdict).name()));
    impl_->finishDetail->setText(QStringLiteral("SN %1 · run_id %2")
        .arg(impl_->activeSerial, QString::fromStdString(result.runId)));
    impl_->finishPower->setText(QStringLiteral("завершено"));
    impl_->finishYalk->setText(QStringLiteral("завершено"));
    impl_->finishYtp->setText(QStringLiteral("завершено"));
    impl_->finishYvp->setText(impl_->includeYvpCheck->isChecked()
        ? QStringLiteral("по сценарию")
        : QStringLiteral("—"));
    impl_->reportButton->setEnabled(!tuReportPath.isEmpty() || !productionReportPath.isEmpty());

    if (impl_->productionMode
        && impl_->activeRow >= 0
        && impl_->activeRow < impl_->productTable->rowCount()) {
        auto* item = impl_->productTable->item(impl_->activeRow, 2);
        item->setText(verdict);
        item->setForeground(verdictColor(result.verdict));
    }

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
