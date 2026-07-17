add_library(breezedesk_audio STATIC
    ${PROJECT_SOURCE_DIR}/include/breezedesk/audio/MediaMetadata.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/audio/IAudioMetadataReader.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/audio/IAudioNormalizer.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/audio/MediaFileSupport.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/audio/FFmpegLocator.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/audio/FFprobeService.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/audio/FFmpegNormalizationService.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/audio/NormalizedAudioValidator.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/audio/WaveformGenerator.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/audio/AudioCacheManager.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/audio/MicrophoneRecorder.h
    MediaMetadata.cpp
    IAudioNormalizer.cpp
    MediaFileSupport.cpp
    FFmpegLocator.cpp
    FFprobeService.cpp
    FFmpegNormalizationService.cpp
    NormalizedAudioValidator.cpp
    WaveformGenerator.cpp
    AudioCacheManager.cpp
    MicrophoneRecorder.cpp)

target_link_libraries(breezedesk_audio
    PUBLIC breezedesk_build_config Qt6::Core Qt6::Multimedia
    PRIVATE breezedesk_core Qt6::Concurrent)
breezedesk_enable_strict_warnings(breezedesk_audio)
