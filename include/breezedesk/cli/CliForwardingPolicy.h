#pragma once

#include <QtCore/QStringList>

namespace BreezeDesk {

class CliForwardingPolicy final {
  public:
    // Removes every global --headless flag so command parsers never treat it as a positional value.
    // Returns true when GUI forwarding must be bypassed.
    [[nodiscard]] static bool consumeHeadlessFlag(QStringList* arguments);
};

} // namespace BreezeDesk
