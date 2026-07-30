#include "breezedesk/audio/AudioCacheManager.h"
#include "breezedesk/audio/FFmpegNormalizationService.h"
#include "breezedesk/audio/FFprobeService.h"
#include "breezedesk/audio/MediaMetadata.h"
#include "breezedesk/audio/NormalizedAudioValidator.h"
#include "breezedesk/audio/WaveformGenerator.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <functional>
#include <limits>

using namespace BreezeDesk;

namespace {

bool writePcmWaveFixture(const QString& path, qint64 durationMs, quint32 sampleRate = 16'000,
                         quint16 channels = 1, quint16 bitsPerSample = 16,
                         bool includeAncillaryChunks = true, quint32 trailingSamples = 0) {
    if (durationMs <= 0 || channels == 0 || bitsPerSample == 0 || (bitsPerSample % 8) != 0) {
        return false;
    }
    const quint16 blockAlign = static_cast<quint16>(channels * (bitsPerSample / 8));
    const quint32 bytesPerSecond = sampleRate * blockAlign;
    const qint64 dataSize = (durationMs * static_cast<qint64>(bytesPerSecond)) / 1'000 +
                            static_cast<qint64>(trailingSamples) * blockAlign;
    if (dataSize <= 0 || dataSize > std::numeric_limits<int>::max()) {
        return false;
    }

    QByteArray body;
    QBuffer bodyBuffer(&body);
    if (!bodyBuffer.open(QIODevice::WriteOnly)) {
        return false;
    }
    QDataStream bodyStream(&bodyBuffer);
    bodyStream.setByteOrder(QDataStream::LittleEndian);
    bodyStream.writeRawData("WAVE", 4);
    if (includeAncillaryChunks) {
        bodyStream.writeRawData("JUNK", 4);
        bodyStream << quint32{3};
        bodyStream.writeRawData("tag", 3);
        bodyStream << quint8{0};
    }
    bodyStream.writeRawData("fmt ", 4);
    bodyStream << quint32{16} << quint16{1} << channels << sampleRate << bytesPerSecond << blockAlign
               << bitsPerSample;
    bodyStream.writeRawData("data", 4);
    bodyStream << static_cast<quint32>(dataSize);
    const QByteArray samples(static_cast<qsizetype>(dataSize), '\0');
    bodyStream.writeRawData(samples.constData(), static_cast<int>(samples.size()));
    if (includeAncillaryChunks) {
        bodyStream.writeRawData("LIST", 4);
        bodyStream << quint32{4};
        bodyStream.writeRawData("INFO", 4);
    }
    if (bodyStream.status() != QDataStream::Ok) {
        return false;
    }

    QByteArray wave;
    QBuffer waveBuffer(&wave);
    if (!waveBuffer.open(QIODevice::WriteOnly)) {
        return false;
    }
    QDataStream waveStream(&waveBuffer);
    waveStream.setByteOrder(QDataStream::LittleEndian);
    waveStream.writeRawData("RIFF", 4);
    waveStream << static_cast<quint32>(body.size());
    waveStream.writeRawData(body.constData(), static_cast<int>(body.size()));
    if (waveStream.status() != QDataStream::Ok) {
        return false;
    }
    QFile output(path);
    return output.open(QIODevice::WriteOnly) && output.write(wave) == wave.size();
}

bool writeSourceFixture(const QString& path) {
    QFile source(path);
    return source.open(QIODevice::WriteOnly) && source.write("fixture") == 7;
}

bool writeWaveformCacheFixture(const QString& path, quint16 levelCount,
                               const std::function<void(QDataStream&)>& writeLevels) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32{0x4257504BU} << quint16{1} << levelCount;
    if (writeLevels)
        writeLevels(stream);
    return stream.status() == QDataStream::Ok;
}

int maximumPcmMagnitude(const QString& path, const NormalizedAudioInfo& info, const qint64 startMs,
                        const qint64 endMs) {
    constexpr qint64 SamplesPerMillisecond = 16;
    constexpr qint64 BytesPerSample = 2;
    QFile file(path);
    const qint64 firstSample = startMs * SamplesPerMillisecond;
    const qint64 sampleCount = (endMs - startMs) * SamplesPerMillisecond;
    if (startMs < 0 || endMs <= startMs || !file.open(QIODevice::ReadOnly) ||
        !file.seek(info.dataOffset + firstSample * BytesPerSample)) {
        return -1;
    }
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    int maximum = 0;
    for (qint64 index = 0; index < sampleCount; ++index) {
        qint16 sample = 0;
        stream >> sample;
        if (stream.status() != QDataStream::Ok) {
            return -1;
        }
        maximum = qMax(maximum, qAbs(static_cast<int>(sample)));
    }
    return maximum;
}

} // namespace

class AudioTest final : public QObject {
    Q_OBJECT

  private slots:
    void executionScopedCachePathsDoNotCollide();
    void removesOnlyExpiredUnreferencedGenerationFiles();
    void parsesFfprobeMetadata();
    void prefersDefaultAudioStreamMetadata();
    void usesAudioEndOnTheContainerTimeline();
    void trimsAudioBeforeTheContainerTimeline();
    void usesEarliestStreamStartWhenFormatStartIsMissing();
    void generatesMultiresolutionWaveformFromUnicodePath();
    void cancellationLeavesNoWaveform();
    void rejectsTruncatedWaveformCacheHeaders();
    void rejectsWaveformCachePayloadBeforeAllocation();
    void rejectsOversizedWaveformCacheBeforeAllocation();
    void rejectsInvalidWaveformCacheMetadata();
    void missingFfprobeIsActionable();
    void validatesNormalizedPcmWithAncillaryChunks();
    void rejectsWrongFormatTruncationAndDurationMismatch();
    void normalizationReportsMissingExecutableAfterReturn();
    void normalizationCanCancelBeforeDeferredStart();
    void normalizationMapsSelectedAudioStream();
    void normalizationPreservesContainerTimelineGaps();
    void normalizationCommitsOnlyValidatedOutput();
    void normalizationRejectsExistingGenerationTarget();
    void normalizationPreservesExistingOutputWhenValidationFails();
};

void AudioTest::executionScopedCachePathsDoNotCollide() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousDataRoot = qgetenv("BREEZEDESK_DATA_ROOT");
    const auto restoreDataRoot = qScopeGuard([previousDataRoot] {
        if (previousDataRoot.isNull()) {
            qunsetenv("BREEZEDESK_DATA_ROOT");
        } else {
            qputenv("BREEZEDESK_DATA_ROOT", previousDataRoot);
        }
    });
    Q_UNUSED(restoreDataRoot)
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());

    const QString normalizedA =
        AudioCacheManager::normalizedAudioPath(QStringLiteral("recording"), QStringLiteral("owner-a"));
    const QString normalizedB =
        AudioCacheManager::normalizedAudioPath(QStringLiteral("recording"), QStringLiteral("owner-b"));
    const QString waveformA =
        AudioCacheManager::waveformPath(QStringLiteral("recording"), QStringLiteral("owner-a"));
    const QString waveformB =
        AudioCacheManager::waveformPath(QStringLiteral("recording"), QStringLiteral("owner-b"));

    QVERIFY(!normalizedA.isEmpty());
    QVERIFY(!normalizedB.isEmpty());
    QVERIFY(normalizedA != normalizedB);
    QVERIFY(waveformA != waveformB);
    QVERIFY(AudioCacheManager::isReusableNormalizedAudioPath(normalizedA));
    const QString legacyNormalized =
        QDir(QFileInfo(normalizedA).absolutePath()).filePath(QStringLiteral("recording.owner-a.wav"));
    QVERIFY(!AudioCacheManager::isReusableNormalizedAudioPath(legacyNormalized));
    QVERIFY(QFileInfo(normalizedA).fileName().contains(QStringLiteral("owner-a")));
    QVERIFY(QFileInfo(waveformB).fileName().contains(QStringLiteral("owner-b")));
}

void AudioTest::removesOnlyExpiredUnreferencedGenerationFiles() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousDataRoot = qgetenv("BREEZEDESK_DATA_ROOT");
    const auto restoreDataRoot = qScopeGuard([previousDataRoot] {
        if (previousDataRoot.isNull()) {
            qunsetenv("BREEZEDESK_DATA_ROOT");
        } else {
            qputenv("BREEZEDESK_DATA_ROOT", previousDataRoot);
        }
    });
    Q_UNUSED(restoreDataRoot)
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());

    const QString referenced = AudioCacheManager::normalizedAudioPath(
        QStringLiteral("referenced"), QStringLiteral("finished-owner"));
    const QString activeOwner = AudioCacheManager::waveformPath(
        QStringLiteral("in-progress"), QStringLiteral("active-owner"));
    const QString orphanedAudio = AudioCacheManager::normalizedAudioPath(
        QStringLiteral("orphaned-audio"), QStringLiteral("stale-owner"));
    const QString orphanedWaveform = AudioCacheManager::waveformPath(
        QStringLiteral("orphaned-waveform"), QStringLiteral("stale-owner"));
    const QString recentOrphan = AudioCacheManager::normalizedAudioPath(
        QStringLiteral("recent-orphan"), QStringLiteral("stale-owner"));

    const auto createFile = [](const QString& path) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write("cache") == 5;
    };
    for (const QString& path :
         {referenced, activeOwner, orphanedAudio, orphanedWaveform, recentOrphan}) {
        QVERIFY(createFile(path));
    }
    const QDateTime oldTimestamp = QDateTime::currentDateTimeUtc().addDays(-2);
    for (const QString& path : {referenced, activeOwner, orphanedAudio, orphanedWaveform}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(oldTimestamp, QFileDevice::FileModificationTime));
    }

    QString referencedKey = QDir::cleanPath(QFileInfo(referenced).absoluteFilePath());
#ifdef Q_OS_WIN
    referencedKey = referencedKey.toUpper();
#endif
    QCOMPARE(AudioCacheManager::removeExpiredOrphanedGenerationFiles(
                 QSet<QString>{referencedKey}, QStringLiteral("active-owner"), 24),
             2);
    QVERIFY(QFileInfo(referenced).isFile());
    QVERIFY(QFileInfo(activeOwner).isFile());
    QVERIFY(!QFileInfo::exists(orphanedAudio));
    QVERIFY(!QFileInfo::exists(orphanedWaveform));
    QVERIFY(QFileInfo(recentOrphan).isFile());
}

void AudioTest::parsesFfprobeMetadata() {
    const QByteArray json = R"({
      "streams": [
        {"codec_type":"video","codec_name":"h264"},
        {"codec_type":"audio","codec_name":"aac","sample_rate":"48000","channels":2,
         "start_time":"0.273","duration":"12.345"}
      ],
      "format":{"format_name":"mov,mp4","start_time":"0.273","duration":"12.345",
                "bit_rate":"128000"}
    })";
    QString error;
    const MediaMetadata metadata =
        MediaMetadata::fromFfprobeJson(QJsonDocument::fromJson(json).object(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(metadata.hasAudio);
    QVERIFY(metadata.hasVideo);
    QCOMPARE(metadata.codecName, QStringLiteral("aac"));
    QCOMPARE(metadata.sampleRate, 48000);
    QCOMPARE(metadata.channelCount, 2);
    QCOMPARE(metadata.durationMs, 12345);
    QCOMPARE(metadata.bitRate, 128000);
    QCOMPARE(metadata.audioStreamIndex, 0);
}

void AudioTest::prefersDefaultAudioStreamMetadata() {
    const QByteArray json = R"({
      "streams": [
        {"codec_type":"audio","codec_name":"aac","sample_rate":"16000","channels":1,
         "duration":"1.000","disposition":{"default":0}},
        {"codec_type":"video","codec_name":"h264"},
        {"codec_type":"audio","codec_name":"opus","sample_rate":"48000","channels":2,
         "duration":"3.000","disposition":{"default":1}}
      ],
      "format":{"format_name":"mov,mp4","duration":"3.000","bit_rate":"128000"}
    })";
    QString error;
    const MediaMetadata metadata =
        MediaMetadata::fromFfprobeJson(QJsonDocument::fromJson(json).object(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(metadata.hasAudio);
    QVERIFY(metadata.hasVideo);
    QCOMPARE(metadata.codecName, QStringLiteral("opus"));
    QCOMPARE(metadata.sampleRate, 48'000);
    QCOMPARE(metadata.channelCount, 2);
    QCOMPARE(metadata.durationMs, 3'000);
    QCOMPARE(metadata.audioStreamIndex, 1);
}

void AudioTest::usesAudioEndOnTheContainerTimeline() {
    const QByteArray json = R"({
      "streams": [
        {"codec_type":"video","codec_name":"mpeg4","start_time":"0.000","duration":"4.000"},
        {"codec_type":"audio","codec_name":"aac","sample_rate":"48000","channels":2,
         "start_time":"1.976","duration":"1.045"}
      ],
      "format":{"format_name":"mov,mp4","start_time":"0.000","duration":"4.000"}
    })";
    QString error;
    const MediaMetadata metadata =
        MediaMetadata::fromFfprobeJson(QJsonDocument::fromJson(json).object(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(metadata.hasAudio);
    QVERIFY(metadata.hasVideo);
    QCOMPARE(metadata.audioStreamIndex, 0);
    QCOMPARE(metadata.durationMs, 3'021);
}

void AudioTest::trimsAudioBeforeTheContainerTimeline() {
    const QByteArray json = R"({
      "streams": [
        {"codec_type":"audio","codec_name":"aac","sample_rate":"48000","channels":2,
         "start_time":"-1.000","duration":"2.000"}
      ],
      "format":{"format_name":"mov,mp4","start_time":"0.000","duration":"1.000"}
    })";
    QString error;
    const MediaMetadata metadata =
        MediaMetadata::fromFfprobeJson(QJsonDocument::fromJson(json).object(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(metadata.durationMs, 1'000);
}

void AudioTest::usesEarliestStreamStartWhenFormatStartIsMissing() {
    const QByteArray json = R"({
      "streams": [
        {"codec_type":"video","codec_name":"mpeg4","start_time":"0.000","duration":"3.000"},
        {"codec_type":"audio","codec_name":"aac","sample_rate":"48000","channels":2,
         "start_time":"1.500","duration":"1.500"}
      ],
      "format":{"format_name":"mov,mp4","duration":"3.000"}
    })";
    QString error;
    const MediaMetadata metadata =
        MediaMetadata::fromFfprobeJson(QJsonDocument::fromJson(json).object(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(metadata.durationMs, 3'000);
}

void AudioTest::generatesMultiresolutionWaveformFromUnicodePath() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString pcmPath = temporary.filePath(QStringLiteral("會議 音訊.pcm"));
    QFile pcm(pcmPath);
    QVERIFY(pcm.open(QIODevice::WriteOnly));
    QDataStream stream(&pcm);
    stream.setByteOrder(QDataStream::LittleEndian);
    for (int sample = 0; sample < 256 * 10000; ++sample) {
        stream << static_cast<qint16>((sample % 2048) - 1024);
    }
    pcm.close();
    const QString waveformPath = temporary.filePath(QStringLiteral("波形 cache.bwpk"));
    std::atomic_bool cancelled = false;
    QString error;
    QVERIFY2(WaveformGenerator::generate(pcmPath, waveformPath, &cancelled, &error), qPrintable(error));
    const QVector<WaveformLevel> levels = WaveformGenerator::read(waveformPath, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(levels.size() >= 2);
    QCOMPARE(levels.first().minimums.size(), 10000);
    QVERIFY(levels.last().minimums.size() <= 2048);
}

void AudioTest::cancellationLeavesNoWaveform() {
    QTemporaryDir temporary;
    const QString inputPath = temporary.filePath(QStringLiteral("cancel.pcm"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    input.write(QByteArray(4096, '\0'));
    input.close();
    std::atomic_bool cancelled = true;
    QString error;
    const QString outputPath = temporary.filePath(QStringLiteral("cancel.bwpk"));
    QVERIFY(!WaveformGenerator::generate(inputPath, outputPath, &cancelled, &error));
    QVERIFY(error.contains(QStringLiteral("cancel"), Qt::CaseInsensitive));
    QVERIFY(!QFileInfo::exists(outputPath));
}

void AudioTest::rejectsTruncatedWaveformCacheHeaders() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QString error;

    const QString mainHeaderPath = temporary.filePath(QStringLiteral("truncated-main.bwpk"));
    QFile mainHeader(mainHeaderPath);
    QVERIFY(mainHeader.open(QIODevice::WriteOnly));
    QDataStream mainStream(&mainHeader);
    mainStream.setByteOrder(QDataStream::LittleEndian);
    mainStream << quint32{0x4257504BU};
    mainHeader.close();
    QVERIFY(WaveformGenerator::read(mainHeaderPath, &error).isEmpty());
    QVERIFY2(error.contains(QStringLiteral("truncated"), Qt::CaseInsensitive), qPrintable(error));

    const QString levelHeaderPath = temporary.filePath(QStringLiteral("truncated-level.bwpk"));
    QVERIFY(writeWaveformCacheFixture(levelHeaderPath, 1, [](QDataStream& stream) {
        stream << quint32{256};
    }));
    QVERIFY(WaveformGenerator::read(levelHeaderPath, &error).isEmpty());
    QVERIFY2(error.contains(QStringLiteral("truncated"), Qt::CaseInsensitive), qPrintable(error));
}

void AudioTest::rejectsWaveformCachePayloadBeforeAllocation() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("missing-payload.bwpk"));
    QVERIFY(writeWaveformCacheFixture(path, 1, [](QDataStream& stream) {
        stream << quint32{256} << quint64{4096};
    }));

    QString error;
    QVERIFY(WaveformGenerator::read(path, &error).isEmpty());
    QVERIFY2(error.contains(QStringLiteral("truncated"), Qt::CaseInsensitive), qPrintable(error));
}

void AudioTest::rejectsOversizedWaveformCacheBeforeAllocation() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("oversized.bwpk"));
    QVERIFY(writeWaveformCacheFixture(path, 1, [](QDataStream& stream) {
        stream << quint32{256}
               << quint64{WaveformGenerator::MaximumSerializedPeakPairs + 1ULL};
    }));

    QString error;
    QVERIFY(WaveformGenerator::read(path, &error).isEmpty());
    QVERIFY2(error.contains(QStringLiteral("too large"), Qt::CaseInsensitive), qPrintable(error));
}

void AudioTest::rejectsInvalidWaveformCacheMetadata() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QString error;

    const QString noLevelsPath = temporary.filePath(QStringLiteral("no-levels.bwpk"));
    QVERIFY(writeWaveformCacheFixture(noLevelsPath, 0, {}));
    QVERIFY(WaveformGenerator::read(noLevelsPath, &error).isEmpty());
    QVERIFY2(error.contains(QStringLiteral("level count"), Qt::CaseInsensitive), qPrintable(error));

    const QString zeroWindowPath = temporary.filePath(QStringLiteral("zero-window.bwpk"));
    QVERIFY(writeWaveformCacheFixture(zeroWindowPath, 1, [](QDataStream& stream) {
        stream << quint32{0} << quint64{1} << qint16{-1} << qint16{1};
    }));
    QVERIFY(WaveformGenerator::read(zeroWindowPath, &error).isEmpty());
    QVERIFY2(error.contains(QStringLiteral("metadata"), Qt::CaseInsensitive), qPrintable(error));

    const QString zeroCountPath = temporary.filePath(QStringLiteral("zero-count.bwpk"));
    QVERIFY(writeWaveformCacheFixture(zeroCountPath, 1, [](QDataStream& stream) {
        stream << quint32{256} << quint64{0};
    }));
    QVERIFY(WaveformGenerator::read(zeroCountPath, &error).isEmpty());
    QVERIFY2(error.contains(QStringLiteral("metadata"), Qt::CaseInsensitive), qPrintable(error));
}

void AudioTest::missingFfprobeIsActionable() {
    QTemporaryDir temporary;
    const QString mediaPath = temporary.filePath(QStringLiteral("audio.wav"));
    QFile file(mediaPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not-a-wave");
    file.close();
    FFprobeService service(temporary.filePath(QStringLiteral("missing-ffprobe")));
    QString error;
    const MediaMetadata metadata = service.inspect(mediaPath, &error);
    QVERIFY(!metadata.hasAudio);
    QVERIFY(!error.isEmpty());
}

void AudioTest::validatesNormalizedPcmWithAncillaryChunks() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString wavePath = temporary.filePath(QStringLiteral("normalized 會議.wav"));
    QVERIFY(writePcmWaveFixture(wavePath, 1'000));
    NormalizedAudioInfo info;
    QString error;
    QVERIFY2(NormalizedAudioValidator::validate(wavePath, 1'000, &info, &error), qPrintable(error));
    QCOMPARE(info.dataSize, 32'000);
    QCOMPARE(info.durationMs, 1'000);
    QVERIFY(info.dataOffset > 44);

    const QString withinTolerance = temporary.filePath(QStringLiteral("within-tolerance.wav"));
    QVERIFY(writePcmWaveFixture(withinTolerance, 9'800, 16'000, 1, 16, false));
    QVERIFY2(NormalizedAudioValidator::validate(withinTolerance, 10'000, nullptr, &error), qPrintable(error));

    const QString decodedMp4Audio = temporary.filePath(QStringLiteral("decoded-mp4-audio.wav"));
    // Ten trailing samples put the PCM duration at 40,745.625 ms. The canonical
    // whole-millisecond duration must floor to a fully readable endpoint.
    QVERIFY(writePcmWaveFixture(decodedMp4Audio, 40'745, 16'000, 1, 16, false, 10));
    QVERIFY2(NormalizedAudioValidator::validate(decodedMp4Audio, 40'789, &info, &error),
             qPrintable(error));
    QCOMPARE(info.durationMs, 40'745);
}

void AudioTest::rejectsWrongFormatTruncationAndDurationMismatch() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QString error;

    const QString wrongFormat = temporary.filePath(QStringLiteral("wrong-format.wav"));
    QVERIFY(writePcmWaveFixture(wrongFormat, 1'000, 48'000));
    QVERIFY(!NormalizedAudioValidator::validate(wrongFormat, 1'000, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("16 kHz")));

    const QString truncated = temporary.filePath(QStringLiteral("truncated.wav"));
    QVERIFY(writePcmWaveFixture(truncated, 1'000));
    QFile truncatedFile(truncated);
    QVERIFY(truncatedFile.open(QIODevice::ReadWrite));
    QVERIFY(truncatedFile.resize(truncatedFile.size() - 1));
    truncatedFile.close();
    QVERIFY(!NormalizedAudioValidator::validate(truncated, 1'000, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("RIFF size")));

    const QString durationMismatch = temporary.filePath(QStringLiteral("duration-mismatch.wav"));
    QVERIFY(writePcmWaveFixture(durationMismatch, 9'600, 16'000, 1, 16, false));
    QVERIFY(!NormalizedAudioValidator::validate(durationMismatch, 10'000, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("duration")));
}

void AudioTest::normalizationReportsMissingExecutableAfterReturn() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = temporary.filePath(QStringLiteral("source.media"));
    const QString outputPath = temporary.filePath(QStringLiteral("normalized.wav"));
    QVERIFY(writeSourceFixture(sourcePath));

    FFmpegNormalizationService service(temporary.filePath(QStringLiteral("missing-ffmpeg")));
    QScopedPointer<NormalizationOperation> operation(service.normalize(sourcePath, outputPath, 1'000));
    QSignalSpy finished(operation.data(), &NormalizationOperation::finished);

    QVERIFY(operation->isRunning());
    QVERIFY(finished.wait(5'000));
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.constFirst().at(0).toBool(), false);
    QVERIFY(!operation->isRunning());
    QVERIFY(operation->error().contains(QStringLiteral("ffmpeg"), Qt::CaseInsensitive));
    QVERIFY(!QFileInfo::exists(outputPath));
    QVERIFY(
        QDir(temporary.path()).entryList({QStringLiteral("normalized.wav.tmp.*")}, QDir::Files).isEmpty());
}

void AudioTest::normalizationCanCancelBeforeDeferredStart() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = temporary.filePath(QStringLiteral("source.media"));
    const QString outputPath = temporary.filePath(QStringLiteral("normalized.wav"));
    QVERIFY(writeSourceFixture(sourcePath));

    FFmpegNormalizationService service(QString::fromUtf8(BREEZEDESK_NORMALIZATION_HELPER_PATH));
    QScopedPointer<NormalizationOperation> operation(service.normalize(sourcePath, outputPath, 1'000));
    QSignalSpy finished(operation.data(), &NormalizationOperation::finished);

    QVERIFY(operation->isRunning());
    operation->cancel();
    QVERIFY(finished.wait(5'000));
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.constFirst().at(0).toBool(), false);
    QVERIFY(!operation->isRunning());
    QVERIFY(operation->error().contains(QStringLiteral("cancel"), Qt::CaseInsensitive));
    QVERIFY(!QFileInfo::exists(outputPath));
    QVERIFY(
        QDir(temporary.path()).entryList({QStringLiteral("normalized.wav.tmp.*")}, QDir::Files).isEmpty());
}

void AudioTest::normalizationMapsSelectedAudioStream() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = temporary.filePath(QStringLiteral("mapped-stream-1.media"));
    const QString outputPath = temporary.filePath(QStringLiteral("normalized.wav"));
    QVERIFY(writeSourceFixture(sourcePath));

    FFmpegNormalizationService service(QString::fromUtf8(BREEZEDESK_NORMALIZATION_HELPER_PATH));
    QScopedPointer<NormalizationOperation> operation(
        service.normalize(sourcePath, outputPath, 1'000, 1));
    QSignalSpy finished(operation.data(), &NormalizationOperation::finished);
    if (finished.isEmpty()) {
        QVERIFY(finished.wait(5'000));
    }
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.constFirst().at(0).toBool(), true);
    QCOMPARE(finished.constFirst().at(1).toString(), outputPath);
    QVERIFY(QFileInfo(outputPath).isFile());
}

void AudioTest::normalizationPreservesContainerTimelineGaps() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = temporary.filePath(QStringLiteral("delayed-audio.media"));
    const QString outputPath = temporary.filePath(QStringLiteral("normalized.wav"));
    QVERIFY(writeSourceFixture(sourcePath));

    FFmpegNormalizationService service(QString::fromUtf8(BREEZEDESK_NORMALIZATION_HELPER_PATH));
    QScopedPointer<NormalizationOperation> operation(service.normalize(sourcePath, outputPath, 3'021));
    QSignalSpy finished(operation.data(), &NormalizationOperation::finished);
    if (finished.isEmpty()) {
        QVERIFY(finished.wait(5'000));
    }
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.constFirst().at(0).toBool(), true);

    NormalizedAudioInfo info;
    QString error;
    QVERIFY2(NormalizedAudioValidator::validate(outputPath, 3'021, &info, &error), qPrintable(error));
    QCOMPARE(info.durationMs, 3'021);
    QCOMPARE(maximumPcmMagnitude(outputPath, info, 0, 1'900), 0);
    QVERIFY(maximumPcmMagnitude(outputPath, info, 2'050, 2'900) > 1'000);
}

void AudioTest::normalizationCommitsOnlyValidatedOutput() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = temporary.filePath(QStringLiteral("good-source.media"));
    const QString outputPath = temporary.filePath(QStringLiteral("normalized.wav"));
    QVERIFY(writeSourceFixture(sourcePath));

    FFmpegNormalizationService service(QString::fromUtf8(BREEZEDESK_NORMALIZATION_HELPER_PATH));
    QScopedPointer<NormalizationOperation> operation(service.normalize(sourcePath, outputPath, 1'000));
    QSignalSpy finished(operation.data(), &NormalizationOperation::finished);
    if (finished.isEmpty()) {
        QVERIFY(finished.wait(5'000));
    }
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.constFirst().at(0).toBool(), true);
    QCOMPARE(finished.constFirst().at(1).toString(), outputPath);
    QCOMPARE(operation->progress(), 1.0);
    QString error;
    QVERIFY2(NormalizedAudioValidator::validate(outputPath, 1'000, nullptr, &error), qPrintable(error));
    QVERIFY(
        QDir(temporary.path()).entryList({QStringLiteral("normalized.wav.tmp.*")}, QDir::Files).isEmpty());
}

void AudioTest::normalizationRejectsExistingGenerationTarget() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = temporary.filePath(QStringLiteral("good-source.media"));
    const QString outputPath = temporary.filePath(QStringLiteral("normalized.owner.wav"));
    QVERIFY(writeSourceFixture(sourcePath));
    QFile existing(outputPath);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    QCOMPARE(existing.write("current-owner-output"), qint64{20});
    existing.close();

    FFmpegNormalizationService service(QString::fromUtf8(BREEZEDESK_NORMALIZATION_HELPER_PATH));
    QScopedPointer<NormalizationOperation> operation(service.normalize(sourcePath, outputPath, 1'000));
    QSignalSpy finished(operation.data(), &NormalizationOperation::finished);
    if (finished.isEmpty()) {
        QVERIFY(finished.wait(5'000));
    }
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.constFirst().at(0).toBool(), false);
    QVERIFY(operation->error().contains(QStringLiteral("already exists")));
    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), QByteArrayLiteral("current-owner-output"));
    QVERIFY(QDir(temporary.path())
                .entryList({QStringLiteral("normalized.owner.wav.tmp.*")}, QDir::Files)
                .isEmpty());
}

void AudioTest::normalizationPreservesExistingOutputWhenValidationFails() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = temporary.filePath(QStringLiteral("bad-source.media"));
    const QString outputPath = temporary.filePath(QStringLiteral("normalized.wav"));
    QVERIFY(writeSourceFixture(sourcePath));
    QFile existing(outputPath);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    QCOMPARE(existing.write("preserve-me"), 11);
    existing.close();

    FFmpegNormalizationService service(QString::fromUtf8(BREEZEDESK_NORMALIZATION_HELPER_PATH));
    QScopedPointer<NormalizationOperation> operation(service.normalize(sourcePath, outputPath, 1'000));
    QSignalSpy finished(operation.data(), &NormalizationOperation::finished);
    if (finished.isEmpty()) {
        QVERIFY(finished.wait(5'000));
    }
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.constFirst().at(0).toBool(), false);
    QVERIFY(operation->error().contains(QStringLiteral("invalid normalized audio")));
    QFile preserved(outputPath);
    QVERIFY(preserved.open(QIODevice::ReadOnly));
    QCOMPARE(preserved.readAll(), QByteArrayLiteral("preserve-me"));
    QVERIFY(
        QDir(temporary.path()).entryList({QStringLiteral("normalized.wav.tmp.*")}, QDir::Files).isEmpty());
}

QTEST_MAIN(AudioTest)
#include "tst_Audio.moc"
