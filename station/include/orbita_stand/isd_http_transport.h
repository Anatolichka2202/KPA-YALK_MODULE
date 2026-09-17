#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace orbita::stand {

struct IsdHttpTransportConfig {
    std::string host;
    std::uint16_t port = 80;
    unsigned timeoutMilliseconds = 1500;
};

class IsdHttpTransport final {
public:
    explicit IsdHttpTransport(IsdHttpTransportConfig config);
    ~IsdHttpTransport();

    std::string probe();
    std::string command(const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orbita::stand
