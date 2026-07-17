#include <breezedesk/cli/CliForwardingPolicy.h>

namespace BreezeDesk {

bool CliForwardingPolicy::consumeHeadlessFlag(QStringList* arguments) {
    return arguments != nullptr && arguments->removeAll(QStringLiteral("--headless")) > 0;
}

} // namespace BreezeDesk
