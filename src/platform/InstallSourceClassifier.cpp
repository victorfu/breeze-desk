#include "breezedesk/platform/InstallSourceClassifier.h"

#include <QDir>

namespace BreezeDesk {
namespace {

QString normalizedPath(const QString& path) {
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    QString separatorsNormalized = trimmed;
    separatorsNormalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QDir::cleanPath(separatorsNormalized).toCaseFolded();
}

bool isSameOrChildPath(const QString& path, const QString& parent) {
    return !parent.isEmpty() && (path == parent || path.startsWith(parent + QLatin1Char('/')));
}

bool containsPathSegment(const QString& path, const QString& segment) {
    return (QLatin1Char('/') + path + QLatin1Char('/'))
        .contains(QLatin1Char('/') + segment + QLatin1Char('/'));
}

} // namespace

QString classifyWindowsInstallSource(const QString& applicationDirectory,
                                     const QString& registeredInstallDirectory) {
    const QString applicationPath = normalizedPath(applicationDirectory);
    if (containsPathSegment(applicationPath, QStringLiteral("windowsapps"))) {
        return QStringLiteral("msix");
    }

    const QString registeredPath = normalizedPath(registeredInstallDirectory);
    if (isSameOrChildPath(applicationPath, registeredPath) ||
        containsPathSegment(applicationPath, QStringLiteral("program files")) ||
        containsPathSegment(applicationPath, QStringLiteral("program files (x86)"))) {
        return QStringLiteral("direct");
    }
    return QStringLiteral("development");
}

} // namespace BreezeDesk
