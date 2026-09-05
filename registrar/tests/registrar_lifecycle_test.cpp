#include "registrar.h"
#include "report.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTemporaryDir>

#include <array>
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

void finishAllStages(Registrar& registrar,
                     const std::string& productId,
                     const std::string& componentId,
                     const std::string& runPrefix)
{
    const std::array<Stage, 4> stages = {
        Stage::InitialElectrical, Stage::PostVibrationElectrical,
        Stage::PostClimateElectrical, Stage::FinalElectrical};
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
        registrar.attachRun(failedAttempt, "run-failed");
        registrar.finishStage(failedAttempt, Verdict::Fail);
        require(registrar.productVerdict(product) == Verdict::Fail,
                "active failed cell must produce Fail");

        registrar.removeComponent(product, yalkOld, "контактный дефект");
        require(!registrar.listInstalledComponents(product).front().active,
                "removed cell must remain in history");
        registrar.installComponent(product, yalkNew);
        require(registrar.productVerdict(product) == Verdict::Incomplete,
                "replacement must restart the lifecycle");

        finishAllStages(registrar, product, yalkNew, "run-yalk-");
        finishAllStages(registrar, product, ytp, "run-ytp-");
        require(registrar.productVerdict(product) == Verdict::Ok,
                "all active cells and stages must produce Ok");

        const auto report = registrar.productReport(product);
        const auto reportPath = writeProductReportHtml(
            report, directory.filePath("reports/ktma.html").toStdString());
        require(QFileInfo::exists(QString::fromStdString(reportPath)),
                "product report was not written");

        const auto attempts = registrar.listStageAttempts(product);
        require(attempts.size() == 9, "history must contain failed and replacement attempts");
        require(registrar.listInstalledComponents(product).size() == 3,
                "replacement must not delete the old binding");
        expectThrows([&] { registrar.attachRun(failedAttempt, "run-failed"); },
                     "a run must not be attached twice");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "registrar lifecycle: OK\n";
    return 0;
}
