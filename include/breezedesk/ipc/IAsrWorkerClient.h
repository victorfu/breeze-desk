#pragma once

#include <breezedesk/ipc/Protocol.h>

#include <QtCore/QObject>

namespace BreezeDesk::Ipc {

class IAsrWorkerClient : public QObject {
    Q_OBJECT

  public:
    using QObject::QObject;
    ~IAsrWorkerClient() override = default;

    virtual void connectToWorker(const QString& serverName, const QByteArray& sessionToken) = 0;
    virtual void disconnectFromWorker() = 0;
    [[nodiscard]] virtual bool isReady() const noexcept = 0;
    virtual QString sendRequest(MessageType type, const QString& jobId, const QCborMap& payload) = 0;

  signals:
    void ready();
    void disconnected();
    void envelopeReceived(const BreezeDesk::Ipc::Envelope& envelope);
    void protocolError(const BreezeDesk::Ipc::ProtocolError& error);
};

} // namespace BreezeDesk::Ipc
