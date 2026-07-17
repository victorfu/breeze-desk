#include <breezedesk/asr/PromptTokenBudget.h>

#include <algorithm>
#include <limits>

namespace BreezeDesk::Asr {
namespace {

int kindRank(PromptPartKind kind) {
    switch (kind) {
    case PromptPartKind::Glossary:
        return 0;
    case PromptPartKind::MeetingContext:
        return 1;
    case PromptPartKind::PreviousTranscript:
        return 2;
    }
    return 3;
}

QString sentence(const PromptPart& part) {
    const QString text = part.text.trimmed();
    switch (part.kind) {
    case PromptPartKind::Glossary:
        return QStringLiteral("Important terminology: %1.").arg(text);
    case PromptPartKind::MeetingContext:
        return QStringLiteral("Meeting context: %1.").arg(text);
    case PromptPartKind::PreviousTranscript:
        return QStringLiteral("Previous confirmed transcript: %1").arg(text);
    }
    return text;
}

} // namespace

PromptBudgetResult PromptTokenBudget::compose(QList<PromptPart> parts, int maximumTokens,
                                              const TokenCounter& counter) {
    PromptBudgetResult result;
    std::stable_sort(parts.begin(), parts.end(), [](const auto& left, const auto& right) {
        const int leftKind = kindRank(left.kind);
        const int rightKind = kindRank(right.kind);
        return leftKind == rightKind ? left.priority > right.priority : leftKind < rightKind;
    });
    if (maximumTokens <= 0 || !counter) {
        result.omitted = std::move(parts);
        return result;
    }

    for (const auto& part : parts) {
        if (part.text.trimmed().isEmpty()) {
            continue;
        }
        const QString candidate =
            result.prompt.isEmpty() ? sentence(part) : result.prompt + QLatin1Char(' ') + sentence(part);
        const int count = counter(candidate);
        if (count < 0 || count > maximumTokens) {
            result.omitted.append(part);
            continue;
        }
        result.prompt = candidate;
        result.tokenCount = count;
    }
    return result;
}

} // namespace BreezeDesk::Asr
