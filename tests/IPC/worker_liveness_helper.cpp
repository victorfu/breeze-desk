#include <breezedesk/ipc/WorkerServer.h>

#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QTimer>
#include <QtCore/QtEndian>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>

#include <memory>
#include <utility>

using namespace BreezeDesk::Ipc;

namespace {

int recordLaunch(const QString& path) {
    if (path.isEmpty()) {
        return 1;
    }
    QFile state(path);
    int launch = 0;
    if (state.open(QIODevice::ReadOnly)) {
        launch = QString::fromUtf8(state.readAll()).trimmed().toInt();
        state.close();
    }
    if (state.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        state.write(QByteArray::number(launch + 1));
        state.close();
    }
    return launch + 1;
}

int runMalformedServer(QCoreApplication& application, const QString& serverName,
                       const QByteArray& sessionToken) {
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(serverName)) {
        return 2;
    }
    QObject::connect(&server, &QLocalServer::newConnection, &application,
                     [&application, &server, sessionToken] {
                         while (server.hasPendingConnections()) {
                             QLocalSocket* socket = server.nextPendingConnection();
                             if (socket == nullptr) {
                                 continue;
                             }
                             auto decoder = std::make_shared<FrameDecoder>();
                             auto authenticated = std::make_shared<bool>(false);
                             QObject::connect(
                                 socket, &QLocalSocket::readyRead, &application,
                                 [&application, socket, decoder, authenticated, sessionToken] {
                                     const FrameParseResult parsed = decoder->append(socket->readAll());
                                     if (parsed.error.isError()) {
                                         application.exit(3);
                                         return;
                                     }
                                     for (const Envelope& request : parsed.envelopes) {
                                         if (!*authenticated) {
                                             if (request.type != MessageType::Hello ||
                                                 request.sessionToken != sessionToken) {
                                                 application.exit(4);
                                                 return;
                                             }
                                             Envelope ack;
                                             ack.type = MessageType::HelloAck;
                                             ack.requestId = request.requestId;
                                             ack.sessionToken = sessionToken;
                                             ProtocolError error;
                                             const QByteArray frame = FrameCodec::encode(ack, &error);
                                             if (frame.isEmpty()) {
                                                 application.exit(5);
                                                 return;
                                             }
                                             *authenticated = true;
                                             socket->write(frame);
                                             (void)socket->flush();
                                             continue;
                                         }
                                         if (request.type == MessageType::Shutdown) {
                                             application.quit();
                                             return;
                                         }
                                         if (request.type == MessageType::GetCapabilities &&
                                             request.payload.value(QStringLiteral("triggerMalformed"))
                                                 .toBool()) {
                                             QByteArray invalidPrefix(kFramePrefixSize,
                                                                      Qt::Uninitialized);
                                             qToBigEndian(kMaximumMessageSize + 1U,
                                                          reinterpret_cast<uchar*>(
                                                              invalidPrefix.data()));
                                             socket->write(invalidPrefix);
                                             (void)socket->flush();
                                         }
                                     }
                                 });
                             QObject::connect(socket, &QLocalSocket::disconnected, socket,
                                              &QObject::deleteLater);
                         }
                     });
    return application.exec();
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addOption({QStringLiteral("server"), QStringLiteral("Local server name"),
                      QStringLiteral("name")});
    parser.addOption({QStringLiteral("session-token"), QStringLiteral("Session token"),
                      QStringLiteral("token")});
    parser.addOption({QStringLiteral("worker-version"), QStringLiteral("Worker version"),
                      QStringLiteral("version")});
    parser.process(application);

    const QString serverName = parser.value(QStringLiteral("server"));
    const QByteArray sessionToken =
        QByteArray::fromBase64(parser.value(QStringLiteral("session-token")).toLatin1(),
                               QByteArray::Base64UrlEncoding);
    const int launch =
        recordLaunch(qEnvironmentVariable("BREEZEDESK_TEST_WORKER_LIVENESS_STATE"));
    const QString mode = qEnvironmentVariable("BREEZEDESK_TEST_WORKER_LIVENESS_MODE");
    const int listenDelayMs =
        qEnvironmentVariableIntValue("BREEZEDESK_TEST_WORKER_LIVENESS_LISTEN_DELAY_MS");

    if (launch == 1 && mode == QStringLiteral("malformed")) {
        return runMalformedServer(application, serverName, sessionToken);
    }

    WorkerServer server;
    QObject::connect(&server, &WorkerServer::envelopeReceived, &application,
                     [&application, &server, launch, mode](const quint64 clientId,
                                                          const Envelope& request) {
                         if (request.type == MessageType::Shutdown) {
                             application.quit();
                             return;
                         }
                         if (launch == 1 && mode == QStringLiteral("peer-close") &&
                             request.payload.value(QStringLiteral("triggerPeerClose")).toBool()) {
                             // Close the authenticated channel but deliberately keep
                             // this process alive so the manager must terminate it.
                             server.close();
                             return;
                         }
                         Envelope response;
                         response.requestId = request.requestId;
                         response.jobId = request.jobId;
                         if (request.type == MessageType::GetCapabilities) {
                             response.type = MessageType::Capabilities;
                             response.payload.insert(QStringLiteral("runtimeAvailable"), true);
                         } else {
                             response.type = MessageType::Error;
                             response.payload.insert(QStringLiteral("message"),
                                                     QStringLiteral("Unsupported test request"));
                         }
                         (void)server.send(clientId, std::move(response));
                     });
    const auto listen = [&application, &server, serverName, sessionToken] {
        if (!server.listen(serverName, sessionToken)) {
            application.exit(2);
        }
    };
    if (launch == 1 && listenDelayMs > 0) {
        QTimer::singleShot(listenDelayMs, &server, listen);
    } else {
        listen();
    }
    return application.exec();
}
