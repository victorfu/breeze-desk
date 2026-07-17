#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QTextStream>

namespace {

QByteArray validWave() {
    constexpr quint32 SampleRate = 16'000;
    constexpr quint16 Channels = 1;
    constexpr quint16 BitsPerSample = 16;
    constexpr quint16 BlockAlign = Channels * (BitsPerSample / 8);
    constexpr quint32 BytesPerSecond = SampleRate * BlockAlign;

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
    bodyStream << BytesPerSecond;
    const QByteArray samples(static_cast<qsizetype>(BytesPerSecond), '\0');
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
    QByteArray output = validWave();
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
