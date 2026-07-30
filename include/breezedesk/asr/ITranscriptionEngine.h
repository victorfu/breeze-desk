#pragma once

#include <breezedesk/asr/AsrTypes.h>
#include <breezedesk/asr/CancellationFlag.h>

#include <QtCore/QVector>

namespace BreezeDesk::Asr {

class ITranscriptionEngine {
  public:
    virtual ~ITranscriptionEngine() = default;

    [[nodiscard]] virtual ModelLoadResult loadModel(const ModelLoadOptions& options) = 0;
    virtual void unloadModel() = 0;
    [[nodiscard]] virtual bool isModelLoaded() const noexcept = 0;
    [[nodiscard]] virtual TranscriptionResult transcribe(const QVector<float>& samples, qint64 globalOffsetMs,
                                                         const TranscriptionOptions& options,
                                                         const TranscriptionCallbacks& callbacks,
                                                         const CancellationFlag& cancellation) = 0;
};

} // namespace BreezeDesk::Asr
