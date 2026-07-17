#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QJsonArray>

namespace BreezeDesk {

class TranscriptSegmentModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        StartMsRole,
        EndMsRole,
        OriginalTextRole,
        EditedTextRole,
        LowConfidenceRole,
        EditedRole,
        GlossaryReplacementRole,
        GlossaryAuditRole,
        ReviewedRole
    };
    Q_ENUM(Role)

    struct Segment {
        QString id;
        QString recordingId;
        QString jobId;
        QString chunkId;
        int ordinal{0};
        qint64 startMs{0};
        qint64 endMs{0};
        QString originalText;
        QString editedText;
        double averageProbability{0.0};
        double minimumProbability{0.0};
        double noSpeechProbability{0.0};
        bool lowConfidence{false};
        bool reviewed{false};
        QJsonArray replacementAudit;
        bool provisional{false};
        int attempt{1};
        QDateTime createdAt;
        QDateTime updatedAt;
    };

    explicit TranscriptSegmentModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QList<Segment> snapshot() const;
    void restoreSnapshot(const QList<Segment>& segments);
    void replaceAll(const QList<Segment>& segments);

    bool editText(int row, const QString& text);
    bool split(int row, qint64 positionMs);
    bool mergePrevious(int row);
    bool mergeNext(int row);
    bool insertAfter(int row, qint64 startMs, qint64 endMs, const QString& text);
    bool removeSegment(int row);
    bool setTimes(int row, qint64 startMs, qint64 endMs);
    bool setReviewed(int row, bool reviewed);
    bool setGlossaryReplacementApplied(int row, int replacementIndex, bool applied);
    [[nodiscard]] QVariantMap segmentAt(int row) const;

  private:
    [[nodiscard]] bool validTimesForRow(int row, qint64 startMs, qint64 endMs) const;
    void normalizeOrdinals();
    void emitRowChanged(int row);

    QList<Segment> m_segments;
};

} // namespace BreezeDesk
