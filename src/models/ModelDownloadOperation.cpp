#include "breezedesk/models/ModelDownloadOperation.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
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
    m_speedTimer = new QTimer(this);
    m_speedTimer->setInterval(1000);
    connect(m_speedTimer, &QTimer::timeout, this, [this] {
        m_bytesPerSecond = static_cast<double>(m_bytesReceived - m_lastSpeedBytes);
        m_lastSpeedBytes = m_bytesReceived;
        emit progressChanged();
    });
}

ModelDownloadOperation::~ModelDownloadOperation() {
    if (m_reply != nullptr) {
        m_reply->abort();
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
    m_error.clear();
    const qint64 existing = QFileInfo(m_partPath).size();
    const QStorageInfo storage(m_destinationDirectory);
    if (storage.isValid() && storage.bytesAvailable() < (m_entry.fileSize - existing) + DiskSafetyMargin) {
        fail(QStringLiteral("Insufficient disk space for model download."), false);
        return;
    }
    beginRequest();
}

void ModelDownloadOperation::pause() {
    if (m_state != State::Downloading) {
        return;
    }
    m_userPaused = true;
    if (m_reply != nullptr) {
        m_reply->abort();
    }
    m_speedTimer->stop();
    setState(State::Paused);
}

void ModelDownloadOperation::resume() {
    if (m_state != State::Paused && m_state != State::Failed) {
        return;
    }
    m_userPaused = false;
    m_cancelled = false;
    m_restartWithoutRange = false;
    beginRequest();
}

void ModelDownloadOperation::cancel() {
    if (m_state == State::Completed || m_state == State::Cancelled) {
        return;
    }
    m_cancelled = true;
    if (m_reply != nullptr) {
        m_reply->abort();
    }
    if (m_partFile != nullptr) {
        m_partFile->close();
    }
    QFile::remove(m_partPath);
    m_speedTimer->stop();
    setState(State::Cancelled);
    emit finished(false, {});
}

void ModelDownloadOperation::beginRequest() {
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
        beginVerification();
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
    connect(m_reply, &QNetworkReply::metaDataChanged, this, [this, offset] {
        const int status =
            m_reply == nullptr ? 0 : m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (offset > 0 && status == 200 && !m_restartWithoutRange) {
            // Some mirrors ignore Range and return the complete file. Abort before writing the
            // response over the partial download, then restart once from byte zero.
            m_restartWithoutRange = true;
            m_reply->abort();
        }
    });
    connect(m_reply, &QNetworkReply::readyRead, this, &ModelDownloadOperation::handleReadyRead);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this, offset](qint64 received, qint64 total) {
        m_bytesReceived = offset + received;
        if (total > 0) {
            m_bytesTotal = offset + total;
        }
        emit progressChanged();
    });
    connect(m_reply, &QNetworkReply::finished, this, &ModelDownloadOperation::handleNetworkFinished);
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
        m_reply->abort();
    }
}

void ModelDownloadOperation::handleNetworkFinished() {
    if (m_reply == nullptr) {
        return;
    }
    handleReadyRead();
    const QNetworkReply::NetworkError networkError = m_reply->error();
    const int httpStatus = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString networkMessage = m_reply->errorString();
    m_reply->deleteLater();
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
    beginVerification();
}

void ModelDownloadOperation::beginVerification() {
    setState(State::Verifying);
    auto* watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher] {
        const QByteArray checksum = watcher->result();
        watcher->deleteLater();
        if (m_cancelled) {
            return;
        }
        if (checksum != m_entry.sha256) {
            QFile::remove(m_partPath);
            fail(QStringLiteral("Model checksum validation failed. The corrupt download was removed."),
                 false);
            return;
        }
        if (QFile::exists(m_finalPath) && !QFile::remove(m_finalPath)) {
            fail(QStringLiteral("The previous model file could not be replaced."), false);
            return;
        }
        if (!QFile::rename(m_partPath, m_finalPath)) {
            fail(QStringLiteral("The verified model could not be committed atomically."), false);
            return;
        }
        setState(State::Completed);
        emit finished(true, m_finalPath);
    });
    watcher->setFuture(QtConcurrent::run(sha256File, m_partPath));
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
    if (retryable && m_retryCount < MaximumRetries && !m_cancelled && !m_userPaused) {
        scheduleRetry();
        return;
    }
    setState(State::Failed);
    emit finished(false, {});
}

void ModelDownloadOperation::scheduleRetry() {
    const int delayMs = 1000 * (1 << m_retryCount);
    ++m_retryCount;
    setState(State::Pending);
    QTimer::singleShot(delayMs, this, [this] {
        if (!m_cancelled && !m_userPaused) {
            beginRequest();
        }
    });
}

} // namespace BreezeDesk
