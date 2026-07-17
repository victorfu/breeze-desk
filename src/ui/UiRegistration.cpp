#include "breezedesk/ui/UiRegistration.h"

#include "breezedesk/ui/ApplicationViewModel.h"
#include "breezedesk/ui/DesignSystem.h"
#include "breezedesk/ui/WaveformItem.h"

#include <QQmlEngine>
#include <QQuickStyle>
#include <QtQml>

namespace BreezeDesk {

void registerUiTypes() {
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    constexpr auto uri = "BreezeDesk";
    qmlRegisterType<ApplicationViewModel>(uri, 1, 0, "ApplicationViewModel");
    qmlRegisterType<WaveformItem>(uri, 1, 0, "WaveformItem");
    qmlRegisterSingletonType<DesignSystem>(
        uri, 1, 0, "DesignSystem", [](QQmlEngine*, QJSEngine*) -> QObject* { return new DesignSystem; });
}

ApplicationViewModel* createApplicationViewModel(QObject* parent) {
    return new ApplicationViewModel(parent);
}

ApplicationViewModel* createApplicationViewModel(IRecordingRepository* recordingRepository, QObject* parent) {
    return new ApplicationViewModel(recordingRepository, parent);
}

ApplicationViewModel* createApplicationViewModel(IRecordingRepository* recordingRepository,
                                                 ITranscriptRepository* transcriptRepository,
                                                 QObject* parent) {
    return new ApplicationViewModel(recordingRepository, transcriptRepository, parent);
}

} // namespace BreezeDesk
