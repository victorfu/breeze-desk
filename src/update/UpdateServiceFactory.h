#pragma once

#include "breezedesk/update/IUpdateService.h"

#include <memory>

namespace BreezeDesk {

[[nodiscard]] std::unique_ptr<IUpdateService> createNativeUpdateService(QObject* parent);

} // namespace BreezeDesk
