#include "orbita_stand/component_runtime.h"
#include "orbita_stand/config.h"
#include "orbita_stand/execution_runtime.h"

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

int main()
{
    try {
        executionRuntimeFactoryContract();
        unknownProviderIsRejected();
        std::cout << "execution runtime contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "execution runtime contract failed: " << error.what() << '\n';
        return 1;
    }
}
