#pragma once

#include <breezedesk/ipc/Protocol.h>

#include <QtCore/QByteArray>
#include <QtCore/QList>

namespace BreezeDesk::Ipc {

struct FrameParseResult {
    QList<Envelope> envelopes;
    ProtocolError error;
};

class FrameCodec final {
  public:
    [[nodiscard]] static QByteArray encode(const Envelope& envelope, ProtocolError* error = nullptr);
};

class FrameDecoder final {
  public:
    explicit FrameDecoder(quint32 maximumMessageSize = kMaximumMessageSize);

    [[nodiscard]] FrameParseResult append(QByteArrayView bytes);
    void reset();
    [[nodiscard]] qsizetype bufferedBytes() const noexcept;
    [[nodiscard]] bool hasFatalError() const noexcept;

  private:
    QByteArray m_buffer;
    quint32 m_maximumMessageSize;
    bool m_fatalError = false;
};

} // namespace BreezeDesk::Ipc
