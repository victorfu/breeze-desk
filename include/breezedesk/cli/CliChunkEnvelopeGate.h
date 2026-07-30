#pragma once

#include "breezedesk/ipc/Protocol.h"

namespace BreezeDesk {

struct CliChunkEnvelopeGate {
    bool protocolFailurePending = false;
    bool requestInFlight = true;

    [[nodiscard]] bool shouldProcess(const Ipc::MessageType type) noexcept {
        if (!protocolFailurePending) {
            return true;
        }
        switch (type) {
        case Ipc::MessageType::ChunkCompleted:
        case Ipc::MessageType::TranscriptionCompleted:
        case Ipc::MessageType::JobCancelled:
        case Ipc::MessageType::Error:
            requestInFlight = false;
            break;
        default:
            break;
        }
        return false;
    }
};

} // namespace BreezeDesk
