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

bool containsPathSegment(const QString& path, const QString& segment) {
    return (QLatin1Char('/') + path + QLatin1Char('/'))
        .contains(QLatin1Char('/') + segment + QLatin1Char('/'));
}

} // namespace

QString classifyWindowsInstallSource(const QString& applicationDirectory) {
    const QString applicationPath = normalizedPath(applicationDirectory);
    if (containsPathSegment(applicationPath, QStringLiteral("windowsapps"))) {
        return QStringLiteral("msix");
    }
    return QStringLiteral("development");
}

} // namespace BreezeDesk
