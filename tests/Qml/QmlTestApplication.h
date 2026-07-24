#pragma once

#include <QByteArray>
#include <QDir>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <crtdbg.h>
#include <cstdlib>
#endif

#ifndef BREEZEDESK_QT_PLUGINS_DIR
#error BREEZEDESK_QT_PLUGINS_DIR must point to the Qt installation's plugins directory
#endif

namespace BreezeDesk::TestSupport {

inline void configureQmlTestProcess() {
#ifdef Q_OS_WIN
    // Qt's Debug assertions ultimately call abort(). Keep test failures on stderr
    // instead of blocking unattended runs with the Visual C++ Runtime dialog.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    if (!qEnvironmentVariableIsSet("QT_QPA_FONTDIR")) {
        const QString windowsDirectory = QString::fromLocal8Bit(qgetenv("WINDIR"));
        const QString systemFonts = QDir(windowsDirectory).filePath(QStringLiteral("Fonts"));
        if (QDir(systemFonts).exists()) {
            qputenv("QT_QPA_FONTDIR", QDir::toNativeSeparators(systemFonts).toLocal8Bit());
        }
    }
#endif

    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE")) {
        qputenv("QT_QUICK_CONTROLS_STYLE", QByteArrayLiteral("Basic"));
    }

    const QByteArray qtPlugins = QByteArrayLiteral(BREEZEDESK_QT_PLUGINS_DIR);
    QByteArray pluginPath = qgetenv("QT_PLUGIN_PATH");
    const char separator = QDir::listSeparator().toLatin1();
    if (!pluginPath.split(separator).contains(qtPlugins)) {
        if (!pluginPath.isEmpty()) {
            pluginPath.prepend(separator);
        }
        pluginPath.prepend(qtPlugins);
        qputenv("QT_PLUGIN_PATH", pluginPath);
    }
}

} // namespace BreezeDesk::TestSupport
