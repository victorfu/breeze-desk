#include "breezedesk/models/ModelDownloadOperation.h"
#include "breezedesk/models/ModelManager.h"
#include "breezedesk/models/ModelManifest.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QScopeGuard>
#include <QSignalSpy>
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

class ModelsTest final : public QObject {
    Q_OBJECT
  private slots:
    void bundledManifestHasVerifiedMetadata();
    void resumesAndCommitsVerifiedDownload();
    void removesChecksumFailure();
    void restartsWhenServerIgnoresRange();
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
