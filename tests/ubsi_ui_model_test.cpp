#include "model/ubsi_ui_model.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

tu::RunEvent event(std::string node, std::string stage, tu::RunVerdict verdict,
                   std::map<std::string, std::string> data)
{
    return {std::chrono::system_clock::now(), std::move(node), std::move(stage),
            {}, verdict, std::move(data)};
}

} // namespace

int main()
{
    try {
        ubsi::ui::UiAdapter ui;

        ui.apply(event("yalk_channels", "MEASUREMENT", tu::RunVerdict::Ok,
            {{"ulk_address", "1"}, {"command_v", "4"}, {"signal", "1"},
             {"expected_signal", "0"}, {"raw_match", "false"},
             {"verdict_policy", "formal_norma"}}));
        require(ui.yalkContact.channels[0].verification == ubsi::ui::VerificationState::Norma,
                "Формально принятая контактная точка должна быть зелёной в UI");
        require(ui.yalkAnalog.channels[0].verification == ubsi::ui::VerificationState::Pending,
                "Контактное событие не должно окрашивать аналоговый график");
        require(ui.yalkAnalog.channels[0].contactLogic == 1
                    && ui.yalkAnalog.channels[0].expectedContactLogic == 0,
                "Комбинированный экран должен показывать фактический и ожидаемый контактные биты");
        require(ui.yalkAnalog.channels[0].contactVerification
                    == ubsi::ui::VerificationState::Norma,
                "Формально принятый контактный бит должен быть зелёным на комбинированном экране");
        require(ui.run.currentProcedure == ubsi::ui::Procedure::YalkAnalog,
                "Контактная точка объединённого прохода не должна переключать отдельный экран");
        ui.apply(event("yalk_channels", "BACKGROUND", tu::RunVerdict::NotRun,
            {{"section", "YALK"}, {"background_mean", "4.01,4.02"},
             {"background_min", "4.00,4.01"}, {"background_max", "4.02,4.03"},
             {"background_codes", "512,513"}, {"background_contacts", "1,0"}}));
        require(ui.yalkAnalog.channels[0].rawCode == 512
                    && ui.yalkAnalog.channels[1].rawCode == 513
                    && ui.yalkAnalog.channels[1].contactLogic == 0,
                "Все подписи кодов и контактные биты должны обновляться свежей телеметрией");

        ui.apply(event("ytp_channels", "OPERATOR", tu::RunVerdict::NotRun,
            {{"target_resistance_ohm", "120"}, {"point_index", "2"}, {"point_count", "3"}}));
        ui.apply(event("ytp_channels", "BACKGROUND", tu::RunVerdict::NotRun,
            {{"section", "YTP"}, {"background_mean", "120.25,120.31"},
             {"background_min", "120.20,120.28"},
             {"background_max", "120.30,120.34"}}));
        require(ui.run.runtimeState == ubsi::ui::RuntimeState::WaitingOperator
                    && ui.ytp.channels[0].currentOhm == 120.25
                    && ui.ytp.channels[1].currentOhm == 120.31,
                "Живой график ЯТП должен обновляться во время ожидания Р4831");

        ui.apply(event("yvp_channels", "YVP_V7_POINT", tu::RunVerdict::NotRun,
            {{"yvp_channel", "3"}, {"gain_mv_per_pc", "2"},
             {"set_frequency_hz", "500"}, {"v7_output_vrms", "1.23"},
             {"calculated_gain_mv_per_pc", "9.45"}, {"point_index", "27"},
             {"point_count", "144"}}));
        require(ui.yvp.channels[2].verification == ubsi::ui::VerificationState::Pending,
                "Сырое измерение ЯВП не должно окрашиваться в НЕ НОРМА");

        ui.apply(event("yvp_channels", "YVP_CRITERION", tu::RunVerdict::Ok,
            {{"yvp_channel", "3"}, {"raw_verdict", "FAIL"},
             {"acceptance_verdict", "OK"}, {"verdict_policy", "manual_confirmed"}}));
        require(ui.yvp.channels[2].verification == ubsi::ui::VerificationState::Norma,
                "Формально принятый критерий ЯВП должен быть зелёным в UI");

        tu::ScenarioRunResult result;
        result.verdict = tu::RunVerdict::Ok;
        result.steps = {
            {"yalk_contact_thresholds", "Контактные", "1.1.4.1",
             tu::RunVerdict::Ok, "Норма", {}, {}, false},
            {"yvp_channels", "ЯВП", "1.1.4.7, 1.1.4.8",
             tu::RunVerdict::Ok, "Норма", {}, {}, false},
        };
        ui.applyResult(result);
        require(ui.run.productVerdict == ubsi::ui::VerificationState::Norma,
                "Итог формального production-прогона должен быть НОРМА");
        require(ui.summaries[1].verdict == ubsi::ui::VerificationState::Norma,
                "Итог ЯЛК должен быть НОРМА");
        require(ui.summaries[3].verdict == ubsi::ui::VerificationState::Norma,
                "Итог ЯВП должен быть НОРМА");

        ui.apply(event("yvp_channels", "ISD_PAUSE", tu::RunVerdict::NotRun, {}));
        require(ui.run.runtimeState == ubsi::ui::RuntimeState::WaitingOperator,
                "При отсутствии ответа ИСД UI должен перейти в ожидание оператора");
        ui.apply(event("yvp_channels", "ISD_RESUMED", tu::RunVerdict::NotRun, {}));
        require(ui.run.runtimeState == ubsi::ui::RuntimeState::Running,
                "После восстановления ИСД UI должен продолжить прогон");

        std::cout << "ubsi_ui_model_test: OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ubsi_ui_model_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
