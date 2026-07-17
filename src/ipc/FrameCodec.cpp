#include <breezedesk/ipc/FrameCodec.h>

#include <QtCore/QCborParserError>
#include <QtCore/QCborValue>
#include <QtCore/QtEndian>

#include <limits>

namespace BreezeDesk::Ipc {
namespace {

void setError(ProtocolError* error, ProtocolErrorCode code, const QString& detail) {
    if (error != nullptr) {
        *error = {code, detail};
    }
}

} // namespace

QByteArray FrameCodec::encode(const Envelope& envelope, ProtocolError* error) {
    const QByteArray body = QCborValue(envelope.toCbor()).toCbor();
    if (body.isEmpty()) {
        setError(error, ProtocolErrorCode::InvalidEnvelope,
                 QStringLiteral("CBOR serialization produced an empty body"));
        return {};
    }
    if (body.size() > static_cast<qsizetype>(kMaximumMessageSize) ||
        body.size() > static_cast<qsizetype>(std::numeric_limits<quint32>::max())) {
        setError(error, ProtocolErrorCode::FrameTooLarge,
                 QStringLiteral("Serialized message exceeds the protocol limit"));
        return {};
    }

    QByteArray frame(kFramePrefixSize, Qt::Uninitialized);
    qToBigEndian(static_cast<quint32>(body.size()), reinterpret_cast<uchar*>(frame.data()));
    frame.append(body);
    if (error != nullptr) {
        *error = {};
    }
    return frame;
}

FrameDecoder::FrameDecoder(quint32 maximumMessageSize) : m_maximumMessageSize(maximumMessageSize) {}

FrameParseResult FrameDecoder::append(QByteArrayView bytes) {
    FrameParseResult result;
    if (m_fatalError) {
        result.error = {ProtocolErrorCode::InvalidFrameLength,
                        QStringLiteral("Decoder must be reset after a fatal framing error")};
        return result;
    }

    if (!bytes.isEmpty()) {
        m_buffer.append(bytes.data(), bytes.size());
    }

    while (m_buffer.size() >= kFramePrefixSize) {
        const auto length = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(m_buffer.constData()));
        if (length == 0U) {
            m_fatalError = true;
            result.error = {ProtocolErrorCode::InvalidFrameLength,
                            QStringLiteral("Zero-length frames are not valid")};
            return result;
        }
        if (length > m_maximumMessageSize) {
            m_fatalError = true;
            result.error = {
                ProtocolErrorCode::FrameTooLarge,
                QStringLiteral("Frame length %1 exceeds limit %2").arg(length).arg(m_maximumMessageSize)};
            return result;
        }

        const auto completeSize = kFramePrefixSize + static_cast<qsizetype>(length);
        if (m_buffer.size() < completeSize) {
            break;
        }

        const QByteArray body = m_buffer.mid(kFramePrefixSize, length);
        m_buffer.remove(0, completeSize);
        QCborParserError parserError;
        const QCborValue value = QCborValue::fromCbor(body, &parserError);
        if (parserError.error != QCborError::NoError || !value.isMap()) {
            m_fatalError = true;
            result.error = {ProtocolErrorCode::InvalidCbor,
                            QStringLiteral("Invalid CBOR payload at offset %1").arg(parserError.offset)};
            return result;
        }

        ProtocolError envelopeError;
        auto envelope = Envelope::fromCbor(value.toMap(), &envelopeError);
        if (!envelope.has_value()) {
            m_fatalError = true;
            result.error = envelopeError;
            return result;
        }
        result.envelopes.append(std::move(*envelope));
    }
    return result;
}

void FrameDecoder::reset() {
    m_buffer.clear();
    m_fatalError = false;
}

qsizetype FrameDecoder::bufferedBytes() const noexcept {
    return m_buffer.size();
}

bool FrameDecoder::hasFatalError() const noexcept {
    return m_fatalError;
}

} // namespace BreezeDesk::Ipc
