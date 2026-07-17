#include <breezedesk/ipc/FrameCodec.h>

#include <QtCore/QCborValue>
#include <QtCore/QCoreApplication>
#include <QtCore/QtEndian>
#include <QtTest/QTest>

using namespace BreezeDesk::Ipc;

class FrameCodecTest final : public QObject {
    Q_OBJECT

  private slots:
    void roundTrip();
    void acceptsPartialPackets();
    void acceptsMultiplePackets();
    void rejectsOversizeLength();
    void rejectsInvalidCbor();
    void rejectsInvalidEnvelope();
};

Envelope sampleEnvelope() {
    Envelope envelope;
    envelope.type = MessageType::StartTranscription;
    envelope.requestId = QStringLiteral("request-1");
    envelope.jobId = QStringLiteral("job-中文");
    envelope.workerVersion = QStringLiteral("1.0.0");
    envelope.sessionToken = QByteArray(32, 'x');
    envelope.payload.insert(QStringLiteral("path"), QStringLiteral("/tmp/語音 file.m4a"));
    return envelope;
}

void FrameCodecTest::roundTrip() {
    const Envelope source = sampleEnvelope();
    ProtocolError encodeError;
    const QByteArray frame = FrameCodec::encode(source, &encodeError);
    QVERIFY(!encodeError.isError());
    QVERIFY(!frame.isEmpty());

    FrameDecoder decoder;
    const auto parsed = decoder.append(frame);
    QVERIFY(!parsed.error.isError());
    QCOMPARE(parsed.envelopes.size(), 1);
    const auto& decoded = parsed.envelopes.constFirst();
    QCOMPARE(decoded.type, source.type);
    QCOMPARE(decoded.requestId, source.requestId);
    QCOMPARE(decoded.jobId, source.jobId);
    QCOMPARE(decoded.sessionToken, source.sessionToken);
    QCOMPARE(decoded.payload.value(QStringLiteral("path")).toString(), QStringLiteral("/tmp/語音 file.m4a"));
}

void FrameCodecTest::acceptsPartialPackets() {
    const QByteArray frame = FrameCodec::encode(sampleEnvelope());
    FrameDecoder decoder;
    for (qsizetype index = 0; index < frame.size() - 1; ++index) {
        const auto parsed = decoder.append(QByteArrayView(frame).sliced(index, 1));
        QVERIFY(!parsed.error.isError());
        QVERIFY(parsed.envelopes.isEmpty());
    }
    const auto parsed = decoder.append(QByteArrayView(frame).last(1));
    QCOMPARE(parsed.envelopes.size(), 1);
    QCOMPARE(decoder.bufferedBytes(), 0);
}

void FrameCodecTest::acceptsMultiplePackets() {
    Envelope second = sampleEnvelope();
    second.requestId = QStringLiteral("request-2");
    FrameDecoder decoder;
    const auto parsed = decoder.append(FrameCodec::encode(sampleEnvelope()) + FrameCodec::encode(second));
    QVERIFY(!parsed.error.isError());
    QCOMPARE(parsed.envelopes.size(), 2);
    QCOMPARE(parsed.envelopes.at(1).requestId, QStringLiteral("request-2"));
}

void FrameCodecTest::rejectsOversizeLength() {
    QByteArray prefix(kFramePrefixSize, Qt::Uninitialized);
    qToBigEndian(kMaximumMessageSize + 1U, reinterpret_cast<uchar*>(prefix.data()));
    FrameDecoder decoder;
    const auto parsed = decoder.append(prefix);
    QCOMPARE(parsed.error.code, ProtocolErrorCode::FrameTooLarge);
    QVERIFY(decoder.hasFatalError());
}

void FrameCodecTest::rejectsInvalidCbor() {
    const QByteArray body("not-cbor");
    QByteArray frame(kFramePrefixSize, Qt::Uninitialized);
    qToBigEndian(static_cast<quint32>(body.size()), reinterpret_cast<uchar*>(frame.data()));
    frame.append(body);
    FrameDecoder decoder;
    QCOMPARE(decoder.append(frame).error.code, ProtocolErrorCode::InvalidCbor);
}

void FrameCodecTest::rejectsInvalidEnvelope() {
    QCborMap invalid;
    invalid.insert(QStringLiteral("type"), QStringLiteral("Ping"));
    const QByteArray body = QCborValue(invalid).toCbor();
    QByteArray frame(kFramePrefixSize, Qt::Uninitialized);
    qToBigEndian(static_cast<quint32>(body.size()), reinterpret_cast<uchar*>(frame.data()));
    frame.append(body);
    FrameDecoder decoder;
    QCOMPARE(decoder.append(frame).error.code, ProtocolErrorCode::InvalidEnvelope);
}

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    FrameCodecTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_FrameCodec.moc"
