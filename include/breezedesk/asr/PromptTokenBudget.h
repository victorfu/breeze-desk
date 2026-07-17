#pragma once

#include <QtCore/QList>
#include <QtCore/QString>

#include <functional>

namespace BreezeDesk::Asr {

enum class PromptPartKind {
    Glossary,
    MeetingContext,
    PreviousTranscript,
};

struct PromptPart {
    QString text;
    PromptPartKind kind = PromptPartKind::Glossary;
    int priority = 0;
};

struct PromptBudgetResult {
    QString prompt;
    int tokenCount = 0;
    QList<PromptPart> omitted;
};

class PromptTokenBudget final {
  public:
    using TokenCounter = std::function<int(const QString&)>;

    [[nodiscard]] static PromptBudgetResult compose(QList<PromptPart> parts, int maximumTokens,
                                                    const TokenCounter& counter);
};

} // namespace BreezeDesk::Asr
