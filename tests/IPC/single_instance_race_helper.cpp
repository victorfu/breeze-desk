#include <breezedesk/ipc/ApplicationCommand.h>
#include <breezedesk/ipc/SingleInstanceGuard.h>

#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addOption(
        {QStringLiteral("application-id"), QStringLiteral("Test application ID"), QStringLiteral("id")});
    parser.addOption({QStringLiteral("hold-ms"), QStringLiteral("Primary hold duration"),
                      QStringLiteral("milliseconds"), QStringLiteral("750")});
    parser.addOption({QStringLiteral("send-command"), QStringLiteral("Send an application command")});
    parser.addOption({QStringLiteral("timeout-ms"), QStringLiteral("Command timeout"),
                      QStringLiteral("milliseconds"), QStringLiteral("3000")});
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Files to forward"));
    parser.process(application);

    if (parser.isSet(QStringLiteral("send-command"))) {
        const auto forwarded = BreezeDesk::Ipc::ApplicationCommandClient::forward(
            parser.value(QStringLiteral("application-id")), parser.positionalArguments(),
            parser.value(QStringLiteral("timeout-ms")).toInt());
        if (forwarded.completed()) {
            if (!forwarded.standardOutput.isEmpty()) {
                (void)fwrite(forwarded.standardOutput.constData(), 1,
                             static_cast<std::size_t>(forwarded.standardOutput.size()), stdout);
            }
            if (!forwarded.standardError.isEmpty()) {
                (void)fwrite(forwarded.standardError.constData(), 1,
                             static_cast<std::size_t>(forwarded.standardError.size()), stderr);
            }
            return forwarded.exitCode;
        }
        switch (forwarded.status) {
        case BreezeDesk::Ipc::ApplicationCommandForwardStatus::Declined:
            return 41;
        case BreezeDesk::Ipc::ApplicationCommandForwardStatus::Unavailable:
            return 42;
        case BreezeDesk::Ipc::ApplicationCommandForwardStatus::Indeterminate:
            return 43;
        case BreezeDesk::Ipc::ApplicationCommandForwardStatus::ProtocolError:
            return 44;
        case BreezeDesk::Ipc::ApplicationCommandForwardStatus::Completed:
            break;
        }
        return 45;
    }

    BreezeDesk::Ipc::SingleInstanceGuard guard(parser.value(QStringLiteral("application-id")));
    const auto result = guard.acquire(parser.positionalArguments(), 3'000);
    if (result == BreezeDesk::Ipc::SingleInstanceGuard::AcquireResult::Error) {
        return 2;
    }
    if (result == BreezeDesk::Ipc::SingleInstanceGuard::AcquireResult::Forwarded) {
        return 0;
    }
    QTimer::singleShot(parser.value(QStringLiteral("hold-ms")).toInt(), &application,
                       &QCoreApplication::quit);
    return application.exec();
}
