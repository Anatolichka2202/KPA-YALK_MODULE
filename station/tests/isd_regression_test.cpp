#include "orbita_stand/equipment_adapters.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace orbita::stand;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main()
{
    try {
        require(IsdHttpRouter::fullResetPath() == "/type=4num=1",
                "Full reset path mismatch");

        // Legacy router API is still compiled for compatibility, but YALK
        // preparation must be a pure no-op.  Point it at loopback with a tiny
        // timeout: any accidental HTTP request/reset/type=7 call would fail.
        IsdHttpConfig config;
        config.host = "127.0.0.1";
        config.port = 1;
        config.timeoutMilliseconds = 25;
        IsdHttpRouter router(config);
        router.prepareYalk();

        std::cout << "Legacy ISD prepareYalk is inert; explicit reset remains service-only\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "ISD regression test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
