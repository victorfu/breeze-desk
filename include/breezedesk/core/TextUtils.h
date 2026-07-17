#pragma once

#include <QString>
#include <QStringList>

namespace BreezeDesk::TextUtils {

[[nodiscard]] QString normalizedForMatching(const QString& text);
[[nodiscard]] QStringList wordAndCharacterTokens(const QString& text);
[[nodiscard]] QString csvField(const QString& text);
[[nodiscard]] QString sanitizedFileName(const QString& name,
                                        const QString& fallback = QStringLiteral("export"));
[[nodiscard]] QString normalizedLineEndings(const QString& text);

} // namespace BreezeDesk::TextUtils
