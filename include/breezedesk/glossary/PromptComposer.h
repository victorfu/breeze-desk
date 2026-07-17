#pragma once

#include "breezedesk/core/Result.h"
#include "breezedesk/glossary/Glossary.h"

#include <functional>

namespace BreezeDesk {

struct PromptCompositionRequest {
    GlossaryProfile profile;
    QList<GlossaryTerm> terms;
    QString meetingContext;
    QString previousConfirmedText;
    int maximumTokens = 0;
};

struct PromptCompositionResult {
    QString prompt;
    int tokenCount = 0;
    int maximumTokens = 0;
    QStringList includedTermIds;
    QStringList omittedTermIds;
};

class PromptComposer final {
  public:
    using TokenCounter = std::function<int(const QString&)>;

    [[nodiscard]] Result<PromptCompositionResult> compose(const PromptCompositionRequest& request,
                                                          const TokenCounter& tokenCounter) const;
};

} // namespace BreezeDesk
