#pragma once

#include "breezedesk/glossary/IGlossaryRepository.h"

namespace BreezeDesk {

class DatabaseManager;

class SqliteGlossaryRepository final : public IGlossaryRepository {
  public:
    explicit SqliteGlossaryRepository(DatabaseManager& databaseManager);

    [[nodiscard]] Result<QString> createProfile(GlossaryProfile profile) override;
    [[nodiscard]] Result<void> updateProfile(const GlossaryProfile& profile) override;
    [[nodiscard]] Result<void> deleteProfile(const QString& profileId) override;
    [[nodiscard]] Result<QString> duplicateProfile(const QString& profileId, const QString& newName) override;
    [[nodiscard]] Result<std::optional<GlossaryProfile>> profile(const QString& profileId) const override;
    [[nodiscard]] Result<QList<GlossaryProfile>> profiles() const override;
    [[nodiscard]] Result<QString> createTerm(GlossaryTerm term) override;
    [[nodiscard]] Result<void> updateTerm(const GlossaryTerm& term) override;
    [[nodiscard]] Result<void> deleteTerm(const QString& termId) override;
    [[nodiscard]] Result<QList<GlossaryTerm>> terms(const QString& profileId,
                                                    const QString& search = {}) const override;
    [[nodiscard]] Result<void> setTermsEnabled(const QStringList& termIds, bool enabled) override;

  private:
    DatabaseManager& m_databaseManager;
};

} // namespace BreezeDesk
