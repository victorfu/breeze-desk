#include "breezedesk/glossary/PromptComposer.h"

#include <algorithm>

namespace BreezeDesk {

Result<PromptCompositionResult> PromptComposer::compose(const PromptCompositionRequest& request,
                                                        const TokenCounter& tokenCounter) const {
    if (!tokenCounter || request.maximumTokens <= 0)
        return Result<PromptCompositionResult>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument,
            QStringLiteral("A token counter and a positive prompt budget are required.")));
    PromptCompositionResult result;
    result.maximumTokens = request.maximumTokens;
    QStringList sentences;
    const auto tryAppend = [&](const QString& sentence) {
        if (sentence.trimmed().isEmpty())
            return false;
        QStringList candidate = sentences;
        candidate.append(sentence.trimmed());
        const QString prompt = candidate.join(QLatin1Char(' '));
        if (tokenCounter(prompt) > request.maximumTokens)
            return false;
        sentences = candidate;
        return true;
    };
    QList<GlossaryTerm> terms = request.terms;
    std::stable_sort(terms.begin(), terms.end(), [](const GlossaryTerm& left, const GlossaryTerm& right) {
        if (left.priority != right.priority)
            return left.priority > right.priority;
        return left.canonicalText.localeAwareCompare(right.canonicalText) < 0;
    });
    for (const GlossaryTerm& term : terms) {
        if (!term.enabled)
            continue;
        QString sentence = QStringLiteral("Use the preferred term “%1”").arg(term.canonicalText);
        if (!term.aliases.isEmpty())
            sentence += QStringLiteral(" for variants %1").arg(term.aliases.join(QStringLiteral(", ")));
        sentence += QLatin1Char('.');
        if (tryAppend(sentence))
            result.includedTermIds.append(term.id);
        else
            result.omittedTermIds.append(term.id);
    }
    if (!request.meetingContext.trimmed().isEmpty())
        tryAppend(QStringLiteral("Meeting context: %1").arg(request.meetingContext));
    if (!request.profile.projectContext.trimmed().isEmpty())
        tryAppend(QStringLiteral("Project context: %1").arg(request.profile.projectContext));
    if (!request.previousConfirmedText.trimmed().isEmpty())
        tryAppend(QStringLiteral("Previous confirmed transcript: %1").arg(request.previousConfirmedText));
    result.prompt = sentences.join(QLatin1Char(' '));
    result.tokenCount = tokenCounter(result.prompt);
    return Result<PromptCompositionResult>::success(result);
}

} // namespace BreezeDesk
