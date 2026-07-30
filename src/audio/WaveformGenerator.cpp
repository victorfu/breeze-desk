#include "breezedesk/audio/WaveformGenerator.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <limits>

namespace BreezeDesk {

namespace {
constexpr quint32 Magic = 0x4257504BU; // BWPK
constexpr quint16 FormatVersion = 1;
constexpr qsizetype BaseWindowSamples = 256;
constexpr qint64 SerializedLevelHeaderBytes = sizeof(quint32) + sizeof(quint64);
constexpr quint64 SerializedPeakPairBytes = sizeof(qint16) * 2ULL;
static_assert(WaveformGenerator::MaximumSerializedPeakPairs <=
              static_cast<quint64>(std::numeric_limits<qsizetype>::max()));

struct PcmLayout {
    qint64 dataOffset = 0;
    qint64 dataSize = 0;
};

bool inspectPcmLayout(QFile* file, PcmLayout* layout, QString* error) {
    if (file == nullptr || layout == nullptr || file->size() <= 0) {
        if (error != nullptr)
            *error = QStringLiteral("Normalized audio is empty.");
        return false;
    }
    const QByteArray header = file->peek(12);
    if (header.size() < 12 || header.first(4) != QByteArrayLiteral("RIFF") ||
        header.sliced(8, 4) != QByteArrayLiteral("WAVE")) {
        if (QFileInfo(file->fileName()).suffix().compare(QStringLiteral("pcm"), Qt::CaseInsensitive) != 0 ||
            (file->size() % 2) != 0) {
            if (error != nullptr)
                *error = QStringLiteral("Audio must be a PCM16 RIFF/WAVE file or raw .pcm data.");
            return false;
        }
        *layout = {0, file->size()};
        return true;
    }

    if (!file->seek(12)) {
        if (error != nullptr)
            *error = file->errorString();
        return false;
    }
    bool formatFound = false;
    bool dataFound = false;
    quint16 audioFormat = 0;
    quint16 channels = 0;
    quint32 sampleRate = 0;
    quint16 bitsPerSample = 0;
    while (file->pos() + 8 <= file->size()) {
        const QByteArray chunkHeader = file->read(8);
        if (chunkHeader.size() != 8)
            break;
        const QByteArray chunkId = chunkHeader.first(4);
        const quint32 chunkSize =
            qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(chunkHeader.constData() + 4));
        const qint64 chunkDataOffset = file->pos();
        const qint64 chunkEnd = chunkDataOffset + static_cast<qint64>(chunkSize);
        if (chunkEnd < chunkDataOffset || chunkEnd > file->size()) {
            if (error != nullptr)
                *error = QStringLiteral("A WAV chunk extends past the end of the file.");
            return false;
        }
        if (chunkId == QByteArrayLiteral("fmt ")) {
            if (chunkSize < 16) {
                if (error != nullptr)
                    *error = QStringLiteral("The WAV format chunk is too short.");
                return false;
            }
            const QByteArray format = file->read(16);
            if (format.size() != 16) {
                if (error != nullptr)
                    *error = QStringLiteral("The WAV format chunk is truncated.");
                return false;
            }
            const auto* bytes = reinterpret_cast<const uchar*>(format.constData());
            audioFormat = qFromLittleEndian<quint16>(bytes);
            channels = qFromLittleEndian<quint16>(bytes + 2);
            sampleRate = qFromLittleEndian<quint32>(bytes + 4);
            bitsPerSample = qFromLittleEndian<quint16>(bytes + 14);
            formatFound = true;
        } else if (chunkId == QByteArrayLiteral("data")) {
            layout->dataOffset = chunkDataOffset;
            layout->dataSize = static_cast<qint64>(chunkSize);
            dataFound = true;
        }
        const qint64 paddedEnd = chunkEnd + static_cast<qint64>(chunkSize & 1U);
        if (paddedEnd > file->size() || !file->seek(paddedEnd)) {
            if (error != nullptr)
                *error = QStringLiteral("The WAV chunk layout is invalid.");
            return false;
        }
        if (formatFound && dataFound)
            break;
    }
    if (!formatFound || !dataFound || audioFormat != 1 || channels != 1 || sampleRate != 16'000U ||
        bitsPerSample != 16 || layout->dataSize <= 0 || (layout->dataSize % 2) != 0) {
        if (error != nullptr)
            *error = QStringLiteral("The WAV file must contain 16 kHz mono signed PCM16 audio.");
        return false;
    }
    return true;
}

bool aggregate(const WaveformLevel& source, WaveformLevel* result) {
    if (result == nullptr || source.samplesPerPeak == 0 ||
        source.samplesPerPeak > std::numeric_limits<quint32>::max() / 4U ||
        source.minimums.isEmpty() || source.minimums.size() != source.maximums.size()) {
        return false;
    }
    result->samplesPerPeak = source.samplesPerPeak * 4U;
    const qsizetype count = source.minimums.size();
    result->minimums.reserve(count / 4 + (count % 4 != 0 ? 1 : 0));
    result->maximums.reserve(count / 4 + (count % 4 != 0 ? 1 : 0));
    for (qsizetype index = 0; index < count; index += 4) {
        const qsizetype end = qMin(index + 4, count);
        qint16 minimum = std::numeric_limits<qint16>::max();
        qint16 maximum = std::numeric_limits<qint16>::min();
        for (qsizetype candidate = index; candidate < end; ++candidate) {
            minimum = qMin(minimum, source.minimums.at(candidate));
            maximum = qMax(maximum, source.maximums.at(candidate));
        }
        result->minimums.push_back(minimum);
        result->maximums.push_back(maximum);
    }
    return true;
}

bool peakPlanFits(quint64 basePeakCount) {
    if (basePeakCount == 0 ||
        basePeakCount > WaveformGenerator::MaximumSerializedPeakPairs) {
        return false;
    }
    quint64 totalPeakCount = basePeakCount;
    quint64 currentPeakCount = basePeakCount;
    quint32 samplesPerPeak = static_cast<quint32>(BaseWindowSamples);
    quint16 levelCount = 1;
    while (currentPeakCount > 2048) {
        if (samplesPerPeak > std::numeric_limits<quint32>::max() / 4U || levelCount >= 32U) {
            return false;
        }
        samplesPerPeak *= 4U;
        currentPeakCount = currentPeakCount / 4U + (currentPeakCount % 4U != 0 ? 1U : 0U);
        if (currentPeakCount > WaveformGenerator::MaximumSerializedPeakPairs - totalPeakCount) {
            return false;
        }
        totalPeakCount += currentPeakCount;
        ++levelCount;
    }
    return true;
}

bool levelsFitSerializedLimits(const QVector<WaveformLevel>& levels, QString* error) {
    if (levels.isEmpty() || levels.size() > 32) {
        if (error != nullptr)
            *error = QStringLiteral("Waveform cache has an invalid level count.");
        return false;
    }
    quint64 totalPeakCount = 0;
    for (const WaveformLevel& level : levels) {
        if (level.samplesPerPeak == 0 || level.minimums.isEmpty() ||
            level.minimums.size() != level.maximums.size()) {
            if (error != nullptr)
                *error = QStringLiteral("Waveform cache contains invalid level data.");
            return false;
        }
        const quint64 count = static_cast<quint64>(level.minimums.size());
        if (count > WaveformGenerator::MaximumSerializedPeakPairs - totalPeakCount) {
            if (error != nullptr)
                *error = QStringLiteral("Waveform cache is too large.");
            return false;
        }
        totalPeakCount += count;
    }
    return true;
}
} // namespace

bool WaveformGenerator::generate(const QString& pcm16Path, const QString& waveformPath,
                                 std::atomic_bool* cancelled, QString* error) {
    if (error != nullptr)
        error->clear();
    QFile input(pcm16Path);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = input.errorString();
        }
        return false;
    }
    PcmLayout layout;
    if (!inspectPcmLayout(&input, &layout, error) || !input.seek(layout.dataOffset)) {
        if (error != nullptr && error->isEmpty())
            *error = input.errorString();
        return false;
    }

    const quint64 sampleCount = static_cast<quint64>(layout.dataSize / 2);
    const quint64 baseWindowSamples = static_cast<quint64>(BaseWindowSamples);
    const quint64 basePeakCount =
        sampleCount / baseWindowSamples + (sampleCount % baseWindowSamples != 0 ? 1U : 0U);
    if (!peakPlanFits(basePeakCount)) {
        if (error != nullptr)
            *error = QStringLiteral("The audio is too large to generate a bounded waveform cache.");
        return false;
    }

    WaveformLevel base;
    base.samplesPerPeak = static_cast<quint32>(BaseWindowSamples);
    base.minimums.reserve(static_cast<qsizetype>(basePeakCount));
    base.maximums.reserve(static_cast<qsizetype>(basePeakCount));
    QByteArray bytes(static_cast<qsizetype>(BaseWindowSamples * 2), Qt::Uninitialized);
    qint64 remaining = layout.dataSize;
    while (remaining > 0) {
        if (cancelled != nullptr && cancelled->load(std::memory_order_relaxed)) {
            if (error != nullptr) {
                *error = QStringLiteral("Waveform generation was cancelled.");
            }
            return false;
        }
        const qint64 requested = qMin<qint64>(bytes.size(), remaining);
        const qint64 read = input.read(bytes.data(), requested);
        if (read != requested || (read % 2) != 0) {
            if (error != nullptr)
                *error = QStringLiteral("The PCM sample data is truncated.");
            return false;
        }
        remaining -= read;
        const qsizetype samples = static_cast<qsizetype>(read / 2);
        qint16 minimum = std::numeric_limits<qint16>::max();
        qint16 maximum = std::numeric_limits<qint16>::min();
        for (qsizetype index = 0; index < samples; ++index) {
            const auto low = static_cast<quint8>(bytes.at(index * 2));
            const auto high = static_cast<quint8>(bytes.at(index * 2 + 1));
            const auto value =
                static_cast<qint16>(static_cast<quint16>(low) | (static_cast<quint16>(high) << 8U));
            minimum = qMin(minimum, value);
            maximum = qMax(maximum, value);
        }
        if (static_cast<quint64>(base.minimums.size()) >= basePeakCount) {
            if (error != nullptr)
                *error = QStringLiteral("The PCM sample layout changed during waveform generation.");
            return false;
        }
        base.minimums.push_back(minimum);
        base.maximums.push_back(maximum);
    }
    if (base.minimums.isEmpty()) {
        if (error != nullptr)
            *error = QStringLiteral("The normalized audio does not contain PCM samples.");
        return false;
    }

    quint64 totalPeakCount = static_cast<quint64>(base.minimums.size());
    if (totalPeakCount != basePeakCount) {
        if (error != nullptr)
            *error = QStringLiteral("The PCM sample layout changed during waveform generation.");
        return false;
    }
    QVector<WaveformLevel> levels;
    levels.reserve(32);
    levels.push_back(std::move(base));
    while (levels.constLast().minimums.size() > 2048) {
        WaveformLevel aggregated;
        if (!aggregate(levels.constLast(), &aggregated)) {
            if (error != nullptr)
                *error = QStringLiteral("Waveform resolution exceeds the supported range.");
            return false;
        }
        const quint64 aggregatedCount = static_cast<quint64>(aggregated.minimums.size());
        if (aggregatedCount > MaximumSerializedPeakPairs - totalPeakCount) {
            if (error != nullptr)
                *error = QStringLiteral("Waveform cache is too large.");
            return false;
        }
        totalPeakCount += aggregatedCount;
        levels.push_back(std::move(aggregated));
    }

    if (!levelsFitSerializedLimits(levels, error))
        return false;

    QSaveFile output(waveformPath);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = output.errorString();
        }
        return false;
    }
    QDataStream stream(&output);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << Magic << FormatVersion << static_cast<quint16>(levels.size());
    for (const WaveformLevel& level : levels) {
        stream << level.samplesPerPeak << static_cast<quint64>(level.minimums.size());
        for (qsizetype index = 0; index < level.minimums.size(); ++index) {
            stream << level.minimums.at(index) << level.maximums.at(index);
        }
    }
    if (stream.status() != QDataStream::Ok || !output.commit()) {
        if (error != nullptr) {
            *error = output.errorString();
        }
        return false;
    }
    return true;
}

QVector<WaveformLevel> WaveformGenerator::read(const QString& waveformPath, QString* error) {
    if (error != nullptr)
        error->clear();
    QFile input(waveformPath);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = input.errorString();
        }
        return {};
    }
    QDataStream stream(&input);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0;
    quint16 version = 0;
    quint16 levelCount = 0;
    stream >> magic >> version >> levelCount;
    if (stream.status() != QDataStream::Ok) {
        if (error != nullptr)
            *error = QStringLiteral("Waveform cache header is truncated.");
        return {};
    }
    if (magic != Magic || version != FormatVersion) {
        if (error != nullptr) {
            *error = QStringLiteral("Unsupported waveform cache format.");
        }
        return {};
    }
    if (levelCount == 0 || levelCount > 32U) {
        if (error != nullptr)
            *error = QStringLiteral("Waveform cache has an invalid level count.");
        return {};
    }
    QVector<WaveformLevel> result;
    result.reserve(levelCount);
    quint64 totalPeakCount = 0;
    for (quint16 levelIndex = 0; levelIndex < levelCount; ++levelIndex) {
        WaveformLevel level;
        quint64 count = 0;
        stream >> level.samplesPerPeak >> count;
        if (stream.status() != QDataStream::Ok) {
            if (error != nullptr)
                *error = QStringLiteral("Waveform cache level header is truncated.");
            return {};
        }
        if (level.samplesPerPeak == 0 || count == 0) {
            if (error != nullptr)
                *error = QStringLiteral("Waveform cache contains invalid level metadata.");
            return {};
        }
        if (count > MaximumSerializedPeakPairs ||
            count > MaximumSerializedPeakPairs - totalPeakCount) {
            if (error != nullptr)
                *error = QStringLiteral("Waveform cache is too large.");
            return {};
        }

        const qint64 remainingBytes = input.size() - input.pos();
        const qint64 remainingLevelHeaders =
            static_cast<qint64>(levelCount - levelIndex - 1U) * SerializedLevelHeaderBytes;
        if (remainingBytes < remainingLevelHeaders ||
            count > static_cast<quint64>(remainingBytes - remainingLevelHeaders) /
                        SerializedPeakPairBytes) {
            if (error != nullptr)
                *error = QStringLiteral("Waveform cache peak data is truncated.");
            return {};
        }

        totalPeakCount += count;
        level.minimums.reserve(static_cast<qsizetype>(count));
        level.maximums.reserve(static_cast<qsizetype>(count));
        for (quint64 index = 0; index < count; ++index) {
            qint16 minimum = 0;
            qint16 maximum = 0;
            stream >> minimum >> maximum;
            if (stream.status() != QDataStream::Ok) {
                if (error != nullptr)
                    *error = QStringLiteral("Waveform cache peak data is truncated.");
                return {};
            }
            if (minimum > maximum) {
                if (error != nullptr)
                    *error = QStringLiteral("Waveform cache contains an invalid peak range.");
                return {};
            }
            level.minimums.push_back(minimum);
            level.maximums.push_back(maximum);
        }
        result.push_back(std::move(level));
    }
    if (stream.status() != QDataStream::Ok) {
        if (error != nullptr) {
            *error = QStringLiteral("Waveform cache is truncated.");
        }
        return {};
    }
    if (input.pos() != input.size()) {
        if (error != nullptr)
            *error = QStringLiteral("Waveform cache contains unexpected trailing data.");
        return {};
    }
    return result;
}

} // namespace BreezeDesk
