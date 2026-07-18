#pragma once

#include <QAbstractListModel>

#include <optional>

#include "breezedesk/jobs/TranscriptionJob.h"

namespace BreezeDesk {

class TranscriptRevisionModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
        JobIdRole = Qt::UserRole + 1,
        RevisionNumberRole,
        StateRole,
        StageRole,
        ProgressRole,
        ModelIdRole,
        BackendRole,
        LanguageRole,
        PresetRole,
        CreatedAtRole,
        CompletedAtRole,
        SegmentCountRole,
        HasManualEditsRole,
        HasProvisionalSegmentsRole,
        LatestTextRole,
        QueueHiddenRole,
        ActiveRole,
        SelectedRole,
        RunningRole,
        CanDeleteRole,
        ErrorCodeRole,
        ErrorMessageRole,
        DisplayLabelRole,
    };
    Q_ENUM(Role)

    explicit TranscriptRevisionModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void replaceRevisions(QList<TranscriptRevisionSummary> revisions, const QString& selectedJobId,
                          const QString& activeJobId);
    void setSelectedJobId(const QString& jobId);
    void setActiveJobId(const QString& jobId);

    [[nodiscard]] bool contains(const QString& jobId) const;
    [[nodiscard]] int indexOf(const QString& jobId) const;
    [[nodiscard]] QString newestJobId() const;
    [[nodiscard]] bool hasRevisionNewerThan(const QString& jobId) const;
    [[nodiscard]] std::optional<TranscriptRevisionSummary> revision(const QString& jobId) const;
    [[nodiscard]] QVariantMap revisionMap(const QString& jobId) const;

  private:
    [[nodiscard]] QVariant valueFor(const TranscriptRevisionSummary& revision, int role) const;
    void emitFlagChanges(const QString& previousJobId, const QString& currentJobId, int role);

    QList<TranscriptRevisionSummary> m_revisions;
    QString m_selectedJobId;
    QString m_activeJobId;
};

} // namespace BreezeDesk
