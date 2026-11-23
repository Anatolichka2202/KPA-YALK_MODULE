#pragma once
// Proposed boundary for MilTech Station Core; NOT IMPLEMENTED in the current Core.
// Qt 6 / C++17. A plugin gets exclusive RAII resource leases from the host.
#include <QHostAddress>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <memory>

namespace miltech::station {
enum class Transport { Tcp, Udp };
enum class EndpointDirection { Listen, Connect };

struct EndpointSpec {
    QString declaredName;
    Transport transport;
    EndpointDirection direction;
    QHostAddress localOrRemoteAddress;
    quint16 port;
    QString interfaceRole; // enterprise / ot / service / loopback
    QString ownerPackageId;
};

struct ResourceSpec {
    QString physicalResourceId; // E.g. COM3, VISA instrument resource, USB identity
    QString kind;
    QString ownerPackageId;
    bool exclusive = true;
};

class ILease {
public:
    virtual ~ILease() = default;
    virtual QString owner() const = 0;
};

// No bind-then-release: the returned lease must retain the actual socket handle.
// A future implementation should expose the adopted QTcpServer/QUdpSocket handle.
class IStationResourceBroker {
public:
    virtual ~IStationResourceBroker() = default;
    virtual std::unique_ptr<ILease> acquireResource(const ResourceSpec&, QString *error) = 0;
    virtual std::unique_ptr<ILease> bindEndpoint(const EndpointSpec&, QString *error) = 0;
};

struct TuPackageManifest {
    QString packageId;
    QString packageVersion;
    QByteArray manifestSha256;
    QStringList requiredCapabilities;
    QStringList allowedResourceIds;
    QStringList allowedSocketNames;
};

class IStationEnvironment {
public:
    virtual ~IStationEnvironment() = default;
    virtual QString stationId() const = 0;
    virtual QByteArray activeProfileSha256() const = 0;
    virtual bool isAvailableOffline() const = 0;
    virtual bool hasCapabilities(const QStringList&) const = 0;
    // Inspection only; never mutate IP routes/firewall from a runtime TU package.
    virtual QStringList environmentProblems(const TuPackageManifest&) const = 0;
};

class IRunOutbox {
public:
    virtual ~IRunOutbox() = default;
    // Called only AFTER a committed local RunStore transaction.
    // Atomicity across different local SQLite databases requires a recovery protocol.
    virtual bool enqueueCommittedRun(const QString& sourceRunId,
                                     const QByteArray& canonicalJson,
                                     QString *error) = 0;
};
} // namespace miltech::station
