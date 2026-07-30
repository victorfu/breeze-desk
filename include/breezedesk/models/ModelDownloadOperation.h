#pragma once

#include "breezedesk/models/ModelManifest.h"

#include <QObject>
#include <QPointer>

#include <memory>

class QFile;
template <typename T> class QFutureWatcher;
class QLockFile;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace BreezeDesk {

class ModelDownloadOperation final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString modelId READ modelId CONSTANT)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(qint64 bytesReceived READ bytesReceived NOTIFY progressChanged)
    Q_PROPERTY(qint64 bytesTotal READ bytesTotal NOTIFY progressChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(double bytesPerSecond READ bytesPerSecond NOTIFY progressChanged)
    Q_PROPERTY(qint64 estimatedRemainingSeconds READ estimatedRemainingSeconds NOTIFY progressChanged)
    Q_PROPERTY(QString error READ error NOTIFY finished)

  public:
    enum class State { Pending, Downloading, Paused, Verifying, Completed, Cancelled, Failed };
    Q_ENUM(State)

    ModelDownloadOperation(ModelManifestEntry entry, QString destinationDirectory,
                           QNetworkAccessManager* network, QObject* parent = nullptr);
    ~ModelDownloadOperation() override;

    [[nodiscard]] QString modelId() const;
    [[nodiscard]] State state() const;
    [[nodiscard]] qint64 bytesReceived() const;
    [[nodiscard]] qint64 bytesTotal() const;
    [[nodiscard]] double progress() const;
    [[nodiscard]] double bytesPerSecond() const;
    [[nodiscard]] qint64 estimatedRemainingSeconds() const;
    [[nodiscard]] QString error() const;
    [[nodiscard]] QString finalPath() const;

  public slots:
    void start();
    void pause();
    void resume();
    void cancel();

  signals:
    void stateChanged();
    void progressChanged();
    void finished(bool success, const QString& path);

  private:
    enum class VerificationPurpose { ExistingFinal, DownloadedPart, FinalBeforeCommit };

    void acquireDownloadLock();
    void inspectLockedFiles();
    void beginRequest();
    void handleReadyRead();
    void handleNetworkFinished();
    void beginVerification(const QString& path, VerificationPurpose purpose);
    void handleVerificationFinished(const QByteArray& checksum, VerificationPurpose purpose);
    void commitVerifiedPart();
    void completeSuccessfully();
    void finish(bool success, const QString& path = {});
    void closeActiveRequest();
    void releaseDownloadLock();
    void setState(State state);
    void fail(const QString& message, bool retryable);
    void scheduleRetry();

    ModelManifestEntry m_entry;
    QString m_destinationDirectory;
    QString m_partPath;
    QString m_finalPath;
    std::unique_ptr<QLockFile> m_downloadLock;
    QNetworkAccessManager* m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    QFile* m_partFile = nullptr;
    QTimer* m_speedTimer = nullptr;
    QTimer* m_lockRetryTimer = nullptr;
    QFutureWatcher<QByteArray>* m_verificationWatcher = nullptr;
    State m_state = State::Pending;
    qint64 m_bytesReceived = 0;
    qint64 m_bytesTotal = 0;
    qint64 m_lastSpeedBytes = 0;
    double m_bytesPerSecond = 0.0;
    int m_retryCount = 0;
    QString m_error;
    bool m_userPaused = false;
    bool m_cancelled = false;
    bool m_restartWithoutRange = false;
    bool m_verificationInProgress = false;
    bool m_finishedEmitted = false;
};

} // namespace BreezeDesk
