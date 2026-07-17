#include <breezedesk/asr/OverlapDeduplicator.h>

#include <QtCore/QList>

namespace BreezeDesk::Asr {
namespace {

struct Unit {
    QString normalized;
    qsizetype start = 0;
    qsizetype end = 0;
    bool cjk = false;
};

bool isCjk(char32_t codePoint) {
    return (codePoint >= 0x3400 && codePoint <= 0x4DBF) || (codePoint >= 0x4E00 && codePoint <= 0x9FFF) ||
           (codePoint >= 0xF900 && codePoint <= 0xFAFF) || (codePoint >= 0x20000 && codePoint <= 0x3134F);
}

QList<Unit> units(const QString& text) {
    QList<Unit> result;
    QString word;
    qsizetype wordStart = -1;

    const auto flushWord = [&](qsizetype end, QList<Unit>* destination, QString* buffer, qsizetype* start) {
        if (!buffer->isEmpty()) {
            destination->append({buffer->toCaseFolded(), *start, end, false});
            buffer->clear();
            *start = -1;
        }
    };

    for (qsizetype index = 0; index < text.size();) {
        const QChar first = text.at(index);
        const bool surrogate =
            first.isHighSurrogate() && index + 1 < text.size() && text.at(index + 1).isLowSurrogate();
        const char32_t codePoint =
            surrogate ? QChar::surrogateToUcs4(first, text.at(index + 1)) : first.unicode();
        const qsizetype width = surrogate ? 2 : 1;
        if (isCjk(codePoint)) {
            flushWord(index, &result, &word, &wordStart);
            result.append({text.mid(index, width), index, index + width, true});
        } else if (first.isLetterOrNumber() || first == QLatin1Char('_')) {
            if (word.isEmpty()) {
                wordStart = index;
            }
            word.append(text.mid(index, width));
        } else {
            flushWord(index, &result, &word, &wordStart);
        }
        index += width;
    }
    flushWord(text.size(), &result, &word, &wordStart);
    return result;
}

} // namespace

DeduplicationResult OverlapDeduplicator::deduplicate(const QString& previousText, const QString& incomingText,
                                                     bool timestampsOverlap) {
    DeduplicationResult result{incomingText, 0, false, {}};
    if (!timestampsOverlap || previousText.isEmpty() || incomingText.isEmpty()) {
        return result;
    }
    const auto previous = units(previousText);
    const auto incoming = units(incomingText);
    const qsizetype maximum = std::min(previous.size(), incoming.size());
    qsizetype matched = 0;
    for (qsizetype length = maximum; length > 0; --length) {
        bool equal = true;
        for (qsizetype index = 0; index < length; ++index) {
            if (previous.at(previous.size() - length + index).normalized != incoming.at(index).normalized) {
                equal = false;
                break;
            }
        }
        if (equal) {
            matched = length;
            break;
        }
    }

    if (matched < 2) {
        result.ambiguous = matched == 1;
        if (result.ambiguous) {
            result.diagnostic =
                QStringLiteral("A one-unit overlap was retained because it is not sufficiently certain");
        }
        return result;
    }

    qsizetype cut = incoming.at(matched - 1).end;
    while (cut < incomingText.size() && (incomingText.at(cut).isSpace() || incomingText.at(cut).isPunct())) {
        ++cut;
    }
    result.text = incomingText.mid(cut);
    result.matchedUnits = matched;
    result.diagnostic = QStringLiteral("Removed %1 deterministic overlap units").arg(matched);
    return result;
}

} // namespace BreezeDesk::Asr
