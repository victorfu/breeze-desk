#include "breezedesk/models/ModelDownloadOperation.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStorageInfo>
#include <QTimer>
#include <QtConcurrentRun>

namespace BreezeDesk {

namespace {
constexpr qint64 DiskSafetyMargin = 256LL * 1024LL * 1024LL;
constexpr int MaximumRetries = 4;
constexpr int LockRetryIntervalMs = 100;

QByteArray sha256File(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(4 * 1024 * 1024));
    }
    return hash.result().toHex();
}
} // namespace

ModelDownloadOperation::ModelDownloadOperation(ModelManifestEntry entry, QString destinationDirectory,
                                               QNetworkAccessManager* network, QObject* parent)
    : QObject(parent), m_entry(std::move(entry)), m_destinationDirectory(std::move(destinationDirectory)),
      m_network(network) {
    QDir().mkpath(m_destinationDirectory);
    m_finalPath = QDir(m_destinationDirectory).filePath(m_entry.fileName);
    m_partPath = m_finalPath + QStringLiteral(".part");
    m_downloadLock = std::make_unique<QLockFile>(m_finalPath + QStringLiteral(".lock"));
    // A model download can legitimately take hours. Only QLockFile's process-liveness
    // checks may break an abandoned lock; elapsed wall-clock time must never do so.
    m_downloadLock->setStaleLockTime(0);
    m_lockRetryTimer = new QTimer(this);
    m_lockRetryTimer->setSingleShot(true);
    m_lockRetryTimer->setInterval(LockRetryIntervalMs);
    connect(m_lockRetryTimer, &QTimer::timeout, this, &ModelDownloadOperation::acquireDownloadLock);
    m_speedTimer = new QTimer(this);
    m_speedTimer->setInterval(1000);
    connect(m_speedTimer, &QTimer::timeout, this, [this] {
        m_bytesPerSecond = static_cast<double>(m_bytesReceived - m_lastSpeedBytes);
        m_lastSpeedBytes = m_bytesReceived;
        emit progressChanged();
    });
}

ModelDownloadOperation::~ModelDownloadOperation() {
    if (m_verificationWatcher != nullptr) {
        // The checksum reader accesses a file protected by m_downloadLock. Do
        // not release that lock until the reader has closed the file.
        m_verificationWatcher->waitForFinished();
    }
    if (m_reply != nullptr) {
        QObject::disconnect(m_reply.data(), nullptr, this, nullptr);
        m_reply->abort();
    }
    if (m_partFile != nullptr) {
        m_partFile->flush();
        m_partFile->close();
    }
}

QString ModelDownloadOperation::modelId() const {
    return m_entry.id;
}
ModelDownloadOperation::State ModelDownloadOperation::state() const {
    return m_state;
}
qint64 ModelDownloadOperation::bytesReceived() const {
    return m_bytesReceived;
}
qint64 ModelDownloadOperation::bytesTotal() const {
    return m_bytesTotal;
}
double ModelDownloadOperation::progress() const {
    return m_bytesTotal > 0 ? static_cast<double>(m_bytesReceived) / static_cast<double>(m_bytesTotal) : 0.0;
}
double ModelDownloadOperation::bytesPerSecond() const {
    return m_bytesPerSecond;
}
qint64 ModelDownloadOperation::estimatedRemainingSeconds() const {
    return m_bytesPerSecond > 0.0
               ? qRound64(static_cast<double>(m_bytesTotal - m_bytesReceived) / m_bytesPerSecond)
               : -1;
}
QString ModelDownloadOperation::error() const {
    return m_error;
}
QString ModelDownloadOperation::finalPath() const {
    return m_finalPath;
}

void ModelDownloadOperation::start() {
    if (m_state != State::Pending && m_state != State::Failed) {
        return;
    }
    m_cancelled = false;
    m_userPaused = false;
    m_finishedEmitted = false;
    m_retryCount = 0;
    m_error.clear();
    acquireDownloadLock();
}

void ModelDownloadOperation::pause() {
    if (m_state != State::Downloading &&
        !(m_state == State::Pending && m_lockRetryTimer->isActive())) {
        return;
    }
    m_userPaused = true;
    m_lockRetryTimer->stop();
    closeActiveRequest();
    m_speedTimer->stop();
    releaseDownloadLock();
    setState(State::Paused);
}

void ModelDownloadOperation::resume() {
    if (m_state != State::Paused && m_state != State::Failed) {
        return;
    }
    m_userPaused = false;
    m_cancelled = false;
    m_restartWithoutRange = false;
    m_finishedEmitted = false;
    m_error.clear();
    setState(State::Pending);
    acquireDownloadLock();
}

void ModelDownloadOperation::cancel() {
    if (m_state == State::Completed || m_state == State::Cancelled) {
        return;
    }
    m_cancelled = true;
    m_lockRetryTimer->stop();
    m_speedTimer->stop();
    setState(State::Cancelled);
    if (m_verificationInProgress) {
        // The background hash reader must settle before its protected files and
        // lock can be handed to another process.
        return;
    }
    closeActiveRequest();
    if (!m_downloadLock->isLocked()) {
        // A paused or retrying operation has already released its lock. Reacquire
        // it only for cleanup so cancellation can remove its abandoned partial,
        // while a lock waiter never touches the active owner's file.
        (void)m_downloadLock->tryLock(0);
    }
    if (m_downloadLock->isLocked()) {
        QFile::remove(m_partPath);
    }
    releaseDownloadLock();
    finish(false);
}

void ModelDownloadOperation::acquireDownloadLock() {
    if (m_cancelled || m_userPaused || m_downloadLock->isLocked()) {
        return;
    }
    if (!m_downloadLock->tryLock(0)) {
        if (m_downloadLock->error() != QLockFile::LockFailedError) {
            fail(QStringLiteral("The model download lock could not be acquired."), false);
            return;
        }
        setState(State::Pending);
        m_lockRetryTimer->start();
        return;
    }
    inspectLockedFiles();
}

void ModelDownloadOperation::inspectLockedFiles() {
    if (!m_downloadLock->isLocked()) {
        fail(QStringLiteral("The model download lock was lost."), false);
        return;
    }
    if (QFileInfo::exists(m_finalPath)) {
        beginVerification(m_finalPath, VerificationPurpose::ExistingFinal);
        return;
    }
    beginRequest();
}

void ModelDownloadOperation::beginRequest() {
    if (!m_downloadLock->isLocked()) {
        acquireDownloadLock();
        return;
    }
    if (m_network == nullptr) {
        fail(QStringLiteral("Network service is unavailable."), false);
        return;
    }
    qint64 existing = QFileInfo(m_partPath).size();
    if (existing > m_entry.fileSize) {
        if (!QFile::remove(m_partPath)) {
            fail(QStringLiteral("The partial model download is larger than the manifest."), false);
            return;
        }
        existing = 0;
    }
    if (existing == m_entry.fileSize && existing > 0) {
        m_bytesReceived = existing;
        m_bytesTotal = m_entry.fileSize;
        beginVerification(m_partPath, VerificationPurpose::DownloadedPart);
        return;
    }
    const QStorageInfo storage(m_destinationDirectory);
    if (storage.isValid() &&
        storage.bytesAvailable() < (m_entry.fileSize - existing) + DiskSafetyMargin) {
        fail(QStringLiteral("Insufficient disk space for model download."), false);
        return;
    }
    delete m_partFile;
    m_partFile = new QFile(m_partPath, this);
    if (!m_partFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
        fail(m_partFile->errorString(), false);
        return;
    }
    const qint64 offset = m_partFile->size();
    m_bytesReceived = offset;
    m_bytesTotal = m_entry.fileSize;
    m_lastSpeedBytes = offset;

    QNetworkRequest request(QUrl(m_entry.downloadUrl));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("Accept-Encoding", "identity");
    if (offset > 0) {
        request.setRawHeader("Range", QByteArrayLiteral("bytes=") + QByteArray::number(offset) +
                                          QByteArrayLiteral("-"));
    }
    m_reply = m_network->get(request);
    connect(m_reply.data(), &QNetworkReply::metaDataChanged, this, [this, offset] {
        const int status =
            m_reply == nullptr ? 0 : m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (offset > 0 && status == 200 && !m_restartWithoutRange) {
            // Some mirrors ignore Range and return the complete file. Abort before writing the
            // response over the partial download, then restart once from byte zero.
            m_restartWithoutRange = true;
            m_reply->abort();
        }
    });
    connect(m_reply.data(), &QNetworkReply::readyRead, this, &ModelDownloadOperation::handleReadyRead);
    connect(m_reply.data(), &QNetworkReply::downloadProgress, this, [this, offset](qint64 received, qint64 total) {
        m_bytesReceived = offset + received;
        if (total > 0) {
            m_bytesTotal = offset + total;
        }
        emit progressChanged();
    });
    connect(m_reply.data(), &QNetworkReply::finished, this, &ModelDownloadOperation::handleNetworkFinished);
    setState(State::Downloading);
    m_speedTimer->start();
}

void ModelDownloadOperation::handleReadyRead() {
    if (m_reply == nullptr || m_partFile == nullptr || m_restartWithoutRange) {
        return;
    }
    const QByteArray data = m_reply->readAll();
    if (m_partFile->write(data) != data.size()) {
        fail(QStringLiteral("Could not write the model download: %1").arg(m_partFile->errorString()), false);
    }
}

void ModelDownloadOperation::handleNetworkFinished() {
    if (m_reply == nullptr) {
        return;
    }
    QNetworkReply* finishedReply = m_reply.data();
    handleReadyRead();
    if (m_reply.data() != finishedReply) {
        return;
    }
    const QNetworkReply::NetworkError networkError = finishedReply->error();
    const int httpStatus = finishedReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString networkMessage = finishedReply->errorString();
    finishedReply->deleteLater();
    m_reply = nullptr;
    if (m_partFile != nullptr) {
        m_partFile->flush();
        m_partFile->close();
    }
    m_speedTimer->stop();
    if (m_cancelled || m_userPaused) {
        return;
    }
    if (m_restartWithoutRange) {
        m_restartWithoutRange = false;
        if (!QFile::remove(m_partPath)) {
            fail(QStringLiteral("The partial model download could not be restarted."), false);
            return;
        }
        QTimer::singleShot(0, this, &ModelDownloadOperation::beginRequest);
        return;
    }
    if (networkError != QNetworkReply::NoError) {
        fail(QStringLiteral("Model download failed: %1").arg(networkMessage), true);
        return;
    }
    if (httpStatus != 200 && httpStatus != 206) {
        fail(QStringLiteral("Model server returned HTTP %1.").arg(httpStatus), httpStatus >= 500);
        return;
    }
    if (QFileInfo(m_partPath).size() != m_entry.fileSize) {
        fail(QStringLiteral("Downloaded model size does not match the manifest."), true);
        return;
    }
    beginVerification(m_partPath, VerificationPurpose::DownloadedPart);
}

void ModelDownloadOperation::beginVerification(const QString& path,
                                               const VerificationPurpose purpose) {
    if (!m_downloadLock->isLocked()) {
        fail(QStringLiteral("The model download lock was lost before verification."), false);
        return;
    }
    setState(State::Verifying);
    m_verificationInProgress = true;
    auto* watcher = new QFutureWatcher<QByteArray>(this);
    m_verificationWatcher = watcher;
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher, purpose] {
        const QByteArray checksum = watcher->result();
        m_verificationWatcher = nullptr;
        watcher->deleteLater();
        m_verificationInProgress = false;
        if (m_cancelled) {
            if (m_downloadLock->isLocked()) {
                QFile::remove(m_partPath);
            }
            releaseDownloadLock();
            finish(false);
            return;
        }
        handleVerificationFinished(checksum, purpose);
    });
    watcher->setFuture(QtConcurrent::run(sha256File, path));
}

void ModelDownloadOperation::handleVerificationFinished(const QByteArray& checksum,
                                                        const VerificationPurpose purpose) {
    const bool valid = checksum == m_entry.sha256;
    if (purpose == VerificationPurpose::ExistingFinal) {
        if (valid) {
            // A different process may have completed while this operation waited.
            // Keep the verified final and discard only the now-obsolete partial.
            QFile::remove(m_partPath);
            completeSuccessfully();
            return;
        }
        if (QFileInfo::exists(m_finalPath) && !QFile::remove(m_finalPath)) {
            fail(QStringLiteral("The invalid model file could not be removed."), false);
            return;
        }
        beginRequest();
        return;
    }

    if (purpose == VerificationPurpose::DownloadedPart) {
        if (!valid) {
            QFile::remove(m_partPath);
            fail(QStringLiteral("Model checksum validation failed. The corrupt download was removed."),
                 false);
            return;
        }
        commitVerifiedPart();
        return;
    }

    if (valid) {
        QFile::remove(m_partPath);
        completeSuccessfully();
        return;
    }
    // The final was checksum-verified as invalid while holding the model lock;
    // only now is it safe to replace it with the already-verified partial.
    if (QFileInfo::exists(m_finalPath) && !QFile::remove(m_finalPath)) {
        fail(QStringLiteral("The invalid model file could not be replaced."), false);
        return;
    }
    if (!QFile::rename(m_partPath, m_finalPath)) {
        fail(QStringLiteral("The verified model could not be committed atomically."), false);
        return;
    }
    completeSuccessfully();
}

void ModelDownloadOperation::commitVerifiedPart() {
    if (QFileInfo::exists(m_finalPath)) {
        beginVerification(m_finalPath, VerificationPurpose::FinalBeforeCommit);
        return;
    }
    if (!QFile::rename(m_partPath, m_finalPath)) {
        // If an external writer published a final between the existence check
        // and rename, verify it instead of deleting it blindly.
        if (QFileInfo::exists(m_finalPath)) {
            beginVerification(m_finalPath, VerificationPurpose::FinalBeforeCommit);
            return;
        }
        fail(QStringLiteral("The verified model could not be committed atomically."), false);
        return;
    }
    completeSuccessfully();
}

void ModelDownloadOperation::completeSuccessfully() {
    releaseDownloadLock();
    setState(State::Completed);
    finish(true, m_finalPath);
}

void ModelDownloadOperation::finish(const bool success, const QString& path) {
    if (m_finishedEmitted) {
        return;
    }
    m_finishedEmitted = true;
    emit finished(success, path);
}

void ModelDownloadOperation::closeActiveRequest() {
    if (m_reply != nullptr) {
        QNetworkReply* reply = m_reply.data();
        m_reply = nullptr;
        QObject::disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    if (m_partFile != nullptr) {
        m_partFile->flush();
        m_partFile->close();
    }
}

void ModelDownloadOperation::releaseDownloadLock() {
    if (m_downloadLock->isLocked()) {
        m_downloadLock->unlock();
    }
}

void ModelDownloadOperation::setState(State state) {
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}

void ModelDownloadOperation::fail(const QString& message, bool retryable) {
    m_error = message;
    closeActiveRequest();
    m_speedTimer->stop();
    if (retryable && m_retryCount < MaximumRetries && !m_cancelled && !m_userPaused) {
        scheduleRetry();
        return;
    }
    releaseDownloadLock();
    setState(State::Failed);
    finish(false);
}

void ModelDownloadOperation::scheduleRetry() {
    const int delayMs = 1000 * (1 << m_retryCount);
    ++m_retryCount;
    releaseDownloadLock();
    setState(State::Pending);
    QTimer::singleShot(delayMs, this, [this] {
        if (!m_cancelled && !m_userPaused) {
            acquireDownloadLock();
        }
    });
}

} // namespace BreezeDesk
