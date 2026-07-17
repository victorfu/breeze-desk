#include "breezedesk/jobs/TranscriptionJob.h"

#include <QHash>

namespace BreezeDesk {
namespace {
template <typename Enum>
QString nameFor(const QHash<Enum, QString>& names, const Enum value, const QString& fallback) {
    const auto iterator = names.constFind(value);
    return iterator == names.cend() ? fallback : iterator.value();
}
template <typename Enum>
Enum valueFor(const QHash<Enum, QString>& names, const QString& name, const Enum fallback) {
    for (auto iterator = names.cbegin(); iterator != names.cend(); ++iterator) {
        if (iterator.value().compare(name, Qt::CaseInsensitive) == 0)
            return iterator.key();
    }
    return fallback;
}
const QHash<JobState, QString>& stateNames() {
    static const QHash<JobState, QString> values = {
        {JobState::Queued, QStringLiteral("Queued")},
        {JobState::Preparing, QStringLiteral("Preparing")},
        {JobState::Normalizing, QStringLiteral("Normalizing")},
        {JobState::WaitingForModel, QStringLiteral("WaitingForModel")},
        {JobState::LoadingModel, QStringLiteral("LoadingModel")},
        {JobState::AnalyzingSpeech, QStringLiteral("AnalyzingSpeech")},
        {JobState::Transcribing, QStringLiteral("Transcribing")},
        {JobState::Finalizing, QStringLiteral("Finalizing")},
        {JobState::Completed, QStringLiteral("Completed")},
        {JobState::Cancelling, QStringLiteral("Cancelling")},
        {JobState::Cancelled, QStringLiteral("Cancelled")},
        {JobState::Failed, QStringLiteral("Failed")},
        {JobState::Interrupted, QStringLiteral("Interrupted")},
    };
    return values;
}
const QHash<JobStage, QString>& stageNames() {
    static const QHash<JobStage, QString> values = {
        {JobStage::Preparing, QStringLiteral("Preparing")},
        {JobStage::InspectingMedia, QStringLiteral("InspectingMedia")},
        {JobStage::NormalizingAudio, QStringLiteral("NormalizingAudio")},
        {JobStage::AnalyzingSpeech, QStringLiteral("AnalyzingSpeech")},
        {JobStage::LoadingModel, QStringLiteral("LoadingModel")},
        {JobStage::Transcribing, QStringLiteral("Transcribing")},
        {JobStage::Finalizing, QStringLiteral("Finalizing")},
        {JobStage::Completed, QStringLiteral("Completed")},
    };
    return values;
}
const QHash<ChunkState, QString>& chunkNames() {
    static const QHash<ChunkState, QString> values = {
        {ChunkState::Pending, QStringLiteral("Pending")},
        {ChunkState::Running, QStringLiteral("Running")},
        {ChunkState::Completed, QStringLiteral("Completed")},
        {ChunkState::Cancelled, QStringLiteral("Cancelled")},
        {ChunkState::Failed, QStringLiteral("Failed")},
        {ChunkState::Interrupted, QStringLiteral("Interrupted")},
    };
    return values;
}
} // namespace

QString jobStateName(const JobState state) {
    return nameFor(stateNames(), state, QStringLiteral("Queued"));
}
JobState jobStateFromName(const QString& name, const JobState fallback) {
    return valueFor(stateNames(), name, fallback);
}
QString jobStageName(const JobStage stage) {
    return nameFor(stageNames(), stage, QStringLiteral("Preparing"));
}
JobStage jobStageFromName(const QString& name, const JobStage fallback) {
    return valueFor(stageNames(), name, fallback);
}
QString chunkStateName(const ChunkState state) {
    return nameFor(chunkNames(), state, QStringLiteral("Pending"));
}
ChunkState chunkStateFromName(const QString& name, const ChunkState fallback) {
    return valueFor(chunkNames(), name, fallback);
}

} // namespace BreezeDesk
