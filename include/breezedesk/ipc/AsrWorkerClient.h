#pragma once

#include <breezedesk/ipc/FrameCodec.h>
#include <breezedesk/ipc/IAsrWorkerClient.h>

#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <QtNetwork/QLocalSocket>

namespace BreezeDesk::Ipc {

class AsrWorkerClient final : public IAsrWorkerClient {
    Q_OBJECT

  public:
    explicit AsrWorkerClient(QString clientVersion, QObject* parent = nullptr);

    void connectToWorker(const QString& serverName, const QByteArray& sessionToken) override;
    void disconnectFromWorker() override;
    [[nodiscard]] bool isReady() const noexcept override;
    QString sendRequest(MessageType type, const QString& jobId, const QCborMap& payload) override;

  private:
    void sendEnvelope(Envelope envelope);
    void readAvailable();
    void processEnvelope(const Envelope& envelope);
    void fail(ProtocolErrorCode code, const QString& detail);
    void heartbeat();

    QLocalSocket m_socket;
    FrameDecoder m_decoder;
    QTimer m_heartbeatTimer;
    QElapsedTimer m_lastPong;
    QString m_clientVersion;
    QByteArray m_sessionToken;
    bool m_ready = false;
};

} // namespace BreezeDesk::Ipc
