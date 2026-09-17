#include "orbita_stand/equipment_adapters.h"
#include "../plugins/plugin_support.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>

using namespace orbita::stand;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        // 1. Verify ISD Path Constants
        require(IsdHttpRouter::fullResetPath() == "/type=4num=1", "Full reset path mismatch");
        require(IsdHttpRouter::yalkPreparePath() == "/type=7num=1", "YALK prepare path mismatch");

        // 2. Verify IsdHttpRouter::prepareYalk does not throw (now a no-op)
        // We can't easily verify it doesn't call httpGet without a mock,
        // but we can ensure the API is stable.
        IsdHttpConfig config{"127.0.0.1", 80};
        IsdHttpRouter router(config);
        router.prepareYalk();

        std::cout << "ISD Path and Basic API tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "Regression test failed: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
