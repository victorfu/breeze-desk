#include "breezedesk/ui/DiagnosticsViewModel.h"

#include "breezedesk/core/StoragePaths.h"

#include <QCoreApplication>
#include <QSysInfo>
#include <QtGlobal>

namespace BreezeDesk {

DiagnosticsViewModel::DiagnosticsViewModel(QObject* parent) : QObject(parent) {}

QString DiagnosticsViewModel::appVersion() const {
    return QCoreApplication::applicationVersion();
}
QString DiagnosticsViewModel::qtVersion() const {
    return QString::fromLatin1(qVersion());
}
QString DiagnosticsViewModel::osDescription() const {
    return QSysInfo::prettyProductName();
}
QString DiagnosticsViewModel::cpuArchitecture() const {
    return QSysInfo::currentCpuArchitecture();
}
QString DiagnosticsViewModel::databasePath() const {
    return StoragePaths::database();
}
QString DiagnosticsViewModel::modelPath() const {
    return StoragePaths::models();
}
QString DiagnosticsViewModel::cachePath() const {
    return StoragePaths::cache();
}
QString DiagnosticsViewModel::logPath() const {
    return StoragePaths::logs();
}
QString DiagnosticsViewModel::ffmpegVersion() const {
    return m_ffmpegVersion;
}
QString DiagnosticsViewModel::whisperVersion() const {
    return m_whisperVersion;
}
QString DiagnosticsViewModel::workerProtocolVersion() const {
    return QStringLiteral("1");
}
QString DiagnosticsViewModel::selectedBackend() const {
    return m_selectedBackend;
}
QString DiagnosticsViewModel::actualBackend() const {
    return m_actualBackend;
}

void DiagnosticsViewModel::refresh() {
    emit refreshRequested();
}
void DiagnosticsViewModel::exportDiagnostics(bool includePaths) {
    emit exportRequested(includePaths);
}

void DiagnosticsViewModel::setFfmpegVersion(const QString& value) {
    if (m_ffmpegVersion != value) {
        m_ffmpegVersion = value;
        emit runtimeChanged();
    }
}

void DiagnosticsViewModel::setWhisperVersion(const QString& value) {
    if (m_whisperVersion != value) {
        m_whisperVersion = value;
        emit runtimeChanged();
    }
}

void DiagnosticsViewModel::setSelectedBackend(const QString& value) {
    if (m_selectedBackend != value) {
        m_selectedBackend = value;
        emit runtimeChanged();
    }
}

void DiagnosticsViewModel::setActualBackend(const QString& value) {
    if (m_actualBackend != value) {
        m_actualBackend = value;
        emit runtimeChanged();
    }
}

} // namespace BreezeDesk
