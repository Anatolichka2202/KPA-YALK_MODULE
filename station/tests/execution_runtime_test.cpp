#include "orbita_stand/component_runtime.h"
#include "orbita_stand/config.h"
#include "orbita_stand/execution_runtime.h"

#include <QCoreApplication>

#include <iostream>
#include <stdexcept>

using namespace orbita::stand;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void executionRuntimeFactoryContract()
{
    ComponentRuntime components;
    registerExecutionRuntimeComponents(components);

    StandProfile profile;
    profile.id = "exec-test";
    profile.version = "1";
    profile.components.push_back(ComponentProfile{
        "external-process",
        "execution_runtime",
        "miltech.exec.process",
        true,
        {"runtime.external"},
        {{"program", "placeholder-program"}},
    });

    components.instantiate(profile, {"execution_runtime"});
    auto* runtime = components.findAs<IExecutionRuntime>("runtime.external");
    require(runtime != nullptr, "execution runtime binding was not instantiated");
    require(!runtime->isRunning(), "new execution runtime must be idle");
    require(components.components().size() == 1, "unexpected execution component count");
    require(components.components().front().provider == "miltech.exec.process",
            "execution provider was not preserved");
}

void realProcessContract()
{
    auto runtime = createExecutionRuntime("miltech.exec.process", {});
    ExecutionRequest request;
    request.target = QCoreApplication::applicationFilePath().toUtf8().toStdString();
    request.arguments = {"--execution-child"};
    request.timeoutMs = 5000;

    const auto result = runtime->execute(request);
    require(result.started, "external process did not start");
    require(!result.cancelled, "normal external process was marked cancelled");
    require(!result.timedOut, "normal external process was marked timed out");
    require(result.exitCode == 7, "external process exit code was not preserved");
    require(result.standardOutput.find("child-stdout") != std::string::npos,
            "external process stdout was not captured");
    require(result.standardError.find("child-stderr") != std::string::npos,
            "external process stderr was not captured");
    require(!runtime->isRunning(), "execution runtime remained busy after child exit");
}

void unknownProviderIsRejected()
{
    bool rejected = false;
    try {
        auto runtime = createExecutionRuntime("unknown.provider", {});
        (void)runtime;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "unknown execution provider must be rejected");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    if (argc == 2 && std::string(argv[1]) == "--execution-child") {
        std::cout << "child-stdout\n" << std::flush;
        std::cerr << "child-stderr\n" << std::flush;
        return 7;
    }

    try {
        executionRuntimeFactoryContract();
        realProcessContract();
        unknownProviderIsRejected();
        std::cout << "execution runtime contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "execution runtime contract failed: " << error.what() << '\n';
        return 1;
    }
}
