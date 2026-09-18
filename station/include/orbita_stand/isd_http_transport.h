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

struct IsdHttpResponse {
    int status = 0;
    std::string body;
    unsigned elapsedMilliseconds = 0;
};

class IsdHttpTransport final {
public:
    explicit IsdHttpTransport(IsdHttpTransportConfig config);
    ~IsdHttpTransport();

    // Exactly one physical HTTP attempt.  timeoutOverrideMilliseconds == 0
    // selects the normal configured timeout.  No retry is performed here.
    IsdHttpResponse get(const std::string& path,
                        unsigned timeoutOverrideMilliseconds = 0);

    static void requireCommandAck(const IsdHttpResponse& response);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orbita::stand
