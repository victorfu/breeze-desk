#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QStringList>

namespace BreezeDesk::Ipc {

struct ApplicationCommandReply {
    bool handled{false};
    int exitCode{0};
    QByteArray standardOutput;
    QByteArray standardError;
};

enum class ApplicationCommandForwardStatus {
    Completed,
    Unavailable,
    Declined,
    Indeterminate,
    ProtocolError,
};

struct ApplicationCommandForwardResult {
    ApplicationCommandForwardStatus status{ApplicationCommandForwardStatus::Unavailable};
    int exitCode{0};
    QByteArray standardOutput;
    QByteArray standardError;
    QString error;

    [[nodiscard]] bool completed() const noexcept {
        return status == ApplicationCommandForwardStatus::Completed;
    }

    [[nodiscard]] bool canExecuteLocally() const noexcept {
        return status == ApplicationCommandForwardStatus::Unavailable ||
               status == ApplicationCommandForwardStatus::Declined;
    }
};

class ApplicationCommandClient final {
  public:
    [[nodiscard]] static ApplicationCommandForwardResult
    forward(const QString& applicationId, const QStringList& arguments, int timeoutMs = 2'000);
};

} // namespace BreezeDesk::Ipc
