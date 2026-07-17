#pragma once

#include <breezedesk/asr/AsrTypes.h>

#include <QtCore/QVector>

#include <atomic>

namespace BreezeDesk::Asr {

class ITranscriptionEngine {
  public:
    virtual ~ITranscriptionEngine() = default;

    [[nodiscard]] virtual ModelLoadResult loadModel(const ModelLoadOptions& options) = 0;
    virtual void unloadModel() = 0;
    [[nodiscard]] virtual bool isModelLoaded() const noexcept = 0;
    [[nodiscard]] virtual TranscriptionResult transcribe(const QVector<float>& samples, qint64 globalOffsetMs,
                                                         const TranscriptionOptions& options,
                                                         const TranscriptionCallbacks& callbacks) = 0;
    virtual void requestCancellation() noexcept = 0;
};

} // namespace BreezeDesk::Asr
