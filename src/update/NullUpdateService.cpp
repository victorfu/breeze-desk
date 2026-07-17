#include "breezedesk/update/NullUpdateService.h"

namespace BreezeDesk {

bool NullUpdateService::isAvailable() const {
    return false;
}

void NullUpdateService::checkForUpdates(bool userInitiated) {
    if (userInitiated) {
        emit updateError(QStringLiteral("Automatic updates are unavailable for this installation. Download "
                                        "releases from the project website."));
    }
}

} // namespace BreezeDesk
