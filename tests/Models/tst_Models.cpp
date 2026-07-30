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

class ModelsTest final : public QObject {
    Q_OBJECT
  private slots:
    void bundledManifestHasVerifiedMetadata();
    void resumesAndCommitsVerifiedDownload();
    void removesChecksumFailure();
    void restartsWhenServerIgnoresRange();
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
