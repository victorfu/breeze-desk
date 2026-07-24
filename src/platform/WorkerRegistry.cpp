#include "breezedesk/platform/WorkerRegistry.h"

#include "breezedesk/app_config.h"

#include <QDir>

namespace BreezeDesk {
namespace {

void appendUnique(QStringList* candidates, const QString& path) {
    const QString cleaned = QDir::cleanPath(path.trimmed());
    if (!cleaned.isEmpty() && cleaned != QStringLiteral(".") && !candidates->contains(cleaned)) {
        candidates->append(cleaned);
    }
}

} // namespace

QStringList WorkerRegistry::executableCandidates(const QString& applicationDirectory,
                                                 const QString& preferredBackend, const QString& overridePath,
                                                 const QString& developmentPath) {
#if defined(Q_OS_WIN)
    constexpr WorkerHostPlatform HostPlatform = WorkerHostPlatform::Windows;
#elif defined(Q_OS_MACOS)
    constexpr WorkerHostPlatform HostPlatform = WorkerHostPlatform::MacOS;
#else
    constexpr WorkerHostPlatform HostPlatform = WorkerHostPlatform::Unix;
#endif
    return executableCandidatesForHost(HostPlatform, applicationDirectory, preferredBackend, overridePath,
                                       developmentPath);
}

QStringList WorkerRegistry::executableCandidatesForHost(WorkerHostPlatform hostPlatform,
                                                        const QString& applicationDirectory,
                                                        const QString& preferredBackend,
                                                        const QString& overridePath,
                                                        const QString& developmentPath) {
    QStringList candidates;
    appendUnique(&candidates, overridePath);

    if (hostPlatform == WorkerHostPlatform::Windows) {
        const QString executable = QString::fromLatin1(AppConfig::WorkerExecutableName);
        const QDir workers(QDir(applicationDirectory).filePath(QStringLiteral("workers")));
        const QString vulkan = workers.filePath(executable + QStringLiteral("-vulkan.exe"));
        const QString cpu = workers.filePath(executable + QStringLiteral("-cpu.exe"));
        const QString generic = QDir(applicationDirectory).filePath(executable + QStringLiteral(".exe"));
        if (preferredBackend.compare(QStringLiteral("CPU"), Qt::CaseInsensitive) == 0) {
            appendUnique(&candidates, cpu);
            appendUnique(&candidates, generic);
        } else if (preferredBackend.compare(QStringLiteral("Vulkan"), Qt::CaseInsensitive) == 0) {
            appendUnique(&candidates, vulkan);
            appendUnique(&candidates, cpu);
            appendUnique(&candidates, generic);
        } else {
            appendUnique(&candidates, vulkan);
            appendUnique(&candidates, cpu);
            appendUnique(&candidates, generic);
        }
        appendUnique(&candidates,
                     QDir(applicationDirectory).filePath(QStringLiteral("../worker/%1.exe").arg(executable)));
    } else {
        const QString executable = QString::fromLatin1(AppConfig::WorkerExecutableName);
        appendUnique(&candidates, QDir(applicationDirectory).filePath(executable));
        if (hostPlatform == WorkerHostPlatform::MacOS) {
            appendUnique(
                &candidates,
                QDir(applicationDirectory).filePath(QStringLiteral("../Helpers/%1").arg(executable)));
            appendUnique(
                &candidates,
                QDir(applicationDirectory).filePath(QStringLiteral("../../../../worker/%1").arg(executable)));
        } else {
            appendUnique(&candidates,
                         QDir(applicationDirectory).filePath(QStringLiteral("../worker/%1").arg(executable)));
        }
    }
    appendUnique(&candidates, developmentPath);
    return candidates;
}

} // namespace BreezeDesk
