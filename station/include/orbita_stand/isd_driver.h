#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace orbita::stand {

// This state describes only whether the current host process has unresolved
// ISD mutations. It deliberately does NOT describe the global physical state
// of every relay/output inside the ISD: the firmware exposes no full readback.
enum class IsdSessionState {
    Operational,
    Indeterminate,
};

enum class IsdOwnedCertainty {
    AcknowledgedActive,
    PossiblyActive,
};

struct IsdDriverOps {
    std::function<std::string()> probe;
    std::function<void(unsigned, unsigned, bool)> setSwitch;
    std::function<void(unsigned, unsigned, bool)> setAnalog;
    std::function<void(unsigned, double)> setYalkVoltage;
    std::function<void(unsigned)> disableYalkOutput;
};

class IsdDriver final {
public:
    explicit IsdDriver(IsdDriverOps ops, std::size_t traceCapacity = 256);
    ~IsdDriver();

    // Connectivity only. A successful probe does not establish relay/UART
    // readiness. A failed probe invalidates the one-session baseline because
    // the ISD may have restarted or changed state while unreachable.
    std::string probe();

    // One acknowledged address-by-address OFF sweep per process session.
    // Returns false only when a previous sweep is still valid and no route is owned.
    // No firmware type=4 command is sent.
    bool establishAddressedBaseline(const std::vector<unsigned>& type3Contacts,
                                    const std::vector<unsigned>& type2Contacts,
                                    const std::vector<unsigned>& analogType1Contacts,
                                    unsigned commandGapMs,
                                    const std::string& owner);

    void setSwitch(unsigned type, unsigned channel, bool enabled,
                   const std::string& owner);
    void setAnalog(unsigned channel, unsigned value, bool enabled,
                   const std::string& owner);
    void setYalkVoltage(unsigned channel, double volts,
                        const std::string& owner);
    void disableYalkOutput(unsigned channel,
                           const std::string& owner);

    // Explicit recovery after the operator has physically restarted ISD.
    // The driver probes connectivity, then replays the exact desired state of
    // every process-owned route, including a route whose ON acknowledgement was
    // lost. No firmware full reset and no implicit retry of the failed command
    // is performed. The caller must establish that ISD was actually restarted.
    void recoverAfterRestart(const std::string& owner = "recovery");

    // Targeted cleanup. Owned/possibly-active resources are released in
    // reverse activation order. Cleanup is allowed while Indeterminate.
    void releaseOwner(const std::string& owner);
    void safeStopAll() noexcept;

    IsdSessionState sessionState() const noexcept;
    bool addressedBaselineAcknowledged() const noexcept;
    std::size_t ownedCount() const noexcept;
    std::string statusText() const;
    std::string traceText() const;
    void clearTrace();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

const char* toString(IsdSessionState state) noexcept;
const char* toString(IsdOwnedCertainty certainty) noexcept;

} // namespace orbita::stand
