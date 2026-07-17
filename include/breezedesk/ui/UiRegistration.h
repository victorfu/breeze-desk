#pragma once

class QObject;

namespace BreezeDesk {

class ApplicationViewModel;
class IRecordingRepository;
class ITranscriptRepository;

void registerUiTypes();
[[nodiscard]] ApplicationViewModel* createApplicationViewModel(QObject* parent = nullptr);
[[nodiscard]] ApplicationViewModel* createApplicationViewModel(IRecordingRepository* recordingRepository,
                                                               QObject* parent);
[[nodiscard]] ApplicationViewModel* createApplicationViewModel(IRecordingRepository* recordingRepository,
                                                               ITranscriptRepository* transcriptRepository,
                                                               QObject* parent);

} // namespace BreezeDesk
