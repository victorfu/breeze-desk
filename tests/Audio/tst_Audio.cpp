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
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <limits>

using namespace BreezeDesk;

namespace {

bool writePcmWaveFixture(const QString& path, qint64 durationMs, quint32 sampleRate = 16'000,
                         quint16 channels = 1, quint16 bitsPerSample = 16,
                         bool includeAncillaryChunks = true) {
    if (durationMs <= 0 || channels == 0 || bitsPerSample == 0 || (bitsPerSample % 8) != 0) {
        return false;
    }
    const quint16 blockAlign = static_cast<quint16>(channels * (bitsPerSample / 8));
    const quint32 bytesPerSecond = sampleRate * blockAlign;
    const qint64 dataSize = (durationMs * static_cast<qint64>(bytesPerSecond)) / 1'000;
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

} // namespace

class AudioTest final : public QObject {
    Q_OBJECT

  private slots:
    void parsesFfprobeMetadata();
    void generatesMultiresolutionWaveformFromUnicodePath();
    void cancellationLeavesNoWaveform();
    void missingFfprobeIsActionable();
    void validatesNormalizedPcmWithAncillaryChunks();
    void rejectsWrongFormatTruncationAndDurationMismatch();
    void normalizationCommitsOnlyValidatedOutput();
    void normalizationPreservesExistingOutputWhenValidationFails();
};

void AudioTest::parsesFfprobeMetadata() {
    const QByteArray json = R"({
      "streams": [
        {"codec_type":"video","codec_name":"h264"},
        {"codec_type":"audio","codec_name":"aac","sample_rate":"48000","channels":2,"duration":"12.345"}
      ],
      "format":{"format_name":"mov,mp4","duration":"12.345","bit_rate":"128000"}
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

void AudioTest::normalizationCommitsOnlyValidatedOutput() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = temporary.filePath(QStringLiteral("good-source.media"));
    const QString outputPath = temporary.filePath(QStringLiteral("normalized.wav"));
    QVERIFY(writeSourceFixture(sourcePath));
    QFile existing(outputPath);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    QCOMPARE(existing.write("previous"), 8);
    existing.close();

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
