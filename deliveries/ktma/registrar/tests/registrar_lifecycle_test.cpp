#include "registrar.h"
#include "report.h"
#include "replacement_policy.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTemporaryDir>

#include <array>
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace ktma::registrar;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void expectThrows(const std::function<void()>& action, const char* message)
{
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void finishProductionStages(Registrar& registrar,
                     const std::string& productId,
                     const std::string& componentId,
                     const std::string& runPrefix)
{
    const std::array<Stage, 7> stages = {
        Stage::Primary, Stage::ClimateNormal, Stage::ClimateMinus,
        Stage::ClimatePlus, Stage::PottingClimateNormal,
        Stage::PottingClimatePlus, Stage::PottingClimateMinus};
    int index = 0;
    for (Stage stage : stages) {
        const auto attempt = registrar.beginComponentStage(productId, componentId, stage);
        registrar.attachRun(attempt, runPrefix + std::to_string(index));
        registrar.finishStage(attempt, Verdict::Ok);
        ++index;
    }
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir directory;
        require(directory.isValid(), "temporary directory was not created");
        Registrar registrar(directory.filePath("registrar.db").toStdString());

        const auto product = registrar.createProduct("UBSI", "UBSI-001");
        require(registrar.findProductBySerial("UBSI-001")->id == product,
                "TU lookup must find an existing product without creating it");
        require(!registrar.findProductBySerial("UNKNOWN-SN"),
                "TU lookup must not create an unknown product");
        registrar.attachTuRun(product, "run-tu-1", "OK");
        require(registrar.listTuRuns(product).size() == 1
                    && registrar.listTuRuns(product).front().runId == "run-tu-1",
                "TU run link must be visible in product history");
        require(replacementVerificationPackages("YALK-96") == std::vector<std::string>{"PROD_YALK_FULL"}
                    && replacementVerificationPackages("YTP") == std::vector<std::string>{"PROD_YTP_FULL"}
                    && replacementVerificationPackages("YVP") == std::vector<std::string>{"PROD_YVP_FULL", "PROD_YALK_88_96"}
                    && replacementVerificationPackages("YP-P") == std::vector<std::string>{"PROD_POWER_CONSUMPTION"},
                "replacement verification policy must cover each current cell type");
        const auto yalkOld = registrar.createComponent("YALK-96", "YALK-001");
        const auto yalkNew = registrar.createComponent("YALK-96", "YALK-002");
        const auto ytp = registrar.createComponent("YTP", "YTP-001");
        registrar.installComponent(product, yalkOld);
        registrar.installComponent(product, ytp);
        expectThrows([&] { registrar.installComponent(product, yalkNew); },
                     "a second active cell of one type must be rejected");
        require(registrar.productVerdict(product) == Verdict::Incomplete,
                "missing stages must produce Incomplete");

        const auto failedAttempt = registrar.beginComponentStage(
            product, yalkOld, Stage::InitialElectrical);
        expectThrows([&] { registrar.finishStage(failedAttempt, Verdict::Fail); },
                     "a terminal stage without run_id must be rejected");
        registrar.attachRun(failedAttempt, "run-failed");
        registrar.finishStage(failedAttempt, Verdict::Fail);
        require(registrar.productVerdict(product) == Verdict::Incomplete,
                "legacy failed stage must not define a new production verdict");

        registrar.removeComponent(product, yalkOld, "контактный дефект");
        require(!registrar.listInstalledComponents(product).front().active,
                "removed cell must remain in history");
        registrar.installComponent(product, yalkNew);
        require(registrar.productVerdict(product) == Verdict::Incomplete,
                "replacement must restart the lifecycle");

        finishProductionStages(registrar, product, yalkNew, "run-yalk-");
        finishProductionStages(registrar, product, ytp, "run-ytp-");
        require(registrar.productVerdict(product) == Verdict::Incomplete,
                "without a verification policy product verdict must not claim Ok");

        const auto report = registrar.productReport(product);
        const auto reportPath = writeProductReportHtml(
            report, directory.filePath("reports/ktma.html").toStdString());
        require(QFileInfo::exists(QString::fromStdString(reportPath)),
                "product report was not written");
        QFile productHtml(QString::fromStdString(reportPath));
        require(productHtml.open(QIODevice::ReadOnly), "product report was not readable");
        const auto productHtmlText = productHtml.readAll();
        require(productHtmlText.contains("Заливка · климат −")
                    && !productHtmlText.contains("После вибрации"),
                "product report must show the current production stages, not legacy Electrical stages");

        const auto attempts = registrar.listStageAttempts(product);
        require(attempts.size() == 15, "history must contain legacy and production attempts");
        require(std::any_of(attempts.begin(), attempts.end(), [](const StageAttempt& attempt) {
            return attempt.stage == Stage::Primary && attempt.runId == "run-yalk-0";
        }), "production stage must retain its attached run_id");
        require(registrar.listInstalledComponents(product).size() == 3,
                "replacement must not delete the old binding");
        const auto replacementYtp = registrar.replaceComponent(
            product, ytp, "YTP", "YTP-002", "плановая замена");
        const auto afterAtomicReplacement = registrar.listInstalledComponents(product);
        require(std::count_if(afterAtomicReplacement.begin(), afterAtomicReplacement.end(),
                    [](const ComponentBinding& binding) { return binding.active; }) == 2
                    && std::any_of(afterAtomicReplacement.begin(), afterAtomicReplacement.end(),
                        [&replacementYtp](const ComponentBinding& binding) {
                            return binding.componentId == replacementYtp && binding.active;
                        }),
                "atomic replacement must preserve one active cell of each installed type");
        expectThrows([&] { registrar.attachRun(failedAttempt, "run-failed"); },
                     "a run must not be attached twice");

        const auto removedOnlyProduct = registrar.createProduct("UBSI", "UBSI-REMOVED");
        const auto removedOnlyComponent = registrar.createComponent("YTP", "YTP-REMOVED");
        registrar.installComponent(removedOnlyProduct, removedOnlyComponent);
        registrar.removeComponent(removedOnlyProduct, removedOnlyComponent, "test removal");
        require(registrar.productVerdict(removedOnlyProduct) == Verdict::Incomplete,
                "product without active components must remain incomplete");

        Registrar reopened(directory.filePath("registrar.db").toStdString());
        require(reopened.listProducts().size() == 2,
                "products must persist in registrar.db for a new registrar instance");
        require(reopened.listTuRuns(product).size() == 1,
                "TU run links must persist after restart");
        require(stageFromString("InitialElectrical") == Stage::InitialElectrical,
                "legacy stage values must remain readable");
        require(stageFromString("PottingClimateMinus") == Stage::PottingClimateMinus,
                "new production stage values must be readable");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "registrar lifecycle: OK\n";
    return 0;
}
