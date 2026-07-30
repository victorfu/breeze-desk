#pragma once

#include "breezedesk/models/ModelFileOperations.h"

#include <atomic>
#include <functional>
#include <memory>

namespace BreezeDesk {

class ModelManagerViewModel;

namespace Internal {

struct ModelManagerViewModelOperations {
    using Cancellation = std::shared_ptr<std::atomic_bool>;

    std::function<ModelVerificationResult(ModelVerificationSnapshot, Cancellation)> verify;
    std::function<PreparedCustomModelImport(CustomModelImportRequest, Cancellation)> prepareImport;
    std::function<void(const PreparedCustomModelImport&)> cleanupPreparedImport;
};

struct ModelManagerViewModelTestAccess final {
    [[nodiscard]] static std::unique_ptr<ModelManagerViewModel>
    create(ModelManagerViewModelOperations operations);
};

} // namespace Internal
} // namespace BreezeDesk
