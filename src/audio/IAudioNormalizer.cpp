#include "breezedesk/audio/IAudioNormalizer.h"

#include <QtGlobal>

namespace BreezeDesk {

NormalizationOperation::NormalizationOperation(QObject* parent) : QObject(parent) {}

double NormalizationOperation::progress() const {
    return m_progress;
}
bool NormalizationOperation::isRunning() const {
    return m_running;
}
QString NormalizationOperation::error() const {
    return m_error;
}

void NormalizationOperation::setProgress(double value) {
    const double bounded = qBound(0.0, value, 1.0);
    if (qFuzzyCompare(m_progress, bounded)) {
        return;
    }
    m_progress = bounded;
    emit progressChanged();
}

void NormalizationOperation::setRunning(bool value) {
    if (m_running == value) {
        return;
    }
    m_running = value;
    emit runningChanged();
}

void NormalizationOperation::setError(const QString& value) {
    m_error = value;
}

} // namespace BreezeDesk
