#pragma once

#include <breezedesk/ipc/ApplicationCommand.h>
#include <breezedesk/ipc/FrameCodec.h>

#include <QtCore/QHash>
#include <QtCore/QLockFile>
#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtNetwork/QLocalServer>

#include <functional>
#include <memory>

QT_FORWARD_DECLARE_CLASS(QLocalSocket)

namespace BreezeDesk::Ipc {

class SingleInstanceGuard final : public QObject {
    Q_OBJECT

  public:
    using CommandHandler = std::function<ApplicationCommandReply(const QStringList&)>;

    enum class AcquireResult {
        Primary,
        Forwarded,
        Error,
    };
    Q_ENUM(AcquireResult)

    explicit SingleInstanceGuard(QString applicationId, QObject* parent = nullptr);
    ~SingleInstanceGuard() override;

    [[nodiscard]] AcquireResult acquire(const QStringList& filePaths = {}, int forwardingTimeoutMs = 2'000);
    [[nodiscard]] bool isPrimary() const noexcept;
    [[nodiscard]] QString errorString() const;
    [[nodiscard]] QString endpointName() const;
    void setCommandHandler(CommandHandler handler);

  signals:
    void activationRequested(const QStringList& filePaths);

  private:
    bool startPrimary();
    bool forwardToPrimary(const QStringList& filePaths, int timeoutMs);
    void acceptConnections();
    void readSocket(QLocalSocket* socket);

    QString m_applicationId;
    QString m_endpointName;
    std::unique_ptr<QLockFile> m_lockFile;
    QLocalServer m_server;
    QHash<QLocalSocket*, std::shared_ptr<FrameDecoder>> m_decoders;
    QString m_error;
    CommandHandler m_commandHandler;
    bool m_primary = false;
};

} // namespace BreezeDesk::Ipc
