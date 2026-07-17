#include "breezedesk/glossary/GlossaryPostProcessor.h"

#include <QJsonObject>

#include <algorithm>

namespace BreezeDesk {
namespace {
bool isWordCharacter(const QChar character) {
    return character.isLetterOrNumber() || character == QLatin1Char('_');
}
bool boundariesAllow(const QString& text, const qsizetype start, const qsizetype length,
                     const QString& alias) {
    const bool latinLike = std::any_of(alias.cbegin(), alias.cend(),
                                       [](const QChar c) { return c.script() == QChar::Script_Latin; });
    if (!latinLike)
        return true;
    return (start == 0 || !isWordCharacter(text.at(start - 1))) &&
           (start + length == text.size() || !isWordCharacter(text.at(start + length)));
}
} // namespace

GlossaryPostProcessResult
GlossaryPostProcessor::applyExplicitAliases(const QString& text, const QList<GlossaryTerm>& terms) const {
    struct Candidate {
        QString termId;
        QString alias;
        QString canonical;
        bool caseSensitive;
        int priority;
    };
    QList<Candidate> candidates;
    for (const GlossaryTerm& term : terms) {
        if (!term.enabled)
            continue;
        for (const QString& alias : term.aliases) {
            if (!alias.isEmpty() && alias != term.canonicalText)
                candidates.append({term.id, alias, term.canonicalText, term.caseSensitive, term.priority});
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        if (left.alias.size() != right.alias.size())
            return left.alias.size() > right.alias.size();
        return left.priority > right.priority;
    });
    GlossaryPostProcessResult result{text, {}};
    for (const Candidate& candidate : candidates) {
        const Qt::CaseSensitivity sensitivity =
            candidate.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        qsizetype from = 0;
        while (from < result.text.size()) {
            const qsizetype position = result.text.indexOf(candidate.alias, from, sensitivity);
            if (position < 0)
                break;
            if (!boundariesAllow(result.text, position, candidate.alias.size(), candidate.alias)) {
                from = position + candidate.alias.size();
                continue;
            }
            const bool overlaps =
                std::any_of(result.replacements.cbegin(), result.replacements.cend(),
                            [position, &candidate](const GlossaryReplacement& replacement) {
                                return position < replacement.start + replacement.length &&
                                       replacement.start < position + candidate.alias.size();
                            });
            if (overlaps) {
                from = position + candidate.alias.size();
                continue;
            }
            const QString matchedText = result.text.mid(position, candidate.alias.size());
            result.text.replace(position, candidate.alias.size(), candidate.canonical);
            const qsizetype delta = candidate.canonical.size() - candidate.alias.size();
            for (GlossaryReplacement& replacement : result.replacements)
                if (replacement.start > position)
                    replacement.start += delta;
            result.replacements.append({candidate.termId, candidate.alias, candidate.canonical, matchedText,
                                        position, candidate.canonical.size(), 1.0, true});
            from = position + candidate.canonical.size();
        }
    }
    std::sort(result.replacements.begin(), result.replacements.end(),
              [](const auto& left, const auto& right) { return left.start < right.start; });
    return result;
}

QString GlossaryPostProcessor::revert(const QString& text,
                                      const QList<GlossaryReplacement>& replacements) const {
    QString result = text;
    QList<GlossaryReplacement> ordered = replacements;
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& left, const auto& right) { return left.start > right.start; });
    for (const GlossaryReplacement& replacement : ordered) {
        if (replacement.applied &&
            result.mid(replacement.start, replacement.length) == replacement.canonicalText)
            result.replace(replacement.start, replacement.length, replacement.originalText);
    }
    return result;
}

std::optional<QString>
GlossaryPostProcessor::renderAudit(const QString& originalText,
                                   const QList<GlossaryReplacement>& replacements) const {
    struct PositionedReplacement {
        GlossaryReplacement replacement;
        qsizetype originalStart = 0;
    };

    QList<GlossaryReplacement> ordered = replacements;
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& left, const auto& right) { return left.start < right.start; });

    QList<PositionedReplacement> positioned;
    positioned.reserve(ordered.size());
    qsizetype accumulatedDelta = 0;
    qsizetype previousOriginalEnd = 0;
    for (const GlossaryReplacement& replacement : ordered) {
        if (replacement.start < 0 || replacement.length != replacement.canonicalText.size() ||
            replacement.originalText.isEmpty()) {
            return std::nullopt;
        }
        const qsizetype originalStart = replacement.start - accumulatedDelta;
        const qsizetype originalEnd = originalStart + replacement.originalText.size();
        if (originalStart < previousOriginalEnd || originalEnd > originalText.size()) {
            return std::nullopt;
        }
        const QString sourceText = originalText.mid(originalStart, replacement.originalText.size());
        if (sourceText.compare(replacement.originalText, Qt::CaseInsensitive) != 0) {
            return std::nullopt;
        }
        positioned.append({replacement, originalStart});
        previousOriginalEnd = originalEnd;
        accumulatedDelta += replacement.canonicalText.size() - replacement.originalText.size();
    }

    QString rendered = originalText;
    for (auto iterator = positioned.crbegin(); iterator != positioned.crend(); ++iterator) {
        if (iterator->replacement.applied) {
            rendered.replace(iterator->originalStart, iterator->replacement.originalText.size(),
                             iterator->replacement.canonicalText);
        }
    }
    return rendered;
}

QJsonArray GlossaryPostProcessor::auditToJson(const QList<GlossaryReplacement>& replacements) {
    QJsonArray array;
    for (const auto& item : replacements) {
        array.append(QJsonObject{{QStringLiteral("termId"), item.termId},
                                 {QStringLiteral("alias"), item.alias},
                                 {QStringLiteral("canonicalText"), item.canonicalText},
                                 {QStringLiteral("originalText"), item.originalText},
                                 {QStringLiteral("start"), item.start},
                                 {QStringLiteral("length"), item.length},
                                 {QStringLiteral("confidence"), item.confidence},
                                 {QStringLiteral("applied"), item.applied}});
    }
    return array;
}

QList<GlossaryReplacement> GlossaryPostProcessor::auditFromJson(const QJsonArray& json) {
    QList<GlossaryReplacement> result;
    for (const QJsonValue& value : json) {
        const QJsonObject object = value.toObject();
        result.append({object.value(QStringLiteral("termId")).toString(),
                       object.value(QStringLiteral("alias")).toString(),
                       object.value(QStringLiteral("canonicalText")).toString(),
                       object.value(QStringLiteral("originalText")).toString(),
                       static_cast<qsizetype>(object.value(QStringLiteral("start")).toInteger()),
                       static_cast<qsizetype>(object.value(QStringLiteral("length")).toInteger()),
                       object.value(QStringLiteral("confidence")).toDouble(),
                       object.value(QStringLiteral("applied")).toBool()});
    }
    return result;
}

} // namespace BreezeDesk
