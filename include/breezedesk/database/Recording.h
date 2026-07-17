#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace BreezeDesk {

struct Recording {
    QString id;
    QString title;
    QString sourcePath;
    QString managedMediaPath;
    QString normalizedPcmPath;
    QString sourceHash;
    QString mediaType;
    qint64 durationMs = 0;
    int sampleRate = 0;
    int channelCount = 0;
    QString waveformPath;
    QDateTime createdAt;
    QDateTime updatedAt;
    QDateTime deletedAt;
    QString notes;
    QString reviewState = QStringLiteral("unreviewed");
    QString activeJobId;
    QStringList tags;
    QString latestJobState;
    QString latestJobModelId;
    double latestJobProgress = 0.0;
};

struct RecordingPage {
    QList<Recording> items;
    int totalCount = 0;
    int offset = 0;
    int limit = 0;
};

struct RecordingQuery {
    QString searchText;
    QString status;
    QString tag;
    bool includeDeleted = false;
    bool deletedOnly = false;
    QString sortColumn = QStringLiteral("updated_at");
    Qt::SortOrder sortOrder = Qt::DescendingOrder;
    int offset = 0;
    int limit = 100;
};

} // namespace BreezeDesk
