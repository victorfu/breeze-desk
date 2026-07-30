#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QTextStream>

#include <limits>

namespace {

QByteArray validWave(const qint64 durationMs = 1'000, const qint64 toneStartMs = -1,
                     const qint64 toneEndMs = -1) {
    constexpr quint32 SampleRate = 16'000;
    constexpr quint16 Channels = 1;
    constexpr quint16 BitsPerSample = 16;
    constexpr quint16 BlockAlign = Channels * (BitsPerSample / 8);
    constexpr quint32 BytesPerSecond = SampleRate * BlockAlign;
    if (durationMs <= 0 ||
        durationMs > std::numeric_limits<int>::max() / BytesPerSecond * 1'000) {
        return {};
    }
    const quint32 dataSize = static_cast<quint32>(durationMs * BytesPerSecond / 1'000);

    QByteArray body;
    QBuffer bodyBuffer(&body);
    if (!bodyBuffer.open(QIODevice::WriteOnly)) {
        return {};
    }
    QDataStream bodyStream(&bodyBuffer);
    bodyStream.setByteOrder(QDataStream::LittleEndian);
    bodyStream.writeRawData("WAVE", 4);
    bodyStream.writeRawData("JUNK", 4);
    bodyStream << quint32{3};
    bodyStream.writeRawData("tag", 3);
    bodyStream << quint8{0};
    bodyStream.writeRawData("fmt ", 4);
    bodyStream << quint32{16} << quint16{1} << Channels << SampleRate << BytesPerSecond << BlockAlign
               << BitsPerSample;
    bodyStream.writeRawData("data", 4);
    bodyStream << dataSize;
    QByteArray samples(static_cast<qsizetype>(dataSize), '\0');
    if (toneStartMs >= 0 && toneEndMs > toneStartMs) {
        const qint64 firstSample = qBound<qint64>(0, toneStartMs * SampleRate / 1'000,
                                                 dataSize / BlockAlign);
        const qint64 lastSample = qBound<qint64>(firstSample, toneEndMs * SampleRate / 1'000,
                                                dataSize / BlockAlign);
        constexpr qint16 ToneAmplitude = 4'000;
        for (qint64 sample = firstSample; sample < lastSample; ++sample) {
            const qsizetype byte = static_cast<qsizetype>(sample * BlockAlign);
            samples[byte] = static_cast<char>(ToneAmplitude & 0xff);
            samples[byte + 1] = static_cast<char>((ToneAmplitude >> 8) & 0xff);
        }
    }
    bodyStream.writeRawData(samples.constData(), static_cast<int>(samples.size()));
    if (bodyStream.status() != QDataStream::Ok) {
        return {};
    }

    QByteArray wave;
    QBuffer waveBuffer(&wave);
    if (!waveBuffer.open(QIODevice::WriteOnly)) {
        return {};
    }
    QDataStream waveStream(&waveBuffer);
    waveStream.setByteOrder(QDataStream::LittleEndian);
    waveStream.writeRawData("RIFF", 4);
    waveStream << static_cast<quint32>(body.size());
    waveStream.writeRawData(body.constData(), static_cast<int>(body.size()));
    return waveStream.status() == QDataStream::Ok ? wave : QByteArray{};
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    const qsizetype inputOption = arguments.indexOf(QStringLiteral("-i"));
    if (inputOption < 0 || inputOption + 1 >= arguments.size() || arguments.size() < 2) {
        return 2;
    }
    const QString sourcePath = arguments.at(inputOption + 1);
    const QString outputPath = arguments.constLast();
    if (sourcePath.contains(QStringLiteral("mapped-stream-1"))) {
        const qsizetype mapOption = arguments.indexOf(QStringLiteral("-map"));
        if (mapOption < 0 || mapOption + 1 >= arguments.size() ||
            arguments.at(mapOption + 1) != QLatin1String("0:a:1")) {
            return 5;
        }
    }
    QByteArray output;
    if (sourcePath.contains(QStringLiteral("delayed-audio"))) {
        const qsizetype filterOption = arguments.indexOf(QStringLiteral("-af"));
        if (filterOption < 0 || filterOption + 1 >= arguments.size()) {
            return 6;
        }
        const QString filter = arguments.at(filterOption + 1);
        if (!filter.contains(QStringLiteral("aresample")) ||
            !filter.contains(QStringLiteral("async=1")) ||
            !filter.contains(QStringLiteral("first_pts=0"))) {
            return 7;
        }
        output = validWave(3'021, 1'976, 3'021);
    } else {
        output = validWave();
    }
    if (sourcePath.contains(QStringLiteral("bad-source"))) {
        output.chop(1);
    }
    if (output.isEmpty()) {
        return 3;
    }
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(output) != output.size()) {
        return 4;
    }
    file.close();
    QTextStream(stdout) << "out_time_ms=1000000\nprogress=end\n";
    return 0;
}
