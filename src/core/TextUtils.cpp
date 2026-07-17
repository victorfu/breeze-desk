#include "breezedesk/core/TextUtils.h"

#include <QRegularExpression>

namespace BreezeDesk::TextUtils {

QString normalizedForMatching(const QString& text) {
    QString value = text.normalized(QString::NormalizationForm_KC).toCaseFolded();
    static const QRegularExpression punctuation(QStringLiteral("[^\\p{L}\\p{N}]+"));
    value.replace(punctuation, QStringLiteral(" "));
    return value.simplified();
}

QStringList wordAndCharacterTokens(const QString& text) {
    const QString normalized = normalizedForMatching(text);
    QStringList tokens;
    QString latinRun;
    const auto flushLatin = [&tokens, &latinRun]() {
        if (!latinRun.isEmpty()) {
            tokens.append(latinRun);
            latinRun.clear();
        }
    };
    for (const QChar character : normalized) {
        if (character.isSpace()) {
            flushLatin();
        } else if (character.script() == QChar::Script_Han || character.script() == QChar::Script_Hiragana ||
                   character.script() == QChar::Script_Katakana) {
            flushLatin();
            tokens.append(character);
        } else {
            latinRun.append(character);
        }
    }
    flushLatin();
    return tokens;
}

QString csvField(const QString& text) {
    QString value = normalizedLineEndings(text);
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

QString sanitizedFileName(const QString& name, const QString& fallback) {
    QString value = name.normalized(QString::NormalizationForm_C).trimmed();
    static const QRegularExpression invalid(QStringLiteral("[\\x00-\\x1f<>:\"/\\\\|?*]+"));
    value.replace(invalid, QStringLiteral("_"));
    value.replace(QRegularExpression(QStringLiteral("[. ]+$")), QString());
    if (value.isEmpty() || value == QStringLiteral(".") || value == QStringLiteral("..")) {
        return fallback;
    }
    return value.left(180);
}

QString normalizedLineEndings(const QString& text) {
    QString value = text;
    value.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    value.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return value;
}

} // namespace BreezeDesk::TextUtils
