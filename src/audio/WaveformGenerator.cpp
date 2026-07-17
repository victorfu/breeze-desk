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

WaveformLevel aggregate(const WaveformLevel& source) {
    WaveformLevel result;
    result.samplesPerPeak = source.samplesPerPeak * 4U;
    const qsizetype count = source.minimums.size();
    for (qsizetype index = 0; index < count; index += 4) {
        const qsizetype end = qMin(index + 4, count);
        qint16 minimum = std::numeric_limits<qint16>::max();
        qint16 maximum = std::numeric_limits<qint16>::min();
        for (qsizetype candidate = index; candidate < end; ++candidate) {
            minimum = qMin(minimum, source.minimums.at(candidate));
            maximum = qMax(maximum, source.maximums.at(candidate));
        }
        result.minimums.push_back(minimum);
        result.maximums.push_back(maximum);
    }
    return result;
}
} // namespace

bool WaveformGenerator::generate(const QString& pcm16Path, const QString& waveformPath,
                                 std::atomic_bool* cancelled, QString* error) {
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

    WaveformLevel base;
    base.samplesPerPeak = static_cast<quint32>(BaseWindowSamples);
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
        if (read <= 0 || (read % 2) != 0) {
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
        base.minimums.push_back(minimum);
        base.maximums.push_back(maximum);
    }
    if (base.minimums.isEmpty()) {
        if (error != nullptr)
            *error = QStringLiteral("The normalized audio does not contain PCM samples.");
        return false;
    }

    QVector<WaveformLevel> levels{base};
    while (levels.constLast().minimums.size() > 2048) {
        levels.push_back(aggregate(levels.constLast()));
    }

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
    if (magic != Magic || version != FormatVersion || levelCount > 32U) {
        if (error != nullptr) {
            *error = QStringLiteral("Unsupported waveform cache format.");
        }
        return {};
    }
    QVector<WaveformLevel> result;
    for (quint16 levelIndex = 0; levelIndex < levelCount; ++levelIndex) {
        WaveformLevel level;
        quint64 count = 0;
        stream >> level.samplesPerPeak >> count;
        if (count > 100000000ULL) {
            if (error != nullptr) {
                *error = QStringLiteral("Waveform cache is too large.");
            }
            return {};
        }
        level.minimums.reserve(static_cast<qsizetype>(count));
        level.maximums.reserve(static_cast<qsizetype>(count));
        for (quint64 index = 0; index < count; ++index) {
            qint16 minimum = 0;
            qint16 maximum = 0;
            stream >> minimum >> maximum;
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
    return result;
}

} // namespace BreezeDesk
