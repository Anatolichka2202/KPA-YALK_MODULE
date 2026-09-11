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

} // namespace

TestPage::TestPage(QWidget* parent)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>(this))
{
    rebuildScopes();

    // Route entries are navigation, not passive status labels. Before a run the
    // operator may inspect every visible screen. During a run only the current
    // and already reached stages are open; backend RunEvent remains the sole
    // authority that advances the actual procedure.
    for (int i = 0; i < impl_->stageLabels.size(); ++i) {
        auto* label = impl_->stageLabels[i];
        label->setProperty("routeStageIndex", i);
        label->setCursor(Qt::PointingHandCursor);
        label->setToolTip(QStringLiteral("Открыть экран «%1»").arg(routeStageName(i)));
        label->installEventFilter(this);
    }
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
        ? QStringLiteral("Один оператор может последовательно проверить несколько УБСИ. Выбранное изделие и маршрут сохраняются при переходе в испытательное окно.")
        : QStringLiteral("Проверка по ТУ: оператор видит измерительные графики и итоговый вердикт; служебные калибровки остаются внутри сценария."));
    impl_->workflowBadge->setText(enabled
        ? QStringLiteral("ПРОИЗВОДСТВО")
        : QStringLiteral("ПРОВЕРКА ПО ТУ"));
    impl_->workflowBadge->setStyleSheet(enabled
        ? QStringLiteral("background:#14251c;color:#70d79b;border:1px solid #315c43;border-radius:5px;padding:8px 12px;font-weight:700;")
        : QStringLiteral("background:#132033;color:#9ac7ff;border:1px solid #27466c;border-radius:5px;padding:8px 12px;font-weight:700;"));

    impl_->operatorCaption->setVisible(enabled);
    impl_->operatorEdit->setVisible(enabled);
    impl_->addProduct->setVisible(enabled);
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

    // In production the visible cards are the source of truth. The hidden
    // combo remains only as a compatibility bridge for legacy engineering
    // actions; it is synchronised from the card selection, never the reverse.
    if (impl_->productionMode) {
        const QString code = productionScenarioForScope(scope);
        if (impl_->testCombo->count() != 1 || impl_->testCombo->currentData().toString() != code) {
            impl_->testCombo->blockSignals(true);
            impl_->testCombo->clear();
            impl_->testCombo->addItem(productionScenarioTitle(code), code);
            impl_->testCombo->setCurrentIndex(0);
            impl_->testCombo->blockSignals(false);
        }
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
        impl_->readiness->setText(QStringLiteral("Проверьте оборудование для выбранного сценария"));
        impl_->readiness->setStyleSheet(QStringLiteral("color:#d7a95b;font-weight:700;"));
    } else {
        impl_->readiness->setText(QStringLiteral("Оборудование готово. Можно запускать проверку."));
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

    if (impl_->productionMode && impl_->productTable->currentRow() >= 0) {
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
                             QStringLiteral("Введите заводской номер УБСИ."));
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
        impl_->updateProgressByStage();
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
