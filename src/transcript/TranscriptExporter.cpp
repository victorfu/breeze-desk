#include "breezedesk/transcript/TranscriptExporter.h"

#include "breezedesk/core/TextUtils.h"
#include "breezedesk/core/TimeUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace BreezeDesk {
namespace {
QString subtitleTime(const qint64 value, const bool comma) {
    return TimeUtils::formatClock(value, comma ? QLatin1Char(',') : QLatin1Char('.'));
}
QString subtitleText(const QString& input, const int maximum) {
    const QString text = input.simplified();
    if (maximum <= 0 || text.size() <= maximum)
        return text;
    QStringList lines;
    QString remaining = text;
    while (remaining.size() > maximum) {
        qsizetype split = remaining.lastIndexOf(QLatin1Char(' '), maximum);
        if (split < maximum / 2)
            split = maximum;
        lines.append(remaining.left(split).trimmed());
        remaining = remaining.mid(split).trimmed();
    }
    if (!remaining.isEmpty())
        lines.append(remaining);
    return lines.join(QLatin1Char('\n'));
}
QList<TranscriptSegment> orderedValidSegments(QList<TranscriptSegment> segments) {
    std::sort(segments.begin(), segments.end(), [](const auto& left, const auto& right) {
        if (left.startMs != right.startMs)
            return left.startMs < right.startMs;
        return left.ordinal < right.ordinal;
    });
    qint64 previousEnd = 0;
    for (TranscriptSegment& segment : segments) {
        segment.startMs = qMax(previousEnd, qMax<qint64>(0, segment.startMs));
        segment.endMs = qMax(segment.startMs + 1, segment.endMs);
        previousEnd = segment.endMs;
    }
    return segments;
}
QByteArray withOptionalBom(QString text, const bool bom) {
    QByteArray bytes = TextUtils::normalizedLineEndings(text).toUtf8();
    if (bom)
        bytes.prepend("\xEF\xBB\xBF", 3);
    return bytes;
}
} // namespace

Result<QByteArray> TranscriptExporter::render(const TranscriptExportFormat format,
                                              const TranscriptExportMetadata& metadata,
                                              const QList<TranscriptSegment>& input,
                                              const TranscriptExportOptions& options) {
    const QList<TranscriptSegment> segments = orderedValidSegments(input);
    QString output;
    if (format == TranscriptExportFormat::Txt) {
        for (const auto& segment : segments) {
            if (options.includeTimecodes)
                output += QLatin1Char('[') + subtitleTime(segment.startMs, false) + QStringLiteral("] ");
            output += segment.displayText().trimmed() + QLatin1Char('\n');
        }
    } else if (format == TranscriptExportFormat::Markdown) {
        output = QStringLiteral("---\ntitle: \"") + metadata.title + QStringLiteral("\"\nrecordingId: ") +
                 metadata.recordingId + QStringLiteral("\nmodel: ") + metadata.modelId +
                 QStringLiteral("\nlanguage: ") + metadata.language + QStringLiteral("\n---\n\n");
        for (const auto& segment : segments)
            output += QStringLiteral("**[") + subtitleTime(segment.startMs, false) + QStringLiteral("]** ") +
                      segment.displayText().trimmed() + QStringLiteral("\n\n");
    } else if (format == TranscriptExportFormat::Srt || format == TranscriptExportFormat::Vtt) {
        if (format == TranscriptExportFormat::Vtt)
            output = QStringLiteral("WEBVTT\n\n");
        for (int i = 0; i < segments.size(); ++i) {
            if (format == TranscriptExportFormat::Srt)
                output += QString::number(i + 1) + QLatin1Char('\n');
            output += subtitleTime(segments.at(i).startMs, format == TranscriptExportFormat::Srt) +
                      QStringLiteral(" --> ") +
                      subtitleTime(segments.at(i).endMs, format == TranscriptExportFormat::Srt) +
                      QLatin1Char('\n') +
                      subtitleText(segments.at(i).displayText(), options.subtitleMaximumLineLength) +
                      QStringLiteral("\n\n");
        }
    } else if (format == TranscriptExportFormat::Json) {
        QJsonArray jsonSegments;
        for (const auto& segment : segments) {
            jsonSegments.append(
                QJsonObject{{QStringLiteral("id"), segment.id},
                            {QStringLiteral("startMs"), segment.startMs},
                            {QStringLiteral("endMs"), segment.endMs},
                            {QStringLiteral("originalText"), segment.originalText},
                            {QStringLiteral("editedText"), segment.editedText},
                            {QStringLiteral("averageProbability"), segment.averageProbability},
                            {QStringLiteral("minimumProbability"), segment.minimumProbability},
                            {QStringLiteral("noSpeechProbability"), segment.noSpeechProbability},
                            {QStringLiteral("lowConfidence"), segment.lowConfidence},
                            {QStringLiteral("reviewed"), segment.reviewed},
                            {QStringLiteral("glossaryAudit"), segment.replacementAudit}});
        }
        QJsonObject recording{{QStringLiteral("id"), metadata.recordingId},
                              {QStringLiteral("title"), metadata.title},
                              {QStringLiteral("durationMs"), metadata.durationMs}};
        QJsonObject engine{{QStringLiteral("modelId"), metadata.modelId},
                           {QStringLiteral("modelChecksum"), metadata.modelChecksum},
                           {QStringLiteral("engineVersion"), metadata.engineVersion},
                           {QStringLiteral("workerVersion"), metadata.workerVersion},
                           {QStringLiteral("language"), metadata.language},
                           {QStringLiteral("preset"), metadata.preset}};
        QByteArray json = QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                                    {QStringLiteral("recording"), recording},
                                                    {QStringLiteral("engine"), engine},
                                                    {QStringLiteral("metadata"), metadata.additional},
                                                    {QStringLiteral("segments"), jsonSegments}})
                              .toJson(QJsonDocument::Indented);
        if (options.utf8Bom)
            json.prepend("\xEF\xBB\xBF", 3);
        return Result<QByteArray>::success(json);
    } else if (format == TranscriptExportFormat::Csv) {
        output = QStringLiteral("start,end,original_text,edited_text,confidence\n");
        for (const auto& segment : segments)
            output += QStringList{QString::number(segment.startMs), QString::number(segment.endMs),
                                  TextUtils::csvField(segment.originalText),
                                  TextUtils::csvField(segment.editedText),
                                  QString::number(segment.averageProbability, 'f', 6)}
                          .join(QLatin1Char(',')) +
                      QLatin1Char('\n');
    }
    return Result<QByteArray>::success(withOptionalBom(output, options.utf8Bom));
}

Result<void> TranscriptExporter::writeFile(const QString& filePath, const TranscriptExportFormat format,
                                           const TranscriptExportMetadata& metadata,
                                           const QList<TranscriptSegment>& segments,
                                           const TranscriptExportOptions& options) {
    if (filePath.isEmpty())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("An export file path is required.")));
    auto bytes = render(format, metadata, segments, options);
    if (!bytes)
        return Result<void>::failure(bytes.error());
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return Result<void>::failure({ErrorDomain::Export,
                                      ErrorCode::ExportFailed,
                                      QStringLiteral("Export failed"),
                                      QStringLiteral("The export file could not be opened."),
                                      QStringLiteral("Choose another folder and try again."),
                                      file.errorString(),
                                      true,
                                      {}});
    if (file.write(bytes.value()) != bytes.value().size() || !file.commit())
        return Result<void>::failure(
            {ErrorDomain::Export,
             ErrorCode::ExportFailed,
             QStringLiteral("Export failed"),
             QStringLiteral("The transcript could not be written completely."),
             QStringLiteral("Check the available disk space and folder permissions."),
             file.errorString(),
             true,
             {}});
    return Result<void>::success();
}

} // namespace BreezeDesk
