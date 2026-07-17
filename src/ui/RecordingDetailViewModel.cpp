#include "breezedesk/ui/RecordingDetailViewModel.h"

namespace BreezeDesk {

RecordingDetailViewModel::RecordingDetailViewModel(QObject* parent) : QObject(parent) {}

QString RecordingDetailViewModel::id() const {
    return m_details.value("id").toString();
}
QString RecordingDetailViewModel::title() const {
    return m_details.value("title").toString();
}
QString RecordingDetailViewModel::sourcePath() const {
    return m_details.value("sourcePath").toString();
}
qint64 RecordingDetailViewModel::durationMs() const {
    return m_details.value("durationMs").toLongLong();
}
QString RecordingDetailViewModel::model() const {
    return m_details.value("model").toString();
}
QString RecordingDetailViewModel::status() const {
    return m_details.value("status").toString();
}
QString RecordingDetailViewModel::notes() const {
    return m_details.value("notes").toString();
}

void RecordingDetailViewModel::setDetails(const QVariantMap& details) {
    if (m_details == details) {
        return;
    }
    m_details = details;
    emit detailsChanged();
}

void RecordingDetailViewModel::clear() {
    setDetails({});
}

void RecordingDetailViewModel::setNotes(const QString& notes) {
    if (m_details.value("notes").toString() == notes) {
        return;
    }
    m_details.insert("notes", notes);
    emit detailsChanged();
    emit notesEdited(id(), notes);
}

} // namespace BreezeDesk
