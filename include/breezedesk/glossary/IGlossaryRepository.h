#pragma once

#include "breezedesk/core/Result.h"
#include "breezedesk/glossary/Glossary.h"

#include <optional>

namespace BreezeDesk {

class IGlossaryRepository {
  public:
    virtual ~IGlossaryRepository() = default;

    [[nodiscard]] virtual Result<QString> createProfile(GlossaryProfile profile) = 0;
    [[nodiscard]] virtual Result<void> updateProfile(const GlossaryProfile& profile) = 0;
    [[nodiscard]] virtual Result<void> deleteProfile(const QString& profileId) = 0;
    [[nodiscard]] virtual Result<QString> duplicateProfile(const QString& profileId,
                                                           const QString& newName) = 0;
    [[nodiscard]] virtual Result<std::optional<GlossaryProfile>> profile(const QString& profileId) const = 0;
    [[nodiscard]] virtual Result<QList<GlossaryProfile>> profiles() const = 0;

    [[nodiscard]] virtual Result<QString> createTerm(GlossaryTerm term) = 0;
    [[nodiscard]] virtual Result<void> updateTerm(const GlossaryTerm& term) = 0;
    [[nodiscard]] virtual Result<void> deleteTerm(const QString& termId) = 0;
    [[nodiscard]] virtual Result<QList<GlossaryTerm>> terms(const QString& profileId,
                                                            const QString& search = {}) const = 0;
    [[nodiscard]] virtual Result<void> setTermsEnabled(const QStringList& termIds, bool enabled) = 0;
};

} // namespace BreezeDesk
