#pragma once

#include <breezedesk/ipc/FrameCodec.h>

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtNetwork/QLocalServer>

#include <memory>

QT_FORWARD_DECLARE_CLASS(QLocalSocket)

namespace BreezeDesk::Ipc {

class WorkerServer final : public QObject {
    Q_OBJECT

  public:
    explicit WorkerServer(QObject* parent = nullptr);
    ~WorkerServer() override;

    [[nodiscard]] bool listen(const QString& serverName, const QByteArray& sessionToken);
    void close();
    [[nodiscard]] bool isListening() const noexcept;
    [[nodiscard]] QString errorString() const;
    [[nodiscard]] QList<quint64> clients() const;

    bool send(quint64 clientId, Envelope envelope);
    void broadcast(Envelope envelope);

  signals:
    void clientReady(quint64 clientId);
    void clientDisconnected(quint64 clientId);
    void envelopeReceived(quint64 clientId, const BreezeDesk::Ipc::Envelope& envelope);
    void protocolError(quint64 clientId, const BreezeDesk::Ipc::ProtocolError& error);

  private:
    struct ClientState;

    void acceptConnections();
    void readClient(quint64 clientId);
    void disconnectClient(quint64 clientId, const ProtocolError& error = {});
    bool processEnvelope(quint64 clientId, const Envelope& envelope);

    QLocalServer m_server;
    QHash<quint64, std::shared_ptr<ClientState>> m_clients;
    QByteArray m_sessionToken;
    quint64 m_nextClientId = 1;
    QTimer m_watchdog;
};

} // namespace BreezeDesk::Ipc
