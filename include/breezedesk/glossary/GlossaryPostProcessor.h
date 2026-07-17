#pragma once

#include "breezedesk/glossary/Glossary.h"

#include <QJsonArray>

#include <optional>

namespace BreezeDesk {

struct GlossaryReplacement {
    QString termId;
    QString alias;
    QString canonicalText;
    QString originalText;
    qsizetype start = 0;
    qsizetype length = 0;
    double confidence = 1.0;
    bool applied = true;
};

struct GlossaryPostProcessResult {
    QString text;
    QList<GlossaryReplacement> replacements;
};

class GlossaryPostProcessor final {
  public:
    [[nodiscard]] GlossaryPostProcessResult applyExplicitAliases(const QString& text,
                                                                 const QList<GlossaryTerm>& terms) const;
    [[nodiscard]] QString revert(const QString& text, const QList<GlossaryReplacement>& replacements) const;
    [[nodiscard]] std::optional<QString> renderAudit(const QString& originalText,
                                                     const QList<GlossaryReplacement>& replacements) const;
    [[nodiscard]] static QJsonArray auditToJson(const QList<GlossaryReplacement>& replacements);
    [[nodiscard]] static QList<GlossaryReplacement> auditFromJson(const QJsonArray& json);
};

} // namespace BreezeDesk
