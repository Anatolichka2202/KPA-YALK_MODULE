#pragma once
// MilTech Station — PROPOSED C++/Qt contract for the external release supervisor.
// No current Core source file includes this header. Integrate only after ABI review.

#include <QString>
#include <QStringList>
#include <QDateTime>

namespace miltech::delivery {

struct ReleaseIdentity final {
    QString releaseId;          // UUID, immutable published release
    QString version;            // SemVer, informational only
    QString manifestSha256;     // authenticated by publisher signature
    QString targetPlatform;
};

struct ReleaseCandidate final {
    ReleaseIdentity identity;
    QString releaseRoot;        // must be within immutable Program Files/releases/
    bool pendingBoot = false;
};

struct StationMaintenanceState final {
    bool allowed = false;
    bool runStoreQuiescent = false;
    bool hardwareResourcesReleased = false;
    bool registrarFlushed = false;
    QString refusalReason;
};

// Core MUST own the safety gate. A CLI flag is not a substitute.
class IMaintenanceGate {
public:
    virtual ~IMaintenanceGate() = default;
    virtual StationMaintenanceState queryAndEnterMaintenance() = 0;
    virtual bool leaveMaintenance() = 0;
};

// To be implemented in a separately installed StationBootstrap / Windows service.
// Must not terminate an active test to perform an automatic update.
class IStationReleaseSupervisor {
public:
    virtual ~IStationReleaseSupervisor() = default;
    virtual bool stageVerifiedRelease(const QString& downloadedBundlePath) = 0;
    virtual bool requestActivation(const QString& releaseId,
                                   const StationMaintenanceState& granted) = 0;
    virtual ReleaseCandidate resolveNextBoot() = 0;
    virtual bool confirmReady(const QString& releaseId) = 0;
    virtual bool requestRollback(const StationMaintenanceState& granted) = 0;
};

// TU executable packaging remains per-component. It does NOT receive direct
// network, COM, VISA or USB access except approved, delegated Core capabilities.
struct TuPackageDescription final {
    QString componentId;
    QString packageVersion;
    QString executableRelativePath;
    QString coreApiRequirement;
    QStringList requiredCapabilities;
};

} // namespace miltech::delivery
