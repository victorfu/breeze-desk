#pragma once

#include "breezedesk/core/Result.h"
#include "breezedesk/transcript/TranscriptSegment.h"

#include <QByteArray>
#include <QJsonObject>

namespace BreezeDesk {

enum class TranscriptExportFormat { Txt, Markdown, Srt, Vtt, Json, Csv };

struct TranscriptExportMetadata {
    QString recordingId;
    QString title;
    qint64 durationMs = 0;
    QString modelId;
    QString modelChecksum;
    QString engineVersion;
    QString workerVersion;
    QString language;
    QString preset;
    QJsonObject additional;
};

struct TranscriptExportOptions {
    bool includeTimecodes = false;
    bool utf8Bom = false;
    int subtitleMaximumLineLength = 42;
};

class TranscriptExporter final {
  public:
    [[nodiscard]] static Result<QByteArray> render(TranscriptExportFormat format,
                                                   const TranscriptExportMetadata& metadata,
                                                   const QList<TranscriptSegment>& segments,
                                                   const TranscriptExportOptions& options = {});
    [[nodiscard]] static Result<void> writeFile(const QString& filePath, TranscriptExportFormat format,
                                                const TranscriptExportMetadata& metadata,
                                                const QList<TranscriptSegment>& segments,
                                                const TranscriptExportOptions& options = {});
};

} // namespace BreezeDesk
