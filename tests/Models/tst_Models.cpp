#include "breezedesk/models/ModelDownloadOperation.h"
#include "breezedesk/models/ModelManager.h"
#include "breezedesk/models/ModelManifest.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace BreezeDesk;

class HttpFixture final : public QTcpServer {
    Q_OBJECT
  public:
    explicit HttpFixture(QByteArray payload, bool honorRanges = true, QObject* parent = nullptr)
        : QTcpServer(parent), m_payload(std::move(payload)), m_honorRanges(honorRanges) {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                QTcpSocket* socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const QByteArray request = socket->readAll();
                    qint64 offset = 0;
                    const qsizetype rangePosition = request.indexOf("Range: bytes=");
                    if (rangePosition >= 0) {
                        const qsizetype numberStart = rangePosition + 13;
                        offset = request.mid(numberStart).split('-').first().toLongLong();
                    }
                    if (!m_honorRanges) {
                        offset = 0;
                    }
                    const QByteArray body = m_payload.mid(offset);
                    QByteArray response = offset > 0 ? QByteArrayLiteral("HTTP/1.1 206 Partial Content\r\n")
                                                     : QByteArrayLiteral("HTTP/1.1 200 OK\r\n");
                    if (offset > 0) {
                        response += QByteArrayLiteral("Content-Range: bytes ") +
                                    QByteArray::number(offset) + QByteArrayLiteral("-") +
                                    QByteArray::number(m_payload.size() - 1) + QByteArrayLiteral("/") +
                                    QByteArray::number(m_payload.size()) + QByteArrayLiteral("\r\n");
                    }
                    response += QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size()) +
                                QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
                    socket->write(response);
                    socket->disconnectFromHost();
                });
            }
        });
    }

  private:
    QByteArray m_payload;
    bool m_honorRanges{true};
};

class ControlledHttpFixture final : public QTcpServer {
    Q_OBJECT
  public:
    explicit ControlledHttpFixture(QByteArray payload, QObject* parent = nullptr)
        : QTcpServer(parent), m_payload(std::move(payload)) {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                QTcpSocket* socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    if (socket->property("requestParsed").toBool()) {
                        return;
                    }
                    QByteArray request = socket->property("requestBuffer").toByteArray();
                    request += socket->readAll();
                    if (!request.contains("\r\n\r\n")) {
                        socket->setProperty("requestBuffer", request);
                        return;
                    }
                    socket->setProperty("requestParsed", true);
                    qint64 offset = 0;
                    const qsizetype rangePosition = request.indexOf("Range: bytes=");
                    if (rangePosition >= 0) {
                        const qsizetype numberStart = rangePosition + 13;
                        offset = request.mid(numberStart).split('-').first().toLongLong();
                    }
                    ++m_requestCount;
                    if (m_activeSocket == nullptr) {
                        m_activeSocket = socket;
                        m_activeOffset = offset;
                    }
                    emit requestReceived();
                });
            }
        });
    }

    [[nodiscard]] int requestCount() const noexcept { return m_requestCount; }

    void sendPrefix(const qsizetype byteCount) {
        QVERIFY(m_activeSocket != nullptr);
        QVERIFY(!m_responseStarted);
        const QByteArray body = m_payload.mid(m_activeOffset);
        const QByteArray prefix = body.first(byteCount);
        QByteArray response = m_activeOffset > 0
                                  ? QByteArrayLiteral("HTTP/1.1 206 Partial Content\r\n")
                                  : QByteArrayLiteral("HTTP/1.1 200 OK\r\n");
        if (m_activeOffset > 0) {
            response += QByteArrayLiteral("Content-Range: bytes ") +
                        QByteArray::number(m_activeOffset) + QByteArrayLiteral("-") +
                        QByteArray::number(m_payload.size() - 1) + QByteArrayLiteral("/") +
                        QByteArray::number(m_payload.size()) + QByteArrayLiteral("\r\n");
        }
        response += QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size()) +
                    QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + prefix;
        QCOMPARE(m_activeSocket->write(response), static_cast<qint64>(response.size()));
        m_activeSocket->flush();
        m_responseStarted = true;
        m_responseBytesSent = prefix.size();
    }

    void finishResponse() {
        QVERIFY(m_activeSocket != nullptr);
        const QByteArray body = m_payload.mid(m_activeOffset);
        if (!m_responseStarted) {
            sendPrefix(body.size());
        } else {
            const QByteArray remainder = body.mid(m_responseBytesSent);
            QCOMPARE(m_activeSocket->write(remainder), static_cast<qint64>(remainder.size()));
            m_activeSocket->flush();
            m_responseBytesSent = body.size();
        }
        m_activeSocket->disconnectFromHost();
    }

  signals:
    void requestReceived();

  private:
    QByteArray m_payload;
    QPointer<QTcpSocket> m_activeSocket;
    qint64 m_activeOffset{0};
    qsizetype m_responseBytesSent{0};
    int m_requestCount{0};
    bool m_responseStarted{false};
};

struct ScriptedHttpResponse {
    QByteArray statusLine{QByteArrayLiteral("HTTP/1.1 200 OK")};
    QList<QPair<QByteArray, QByteArray>> headers;
    QList<QByteArray> bodyChunks;
    bool chunked{false};
    bool closeWithoutResponse{false};
};

class ScriptedHttpFixture final : public QTcpServer {
    Q_OBJECT
  public:
    explicit ScriptedHttpFixture(QList<ScriptedHttpResponse> responses, QObject* parent = nullptr)
        : QTcpServer(parent), m_responses(std::move(responses)) {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                QTcpSocket* socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    if (socket->property("requestParsed").toBool()) {
                        return;
                    }
                    QByteArray request = socket->property("requestBuffer").toByteArray();
                    request += socket->readAll();
                    if (!request.contains("\r\n\r\n")) {
                        socket->setProperty("requestBuffer", request);
                        return;
                    }
                    socket->setProperty("requestParsed", true);
                    qint64 offset = 0;
                    const qsizetype rangePosition = request.indexOf("Range: bytes=");
                    if (rangePosition >= 0) {
                        const qsizetype numberStart = rangePosition + 13;
                        offset = request.mid(numberStart).split('-').first().toLongLong();
                    }
                    m_requestOffsets.append(offset);
                    const int responseIndex = qMin(m_requestOffsets.size() - 1, m_responses.size() - 1);
                    sendResponse(socket, m_responses.at(responseIndex));
                });
            }
        });
    }

    [[nodiscard]] int requestCount() const noexcept { return m_requestOffsets.size(); }
    [[nodiscard]] QList<qint64> requestOffsets() const { return m_requestOffsets; }

  private:
    static void sendResponse(QTcpSocket* socket, const ScriptedHttpResponse& scripted) {
        if (scripted.closeWithoutResponse) {
            socket->disconnectFromHost();
            return;
        }
        QByteArray response = scripted.statusLine + QByteArrayLiteral("\r\n");
        for (const auto& header : scripted.headers) {
            response += header.first + QByteArrayLiteral(": ") + header.second +
                        QByteArrayLiteral("\r\n");
        }
        if (scripted.chunked) {
            response += QByteArrayLiteral("Transfer-Encoding: chunked\r\n");
        }
        response += QByteArrayLiteral("Connection: close\r\n\r\n");
        if (!scripted.chunked) {
            for (const QByteArray& chunk : scripted.bodyChunks) {
                response += chunk;
            }
            socket->write(response);
            socket->disconnectFromHost();
            return;
        }

        socket->write(response);
        QPointer<QTcpSocket> guardedSocket(socket);
        for (qsizetype index = 0; index < scripted.bodyChunks.size(); ++index) {
            const QByteArray chunk = scripted.bodyChunks.at(index);
            QTimer::singleShot(static_cast<int>(index * 25), socket, [guardedSocket, chunk] {
                if (guardedSocket != nullptr) {
                    guardedSocket->write(QByteArray::number(chunk.size(), 16) +
                                         QByteArrayLiteral("\r\n") + chunk +
                                         QByteArrayLiteral("\r\n"));
                    guardedSocket->flush();
                }
            });
        }
        QTimer::singleShot(static_cast<int>(scripted.bodyChunks.size() * 25), socket,
                           [guardedSocket] {
                               if (guardedSocket != nullptr) {
                                   guardedSocket->write(QByteArrayLiteral("0\r\n\r\n"));
                                   guardedSocket->disconnectFromHost();
                               }
                           });
    }

    QList<ScriptedHttpResponse> m_responses;
    QList<qint64> m_requestOffsets;
};

class ObservingPartFile final : public QFile {
  public:
    ObservingPartFile(const QString& path, QObject* parent, qint64* maximumSize,
                      QByteArray* writtenBytes, bool failWrites = false)
        : QFile(path, parent), m_maximumSize(maximumSize), m_writtenBytes(writtenBytes),
          m_failWrites(failWrites) {}

  protected:
    qint64 writeData(const char* data, const qint64 length) override {
        if (m_failWrites) {
            return -1;
        }
        const qint64 written = QFile::writeData(data, length);
        if (written > 0) {
            if (m_writtenBytes != nullptr) {
                m_writtenBytes->append(data, written);
            }
            if (m_maximumSize != nullptr) {
                *m_maximumSize = qMax(*m_maximumSize, size());
            }
        }
        return written;
    }

  private:
    qint64* m_maximumSize = nullptr;
    QByteArray* m_writtenBytes = nullptr;
    bool m_failWrites = false;
};

class ModelsTest final : public QObject {
    Q_OBJECT
  private slots:
    void bundledManifestHasVerifiedMetadata();
    void resumesAndCommitsVerifiedDownload();
    void removesChecksumFailure();
    void restartsWhenServerIgnoresRange();
    void rejectsOversizedDeclaredResponse();
    void capsChunkedOversizedResponse();
    void rejectsHttpErrorBodyWithoutWriting();
    void retriesServerErrorWithoutContamination();
    void retriesTransportFailureWithoutDiscardingPrefix();
    void rejectsInvalidResumeMetadata_data();
    void rejectsInvalidResumeMetadata();
    void removesPartialAfterWriteFailure();
    void serializesConcurrentOperationsAcrossNetworkManagers();
    void cancellingLockWaiterPreservesOwnerPartial();
    void cancellingPausedOwnerRemovesPartial();
    void destroysActiveOperationAfterNetworkSiblingSafely();
    void managerDeduplicatesConcurrentDownloads();
    void customModelSurvivesManagerRestart();
};

void ModelsTest::bundledManifestHasVerifiedMetadata() {
    QString error;
    const ModelManifest manifest = ModelManifest::loadBundled(&error);
    QVERIFY2(manifest.isValid(&error), qPrintable(error));
    QCOMPARE(manifest.entries().size(), 3);
    const ModelManifestEntry* q5 = manifest.find(QStringLiteral("breeze-asr-25-q5"));
    QVERIFY(q5 != nullptr);
    QCOMPARE(q5->fileSize, 1080732108LL);
    QCOMPARE(q5->sha256,
             QByteArrayLiteral("8efbf0ce8a3f50fe332b7617da787fb81354b358c288b008d3bdef8359df64c6"));
    const ModelManifestEntry* vad = manifest.find(QStringLiteral("silero-vad-v6.2.0"));
    QVERIFY(vad != nullptr);
    QCOMPARE(vad->fileName, QStringLiteral("ggml-silero-v6.2.0.bin"));
    QCOMPARE(vad->fileSize, 885'098LL);
    QCOMPARE(vad->sha256,
             QByteArrayLiteral("2aa269b785eeb53a82983a20501ddf7c1d9c48e33ab63a41391ac6c9f7fb6987"));
    QVERIFY(vad->downloadUrl.contains(QStringLiteral("/9ffd54a1e1ee413ddf265af9913beaf518d1639b/")));
}

void ModelsTest::resumesAndCommitsVerifiedDownload() {
    const QByteArray payload("native-model-fixture");
    HttpFixture server(payload);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir temporary;
    ModelManifestEntry entry;
    entry.id = QStringLiteral("fixture");
    entry.fileName = QStringLiteral("fixture.bin");
    entry.fileSize = payload.size();
    entry.sha256 = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    entry.downloadUrl = QStringLiteral("http://127.0.0.1:%1/model").arg(server.serverPort());
    QFile part(temporary.filePath(QStringLiteral("fixture.bin.part")));
    QVERIFY(part.open(QIODevice::WriteOnly));
    QCOMPARE(part.write(payload.first(6)), 6);
    part.close();
    QNetworkAccessManager network;
    ModelDownloadOperation operation(entry, temporary.path(), &network);
    QSignalSpy finished(&operation, &ModelDownloadOperation::finished);
    operation.start();
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.first().at(0).toBool(), true);
    QFile result(temporary.filePath(QStringLiteral("fixture.bin")));
    QVERIFY(result.open(QIODevice::ReadOnly));
    QCOMPARE(result.readAll(), payload);
    QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("fixture.bin.part"))));
}

void ModelsTest::removesChecksumFailure() {
    const QByteArray payload("corrupt-fixture");
    HttpFixture server(payload);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir temporary;
    ModelManifestEntry entry;
    entry.id = QStringLiteral("bad");
    entry.fileName = QStringLiteral("bad.bin");
    entry.fileSize = payload.size();
    entry.sha256 = QByteArray(64, '0');
    entry.downloadUrl = QStringLiteral("http://127.0.0.1:%1/model").arg(server.serverPort());
    QNetworkAccessManager network;
    ModelDownloadOperation operation(entry, temporary.path(), &network);
    QSignalSpy finished(&operation, &ModelDownloadOperation::finished);
    operation.start();
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.first().at(0).toBool(), false);
    QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("bad.bin"))));
    QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("bad.bin.part"))));
}

void ModelsTest::restartsWhenServerIgnoresRange() {
    const QByteArray payload("server-does-not-support-byte-ranges");
    HttpFixture server(payload, false);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir temporary;
    const QString partPath = temporary.filePath(QStringLiteral("no-range.bin.part"));
    QFile part(partPath);
    QVERIFY(part.open(QIODevice::WriteOnly));
    QCOMPARE(part.write(payload.first(7)), 7);
    part.close();

    ModelManifestEntry entry;
    entry.id = QStringLiteral("no-range");
    entry.fileName = QStringLiteral("no-range.bin");
    entry.fileSize = payload.size();
    entry.sha256 = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    entry.downloadUrl = QStringLiteral("http://127.0.0.1:%1/model").arg(server.serverPort());
    QNetworkAccessManager network;
    ModelDownloadOperation operation(entry, temporary.path(), &network);
    QSignalSpy finished(&operation, &ModelDownloadOperation::finished);
    operation.start();
    QVERIFY(finished.wait(5'000));
    QCOMPARE(finished.constFirst().constFirst().toBool(), true);
    QFile result(temporary.filePath(QStringLiteral("no-range.bin")));
    QVERIFY(result.open(QIODevice::ReadOnly));
    QCOMPARE(result.readAll(), payload);
}

void ModelsTest::rejectsOversizedDeclaredResponse() {
    const QByteArray payload("declared-size-bound");
    ScriptedHttpResponse response;
    response.headers.append(
        {QByteArrayLiteral("Content-Length"), QByteArray::number(payload.size() + 1)});
    response.bodyChunks.append(payload + QByteArrayLiteral("!"));
    ScriptedHttpFixture server({response});
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    ModelManifestEntry entry;
    entry.id = QStringLiteral("declared-oversize");
    entry.fileName = QStringLiteral("declared-oversize.bin");
    entry.fileSize = payload.size();
    entry.sha256 = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    entry.downloadUrl = QStringLiteral("http://127.0.0.1:%1/model").arg(server.serverPort());

    qint64 maximumSize = 0;
    QByteArray writtenBytes;
    QNetworkAccessManager network;
    ModelDownloadOperation operation(
        entry, temporary.path(), &network, nullptr,
        [&maximumSize, &writtenBytes](const QString& path, QObject* parent) {
            return new ObservingPartFile(path, parent, &maximumSize, &writtenBytes);
        });
    QSignalSpy finished(&operation, &ModelDownloadOperation::finished);
    operation.start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2'000);
    QCOMPARE(finished.constFirst().constFirst().toBool(), false);
    QCOMPARE(server.requestCount(), 1);
    QCOMPARE(maximumSize, 0);
    QVERIFY(writtenBytes.isEmpty());
    QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("declared-oversize.bin.part"))));
}

void ModelsTest::capsChunkedOversizedResponse() {
    const QByteArray payload("chunked-stream-bound");
    ScriptedHttpResponse response;
    response.chunked = true;
    response.bodyChunks = {payload, QByteArrayLiteral("!")};
    ScriptedHttpFixture server({response});
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    ModelManifestEntry entry;
    entry.id = QStringLiteral("chunked-oversize");
    entry.fileName = QStringLiteral("chunked-oversize.bin");
    entry.fileSize = payload.size();
    entry.sha256 = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    entry.downloadUrl = QStringLiteral("http://127.0.0.1:%1/model").arg(server.serverPort());

    qint64 maximumSize = 0;
    QByteArray writtenBytes;
    QNetworkAccessManager network;
    ModelDownloadOperation operation(
        entry, temporary.path(), &network, nullptr,
        [&maximumSize, &writtenBytes](const QString& path, QObject* parent) {
            return new ObservingPartFile(path, parent, &maximumSize, &writtenBytes);
        });
    QSignalSpy finished(&operation, &ModelDownloadOperation::finished);
    operation.start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2'000);
    QCOMPARE(finished.constFirst().constFirst().toBool(), false);
    QCOMPARE(operation.bytesTotal(), entry.fileSize);
    QCOMPARE(maximumSize, entry.fileSize);
    QCOMPARE(writtenBytes, payload);
    QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("chunked-oversize.bin.part"))));
}

void ModelsTest::rejectsHttpErrorBodyWithoutWriting() {
    const QByteArray errorBody(128 * 1024, 'e');
    ScriptedHttpResponse response;
    response.statusLine = QByteArrayLiteral("HTTP/1.1 404 Not Found");
    response.headers.append(
        {QByteArrayLiteral("Content-Length"), QByteArray::number(errorBody.size())});
    response.bodyChunks.append(errorBody);
    ScriptedHttpFixture server({response});
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    ModelManifestEntry entry;
    entry.id = QStringLiteral("http-error-body");
    entry.fileName = QStringLiteral("http-error-body.bin");
    entry.fileSize = 32;
    entry.sha256 = QByteArray(64, '0');
    entry.downloadUrl = QStringLiteral("http://127.0.0.1:%1/model").arg(server.serverPort());

    qint64 maximumSize = 0;
    QByteArray writtenBytes;
    QNetworkAccessManager network;
    ModelDownloadOperation operation(
        entry, temporary.path(), &network, nullptr,
        [&maximumSize, &writtenBytes](const QString& path, QObject* parent) {
            return new ObservingPartFile(path, parent, &maximumSize, &writtenBytes);
        });
    QSignalSpy finished(&operation, &ModelDownloadOperation::finished);
    operation.start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2'000);
    QCOMPARE(finished.constFirst().constFirst().toBool(), false);
    QCOMPARE(server.requestCount(), 1);
    QCOMPARE(maximumSize, 0);
    QVERIFY(writtenBytes.isEmpty());
    QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("http-error-body.bin.part"))));
}

void ModelsTest::retriesServerErrorWithoutContamination() {
    const QByteArray payload("clean-retry-payload");
    const QByteArray errorBody("server-error-must-not-be-written");
    constexpr qint64 PrefixSize = 5;
    ScriptedHttpResponse first;
    first.statusLine = QByteArrayLiteral("HTTP/1.1 500 Server Error");
    first.headers.append({QByteArrayLiteral("Content-Length"), QByteArray::number(errorBody.size())});
    first.bodyChunks.append(errorBody);
    ScriptedHttpResponse second;
    second.statusLine = QByteArrayLiteral("HTTP/1.1 206 Partial Content");
    second.headers.append(
        {QByteArrayLiteral("Content-Range"),
         QByteArrayLiteral("bytes ") + QByteArray::number(PrefixSize) + QByteArrayLiteral("-") +
             QByteArray::number(payload.size() - 1) + QByteArrayLiteral("/") +
             QByteArray::number(payload.size())});
    second.headers.append(
        {QByteArrayLiteral("Content-Length"), QByteArray::number(payload.size() - PrefixSize)});
    second.bodyChunks.append(payload.mid(PrefixSize));
    ScriptedHttpFixture server({first, second});
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString partPath = temporary.filePath(QStringLiteral("clean-server-retry.bin.part"));
    QFile part(partPath);
    QVERIFY(part.open(QIODevice::WriteOnly));
    QCOMPARE(part.write(payload.first(PrefixSize)), PrefixSize);
    part.close();

    ModelManifestEntry entry;
    entry.id = QStringLiteral("clean-server-retry");
    entry.fileName = QStringLiteral("clean-server-retry.bin");
    entry.fileSize = payload.size();
    entry.sha256 = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    entry.downloadUrl = QStringLiteral("http://127.0.0.1:%1/model").arg(server.serverPort());

    qint64 maximumSize = 0;
    QByteArray writtenBytes;
    QNetworkAccessManager network;
    ModelDownloadOperation operation(
        entry, temporary.path(), &network, nullptr,
        [&maximumSize, &writtenBytes](const QString& path, QObject* parent) {
            return new ObservingPartFile(path, parent, &maximumSize, &writtenBytes);
        });
    QSignalSpy finished(&operation, &ModelDownloadOperation::finished);
    operation.start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5'000);
    QCOMPARE(finished.constFirst().constFirst().toBool(), true);
    QCOMPARE(server.requestCount(), 2);
    QCOMPARE(server.requestOffsets(), QList<qint64>({PrefixSize, PrefixSize}));
    QCOMPARE(writtenBytes, payload.mid(PrefixSize));
    QCOMPARE(maximumSize, entry.fileSize);
    QFile finalFile(temporary.filePath(QStringLiteral("clean-server-retry.bin")));
    QVERIFY(finalFile.open(QIODevice::ReadOnly));
    QCOMPARE(finalFile.readAll(), payload);
}

void ModelsTest::retriesTransportFailureWithoutDiscardingPrefix() {
    const QByteArray payload("transport-retry-payload");
    constexpr qint64 PrefixSize = 6;
    ScriptedHttpResponse first;
    first.closeWithoutResponse = true;
    ScriptedHttpResponse second;
    second.statusLine = QByteArrayLiteral("HTTP/1.1 206 Partial Content");
    second.headers.append(
        {QByteArrayLiteral("Content-Range"),
         QByteArrayLiteral("bytes ") + QByteArray::number(PrefixSize) + QByteArrayLiteral("-") +
             QByteArray::number(payload.size() - 1) + QByteArrayLiteral("/") +
             QByteArray::number(payload.size())});
    second.headers.append(
        {QByteArrayLiteral("Content-Length"), QByteArray::number(payload.size() - PrefixSize)});
    second.bodyChunks.append(payload.mid(PrefixSize));
    ScriptedHttpFixture server({first, second});
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString partPath = temporary.filePath(QStringLiteral("transport-retry.bin.part"));
    QFile part(partPath);
    QVERIFY(part.open(QIODevice::WriteOnly));
    QCOMPARE(part.write(payload.first(PrefixSize)), PrefixSize);
    part.close();

    ModelManifestEntry entry;
    entry.id = QStringLiteral("transport-retry");
    entry.fileName = QStringLiteral("transport-retry.bin");
    entry.fileSize = payload.size();
    entry.sha256 = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    entry.downloadUrl = QStringLiteral("http://127.0.0.1:%1/model").arg(server.serverPort());

    QByteArray writtenBytes;
    QNetworkAccessManager network;
    ModelDownloadOperation operation(
        entry, temporary.path(), &network, nullptr,
        [&writtenBytes](const QString& path, QObject* parent) {
            return new ObservingPartFile(path, parent, nullptr, &writtenBytes);
        });
    QSignalSpy finished(&operation, &ModelDownloadOperation::finished);
    operation.start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5'000);
    QCOMPARE(finished.constFirst().constFirst().toBool(), true);
    QCOMPARE(server.requestCount(), 2);
    QCOMPARE(server.requestOffsets(), QList<qint64>({PrefixSize, PrefixSize}));
    QCOMPARE(writtenBytes, payload.mid(PrefixSize));
    QFile finalFile(temporary.filePath(QStringLiteral("transport-retry.bin")));
    QVERIFY(finalFile.open(QIODevice::ReadOnly));
    QCOMPARE(finalFile.readAll(), payload);
}

void ModelsTest::rejectsInvalidResumeMetadata_data() {
    const QByteArray payload("resume-metadata");
    constexpr qint64 PrefixSize = 4;
    const qint64 remaining = payload.size() - PrefixSize;
    const QByteArray correctLength = QByteArray::number(remaining);
    const QByteArray last = QByteArray::number(payload.size() - 1);
    const QByteArray total = QByteArray::number(payload.size());

    QTest::addColumn<QByteArray>("contentRange");
    QTest::addColumn<QByteArray>("contentLength");
    QTest::newRow("missing-content-range") << QByteArray() << correctLength;
    QTest::newRow("malformed-content-range") << QByteArrayLiteral("bytes nope") << correctLength;
    QTest::newRow("wrong-range-start")
        << QByteArrayLiteral("bytes 3-") + last + QByteArrayLiteral("/") + total << correctLength;
    QTest::newRow("wrong-range-end")
        << QByteArrayLiteral("bytes 4-") + QByteArray::number(payload.size() - 2) +
               QByteArrayLiteral("/") + total
        << correctLength;
    QTest::newRow("wrong-range-total")
        << QByteArrayLiteral("bytes 4-") + last + QByteArrayLiteral("/") +
               QByteArray::number(payload.size() + 1)
        << correctLength;
    QTest::newRow("wrong-content-length")
        << QByteArrayLiteral("bytes 4-") + last + QByteArrayLiteral("/") + total
        << QByteArray::number(remaining + 1);
    QTest::newRow("malformed-content-length")
        << QByteArrayLiteral("bytes 4-") + last + QByteArrayLiteral("/") + total
        << QByteArrayLiteral("invalid");
}

void ModelsTest::rejectsInvalidResumeMetadata() {
    QFETCH(QByteArray, contentRange);
    QFETCH(QByteArray, contentLength);
    const QByteArray payload("resume-metadata");
    constexpr qint64 PrefixSize = 4;
    ScriptedHttpResponse response;
    response.statusLine = QByteArrayLiteral("HTTP/1.1 206 Partial Content");
    if (!contentRange.isNull()) {
        response.headers.append({QByteArrayLiteral("Content-Range"), contentRange});
    }
    if (!contentLength.isNull()) {
        response.headers.append({QByteArrayLiteral("Content-Length"), contentLength});
    }
    response.bodyChunks.append(payload.mid(PrefixSize));
    ScriptedHttpFixture server({response});
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString partPath = temporary.filePath(QStringLiteral("invalid-resume.bin.part"));
    QFile part(partPath);
    QVERIFY(part.open(QIODevice::WriteOnly));
    QCOMPARE(part.write(payload.first(PrefixSize)), PrefixSize);
    part.close();

    ModelManifestEntry entry;
    entry.id = QStringLiteral("invalid-resume");
    entry.fileName = QStringLiteral("invalid-resume.bin");
    entry.fileSize = payload.size();
    entry.sha256 = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    entry.downloadUrl = QStringLiteral("http://127.0.0.1:%1/model").arg(server.serverPort());
    QNetworkAccessManager network;
    ModelDownloadOperation operation(entry, temporary.path(), &network);
    QSignalSpy finished(&operation, &ModelDownloadOperation::finished);
    operation.start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2'000);
    QCOMPARE(finished.constFirst().constFirst().toBool(), false);
    QCOMPARE(server.requestCount(), 1);
    QCOMPARE(server.requestOffsets(), QList<qint64>({PrefixSize}));
    QVERIFY(!QFileInfo::exists(partPath));
}

void ModelsTest::removesPartialAfterWriteFailure() {
    const QByteArray payload("write-failure-payload");
    ScriptedHttpResponse response;
    response.headers.append({QByteArrayLiteral("Content-Length"), QByteArray::number(payload.size())});
    response.bodyChunks.append(payload);
    ScriptedHttpFixture server({response});
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    ModelManifestEntry entry;
    entry.id = QStringLiteral("write-failure");
    entry.fileName = QStringLiteral("write-failure.bin");
    entry.fileSize = payload.size();
    entry.sha256 = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    entry.downloadUrl = QStringLiteral("http://127.0.0.1:%1/model").arg(server.serverPort());
    QNetworkAccessManager network;
    ModelDownloadOperation operation(
        entry, temporary.path(), &network, nullptr,
        [](const QString& path, QObject* parent) {
            return new ObservingPartFile(path, parent, nullptr, nullptr, true);
        });
    QSignalSpy finished(&operation, &ModelDownloadOperation::finished);
    operation.start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2'000);
    QCOMPARE(finished.constFirst().constFirst().toBool(), false);
    QCOMPARE(server.requestCount(), 1);
    QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("write-failure.bin.part"))));
}

void ModelsTest::serializesConcurrentOperationsAcrossNetworkManagers() {
    const QByteArray payload("one-writer-model-download");
    ControlledHttpFixture server(payload);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    ModelManifestEntry entry;
    entry.id = QStringLiteral("shared-model");
    entry.fileName = QStringLiteral("shared-model.bin");
    entry.fileSize = payload.size();
    entry.sha256 = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    entry.downloadUrl = QStringLiteral("http://127.0.0.1:%1/model").arg(server.serverPort());

    QNetworkAccessManager firstNetwork;
    QNetworkAccessManager secondNetwork;
    ModelDownloadOperation first(entry, temporary.path(), &firstNetwork);
    ModelDownloadOperation second(entry, temporary.path(), &secondNetwork);
    QSignalSpy firstFinished(&first, &ModelDownloadOperation::finished);
    QSignalSpy secondFinished(&second, &ModelDownloadOperation::finished);

    first.start();
    QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 1, 2'000);
    QVERIFY(QFileInfo::exists(temporary.filePath(QStringLiteral("shared-model.bin.lock"))));
    second.start();

    // The controlled first response stays open for several lock-poll cycles.
    // A second HTTP request here would prove that the shared .part has two writers.
    QTest::qWait(350);
    QCOMPARE(server.requestCount(), 1);
    QCOMPARE(second.state(), ModelDownloadOperation::State::Pending);

    server.finishResponse();
    QTRY_COMPARE_WITH_TIMEOUT(firstFinished.count(), 1, 5'000);
    QTRY_COMPARE_WITH_TIMEOUT(secondFinished.count(), 1, 5'000);
    QCOMPARE(firstFinished.constFirst().constFirst().toBool(), true);
    QCOMPARE(secondFinished.constFirst().constFirst().toBool(), true);
    QCOMPARE(server.requestCount(), 1);

    QFile finalFile(temporary.filePath(QStringLiteral("shared-model.bin")));
    QVERIFY(finalFile.open(QIODevice::ReadOnly));
    const QByteArray finalBytes = finalFile.readAll();
    QCOMPARE(finalBytes, payload);
    QCOMPARE(QCryptographicHash::hash(finalBytes, QCryptographicHash::Sha256).toHex(), entry.sha256);
    QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("shared-model.bin.part"))));
    QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("shared-model.bin.lock"))));
}

void ModelsTest::cancellingLockWaiterPreservesOwnerPartial() {
    const QByteArray payload("owner-part-must-survive-waiter-cancellation");
    constexpr qsizetype PrefixSize = 13;
    ControlledHttpFixture server(payload);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    ModelManifestEntry entry;
    entry.id = QStringLiteral("cancelled-waiter");
    entry.fileName = QStringLiteral("cancelled-waiter.bin");
    entry.fileSize = payload.size();
    entry.sha256 = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    entry.downloadUrl = QStringLiteral("http://127.0.0.1:%1/model").arg(server.serverPort());

    QNetworkAccessManager ownerNetwork;
    QNetworkAccessManager waiterNetwork;
    ModelDownloadOperation owner(entry, temporary.path(), &ownerNetwork);
    ModelDownloadOperation waiter(entry, temporary.path(), &waiterNetwork);
    QSignalSpy ownerFinished(&owner, &ModelDownloadOperation::finished);
    QSignalSpy waiterFinished(&waiter, &ModelDownloadOperation::finished);

    const QString partPath = temporary.filePath(QStringLiteral("cancelled-waiter.bin.part"));
    QFile existingPart(partPath);
    QVERIFY(existingPart.open(QIODevice::WriteOnly));
    QCOMPARE(existingPart.write(payload.first(PrefixSize)), static_cast<qint64>(PrefixSize));
    existingPart.close();

    owner.start();
    QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 1, 2'000);
    QVERIFY(QFileInfo::exists(temporary.filePath(QStringLiteral("cancelled-waiter.bin.lock"))));

    waiter.start();
    QTest::qWait(250);
    QCOMPARE(server.requestCount(), 1);
    QCOMPARE(waiter.state(), ModelDownloadOperation::State::Pending);
    waiter.cancel();
    QCOMPARE(waiterFinished.count(), 1);
    QCOMPARE(waiterFinished.constFirst().constFirst().toBool(), false);

    QFile ownerPart(partPath);
    QVERIFY(ownerPart.open(QIODevice::ReadOnly));
    QCOMPARE(ownerPart.readAll(), payload.first(PrefixSize));
    ownerPart.close();
    QVERIFY(QFileInfo::exists(temporary.filePath(QStringLiteral("cancelled-waiter.bin.lock"))));

    server.finishResponse();
    QTRY_COMPARE_WITH_TIMEOUT(ownerFinished.count(), 1, 5'000);
    QCOMPARE(ownerFinished.constFirst().constFirst().toBool(), true);
    QFile finalFile(temporary.filePath(QStringLiteral("cancelled-waiter.bin")));
    QVERIFY(finalFile.open(QIODevice::ReadOnly));
    QCOMPARE(finalFile.readAll(), payload);
}

void ModelsTest::cancellingPausedOwnerRemovesPartial() {
    const QByteArray payload("paused-owner-part-must-be-removed");
    constexpr qsizetype PrefixSize = 12;
    ControlledHttpFixture server(payload);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    ModelManifestEntry entry;
    entry.id = QStringLiteral("cancelled-paused-owner");
    entry.fileName = QStringLiteral("cancelled-paused-owner.bin");
    entry.fileSize = payload.size();
    entry.sha256 = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    entry.downloadUrl = QStringLiteral("http://127.0.0.1:%1/model").arg(server.serverPort());

    QNetworkAccessManager network;
    ModelDownloadOperation operation(entry, temporary.path(), &network);
    QSignalSpy finished(&operation, &ModelDownloadOperation::finished);

    const QString partPath = temporary.filePath(QStringLiteral("cancelled-paused-owner.bin.part"));
    QFile existingPart(partPath);
    QVERIFY(existingPart.open(QIODevice::WriteOnly));
    QCOMPARE(existingPart.write(payload.first(PrefixSize)), static_cast<qint64>(PrefixSize));
    existingPart.close();

    operation.start();
    QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 1, 2'000);

    operation.pause();
    QCOMPARE(operation.state(), ModelDownloadOperation::State::Paused);
    QCOMPARE(QFileInfo(partPath).size(), static_cast<qint64>(PrefixSize));
    QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("cancelled-paused-owner.bin.lock"))));

    operation.cancel();
    QCOMPARE(operation.state(), ModelDownloadOperation::State::Cancelled);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.constFirst().constFirst().toBool(), false);
    QVERIFY(!QFileInfo::exists(partPath));
    QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("cancelled-paused-owner.bin.lock"))));
}

void ModelsTest::destroysActiveOperationAfterNetworkSiblingSafely() {
    const QByteArray payload("active-operation-parent-destruction");
    constexpr qsizetype PrefixSize = 11;
    ControlledHttpFixture server(payload);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    ModelManifestEntry entry;
    entry.id = QStringLiteral("parent-destruction");
    entry.fileName = QStringLiteral("parent-destruction.bin");
    entry.fileSize = payload.size();
    entry.sha256 = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    entry.downloadUrl = QStringLiteral("http://127.0.0.1:%1/model").arg(server.serverPort());

    auto* owner = new QObject;
    auto* network = new QNetworkAccessManager(owner);
    auto* operation = new ModelDownloadOperation(entry, temporary.path(), network, owner);
    QPointer<QNetworkAccessManager> guardedNetwork(network);
    QPointer<ModelDownloadOperation> guardedOperation(operation);
    QStringList destructionOrder;
    connect(network, &QObject::destroyed, [&destructionOrder] {
        destructionOrder.append(QStringLiteral("network"));
    });
    connect(operation, &QObject::destroyed, [&destructionOrder] {
        destructionOrder.append(QStringLiteral("operation"));
    });

    const QString partPath = temporary.filePath(QStringLiteral("parent-destruction.bin.part"));
    QFile existingPart(partPath);
    QVERIFY(existingPart.open(QIODevice::WriteOnly));
    QCOMPARE(existingPart.write(payload.first(PrefixSize)), static_cast<qint64>(PrefixSize));
    existingPart.close();

    const QObjectList siblings = owner->children();
    QCOMPARE(siblings.size(), 2);
    QCOMPARE(siblings.at(0), static_cast<QObject*>(network));
    QCOMPARE(siblings.at(1), static_cast<QObject*>(operation));

    operation->start();
    QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 1, 2'000);
    QVERIFY(QFileInfo::exists(temporary.filePath(QStringLiteral("parent-destruction.bin.lock"))));

    delete owner;

    QVERIFY(guardedNetwork.isNull());
    QVERIFY(guardedOperation.isNull());
    QCOMPARE(destructionOrder,
             QStringList({QStringLiteral("network"), QStringLiteral("operation")}));
    QCOMPARE(QFileInfo(partPath).size(), static_cast<qint64>(PrefixSize));
    QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("parent-destruction.bin.lock"))));
}

void ModelsTest::managerDeduplicatesConcurrentDownloads() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray previousRoot = qgetenv("BREEZEDESK_DATA_ROOT");
    const auto restoreRoot = qScopeGuard([previousRoot] {
        if (previousRoot.isNull()) {
            qunsetenv("BREEZEDESK_DATA_ROOT");
        } else {
            qputenv("BREEZEDESK_DATA_ROOT", previousRoot);
        }
    });
    qputenv("BREEZEDESK_DATA_ROOT", temporary.path().toUtf8());

    ModelManager manager;
    ModelDownloadOperation* first = manager.download(QStringLiteral("silero-vad-v6.2.0"));
    QVERIFY(first != nullptr);
    QCOMPARE(manager.download(QStringLiteral("silero-vad-v6.2.0")), first);
    QSignalSpy finished(first, &ModelDownloadOperation::finished);
    first->cancel();
    QCOMPARE(finished.count(), 1);
}

void ModelsTest::customModelSurvivesManagerRestart() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray previousRoot = qgetenv("BREEZEDESK_DATA_ROOT");
    const auto restoreRoot = qScopeGuard([previousRoot] {
        if (previousRoot.isNull()) {
            qunsetenv("BREEZEDESK_DATA_ROOT");
        } else {
            qputenv("BREEZEDESK_DATA_ROOT", previousRoot);
        }
    });
    qputenv("BREEZEDESK_DATA_ROOT", temporary.path().toUtf8());

    const QString sourcePath = temporary.filePath(QStringLiteral("自訂 model.bin"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(QByteArray(2'048, '\x01')), 2'048);
    source.close();

    QString modelId;
    {
        ModelManager manager;
        QString error;
        QVERIFY2(manager.importCustomModel(sourcePath, QStringLiteral("專案: Model"), &modelId, &error),
                 qPrintable(error));
        QVERIFY(modelId.startsWith(QStringLiteral("custom-")));
        QCOMPARE(manager.customModels().size(), 1);
        const QByteArray expected =
            QCryptographicHash::hash(QByteArray(2'048, '\x01'), QCryptographicHash::Sha256).toHex();
        QCOMPARE(manager.expectedSha256(modelId), expected);
        QVERIFY(QFileInfo(manager.modelPath(modelId) + QStringLiteral(".sha256")).isFile());
    }

    {
        ModelManager manager;
        const QList<CustomModelInfo> custom = manager.customModels();
        QCOMPARE(custom.size(), 1);
        QCOMPARE(custom.constFirst().id, modelId);
        QCOMPARE(custom.constFirst().displayName, QStringLiteral("專案_ Model"));
        QCOMPARE(custom.constFirst().sha256, manager.expectedSha256(modelId));
        QVERIFY(manager.isInstalled(modelId));
        QString verificationError;
        QVERIFY2(manager.verify(modelId, &verificationError), qPrintable(verificationError));
        QFile tampered(manager.modelPath(modelId));
        QVERIFY(tampered.open(QIODevice::Append));
        QCOMPARE(tampered.write("x", 1), 1);
        tampered.close();
        QVERIFY(!manager.verify(modelId, &verificationError));
        QVERIFY(verificationError.contains(QStringLiteral("checksum"), Qt::CaseInsensitive));
        manager.setDefaultModelId(modelId);
        QCOMPARE(manager.defaultModelId(), modelId);
        const QString installedPath = manager.modelPath(modelId);
        QString error;
        QVERIFY2(manager.removeModel(modelId, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(installedPath + QStringLiteral(".sha256")));
    }
}

QTEST_MAIN(ModelsTest)
#include "tst_Models.moc"
