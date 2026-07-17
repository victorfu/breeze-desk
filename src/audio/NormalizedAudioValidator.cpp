#include "breezedesk/audio/NormalizedAudioValidator.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <limits>

namespace BreezeDesk {
namespace {

constexpr quint16 PcmFormat = 1;
constexpr quint16 RequiredChannels = 1;
constexpr quint32 RequiredSampleRate = 16'000;
constexpr quint16 RequiredBitsPerSample = 16;
constexpr quint16 RequiredBlockAlign = RequiredChannels * (RequiredBitsPerSample / 8);
constexpr quint32 RequiredBytesPerSecond = RequiredSampleRate * RequiredBlockAlign;
constexpr qint64 MinimumDurationToleranceMs = 250;
constexpr qint64 MaximumDurationToleranceMs = 2'000;
constexpr qint64 DurationToleranceDivisor = 200;

void setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

quint16 readLe16(const char* data) {
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data));
}

quint32 readLe32(const char* data) {
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(data));
}

qint64 durationToleranceMs(qint64 expectedDurationMs) {
    return std::clamp(expectedDurationMs / DurationToleranceDivisor, MinimumDurationToleranceMs,
                      MaximumDurationToleranceMs);
}

} // namespace

bool NormalizedAudioValidator::validate(const QString& path, qint64 expectedDurationMs,
                                        NormalizedAudioInfo* info, QString* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (expectedDurationMs <= 0) {
        setError(error, QStringLiteral("The expected media duration is invalid."));
        return false;
    }
    if (expectedDurationMs >
        std::numeric_limits<qint64>::max() / static_cast<qint64>(RequiredBytesPerSecond)) {
        setError(error, QStringLiteral("The expected media duration is too large."));
        return false;
    }

    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        setError(error, input.errorString());
        return false;
    }
    const qint64 fileSize = input.size();
    const QByteArray header = input.read(12);
    if (header.size() != 12 || header.first(4) != QByteArrayLiteral("RIFF") ||
        header.sliced(8, 4) != QByteArrayLiteral("WAVE")) {
        setError(error, QStringLiteral("The normalized output is not a RIFF/WAVE file."));
        return false;
    }

    const qint64 riffEnd = 8 + static_cast<qint64>(readLe32(header.constData() + 4));
    if (riffEnd != fileSize) {
        setError(error, QStringLiteral("The normalized WAV RIFF size does not match the file size."));
        return false;
    }

    bool formatFound = false;
    bool dataFound = false;
    quint16 audioFormat = 0;
    quint16 channels = 0;
    quint32 sampleRate = 0;
    quint32 bytesPerSecond = 0;
    quint16 blockAlign = 0;
    quint16 bitsPerSample = 0;
    qint64 dataOffset = 0;
    qint64 dataSize = 0;
    while (input.pos() + 8 <= riffEnd) {
        const QByteArray chunkHeader = input.read(8);
        if (chunkHeader.size() != 8) {
            setError(error, QStringLiteral("The normalized WAV contains a truncated chunk header."));
            return false;
        }
        const QByteArray chunkId = chunkHeader.first(4);
        const quint32 chunkSize = readLe32(chunkHeader.constData() + 4);
        const qint64 chunkDataOffset = input.pos();
        const qint64 chunkEnd = chunkDataOffset + static_cast<qint64>(chunkSize);
        const qint64 paddedEnd = chunkEnd + static_cast<qint64>(chunkSize & 1U);
        if (chunkEnd < chunkDataOffset || paddedEnd > riffEnd) {
            setError(error, QStringLiteral("A normalized WAV chunk extends past the end of the file."));
            return false;
        }

        if (chunkId == QByteArrayLiteral("fmt ")) {
            if (formatFound || chunkSize < 16) {
                setError(error, QStringLiteral("The normalized WAV format chunk is invalid."));
                return false;
            }
            const QByteArray format = input.read(16);
            if (format.size() != 16) {
                setError(error, QStringLiteral("The normalized WAV format chunk is truncated."));
                return false;
            }
            audioFormat = readLe16(format.constData());
            channels = readLe16(format.constData() + 2);
            sampleRate = readLe32(format.constData() + 4);
            bytesPerSecond = readLe32(format.constData() + 8);
            blockAlign = readLe16(format.constData() + 12);
            bitsPerSample = readLe16(format.constData() + 14);
            formatFound = true;
        } else if (chunkId == QByteArrayLiteral("data")) {
            if (dataFound) {
                setError(error, QStringLiteral("The normalized WAV contains multiple data chunks."));
                return false;
            }
            dataOffset = chunkDataOffset;
            dataSize = static_cast<qint64>(chunkSize);
            dataFound = true;
        }

        if (!input.seek(paddedEnd)) {
            setError(error, input.errorString());
            return false;
        }
    }

    if (input.pos() != riffEnd || !formatFound || !dataFound) {
        setError(error, QStringLiteral("The normalized WAV is missing required format or audio data."));
        return false;
    }
    if (audioFormat != PcmFormat || channels != RequiredChannels || sampleRate != RequiredSampleRate ||
        bytesPerSecond != RequiredBytesPerSecond || blockAlign != RequiredBlockAlign ||
        bitsPerSample != RequiredBitsPerSample) {
        setError(error, QStringLiteral("The normalized WAV must be 16 kHz mono signed PCM16 audio."));
        return false;
    }
    if (dataSize <= 0 || (dataSize % RequiredBlockAlign) != 0) {
        setError(error, QStringLiteral("The normalized WAV contains invalid PCM sample data."));
        return false;
    }

    const qint64 expectedDataSize =
        (expectedDurationMs * static_cast<qint64>(RequiredBytesPerSecond)) / 1'000;
    const qint64 toleranceMs = durationToleranceMs(expectedDurationMs);
    const qint64 toleratedBytes =
        (toleranceMs * static_cast<qint64>(RequiredBytesPerSecond)) / 1'000 + RequiredBlockAlign;
    if (qAbs(dataSize - expectedDataSize) > toleratedBytes) {
        const qint64 actualDurationMs =
            (dataSize * 1'000 + (RequiredBytesPerSecond / 2)) / RequiredBytesPerSecond;
        setError(error,
                 QStringLiteral("The normalized WAV duration is %1 ms; expected %2 ms (tolerance %3 ms).")
                     .arg(actualDurationMs)
                     .arg(expectedDurationMs)
                     .arg(toleranceMs));
        return false;
    }

    if (info != nullptr) {
        info->dataOffset = dataOffset;
        info->dataSize = dataSize;
        info->durationMs = (dataSize * 1'000 + (RequiredBytesPerSecond / 2)) / RequiredBytesPerSecond;
    }
    return true;
}

} // namespace BreezeDesk
