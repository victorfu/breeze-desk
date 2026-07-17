#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QString>

namespace BreezeDesk {

struct TranscriptSegment {
    QString id;
    QString recordingId;
    QString jobId;
    QString chunkId;
    int ordinal = 0;
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString originalText;
    QString editedText;
    double averageProbability = 0.0;
    double minimumProbability = 0.0;
    double noSpeechProbability = 0.0;
    bool lowConfidence = false;
    bool reviewed = false;
    QJsonArray replacementAudit;
    bool provisional = false;
    int attempt = 1;
    QDateTime createdAt;
    QDateTime updatedAt;

    [[nodiscard]] QString displayText() const { return editedText.isEmpty() ? originalText : editedText; }
    [[nodiscard]] bool isEdited() const { return !editedText.isEmpty() && editedText != originalText; }
};

} // namespace BreezeDesk
