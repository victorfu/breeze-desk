#pragma once

#include "breezedesk/core/Result.h"
#include "breezedesk/glossary/Glossary.h"

#include <QByteArray>

namespace BreezeDesk {

struct GlossaryDocument {
    GlossaryProfile profile;
    QList<GlossaryTerm> terms;
};

class GlossarySerializer final {
  public:
    [[nodiscard]] static QByteArray toJson(const GlossaryDocument& document);
    [[nodiscard]] static Result<GlossaryDocument> fromJson(const QByteArray& data);
    [[nodiscard]] static QByteArray termsToCsv(const QList<GlossaryTerm>& terms, bool withBom = false);
    [[nodiscard]] static Result<QList<GlossaryTerm>> termsFromCsv(const QByteArray& data,
                                                                  const QString& profileId);
};

} // namespace BreezeDesk
