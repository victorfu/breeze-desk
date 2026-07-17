#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QSortFilterProxyModel>
#include <QUrl>

namespace BreezeDesk {

class IGlossaryRepository;

class GlossaryProfileListModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role { IdRole = Qt::UserRole + 1, NameRole, DescriptionRole, ProjectContextRole, TermCountRole };

    struct Profile {
        QString id;
        QString name;
        QString description;
        QString projectContext;
        int termCount{0};
    };

    explicit GlossaryProfileListModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    QString add(const QString& name, const QString& description, const QString& context);
    QString duplicate(const QString& id);
    bool remove(const QString& id);
    bool rename(const QString& id, const QString& name);
    [[nodiscard]] QString firstId() const;
    [[nodiscard]] QVariantMap profile(const QString& id) const;
    void adjustTermCount(const QString& id, int delta);
    void replaceProfiles(QList<Profile> profiles);

  private:
    [[nodiscard]] int indexOf(const QString& id) const;
    QList<Profile> m_profiles;
};

class GlossaryTermListModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        ProfileIdRole,
        CanonicalTextRole,
        AliasesRole,
        CategoryRole,
        LanguageRole,
        PriorityRole,
        CaseSensitiveRole,
        EnabledRole,
        NotesRole
    };

    struct Term {
        QString id;
        QString profileId;
        QString canonicalText;
        QStringList aliases;
        QString category;
        QString language{"zh"};
        int priority{50};
        bool caseSensitive{false};
        bool enabled{true};
        QString notes;
    };

    explicit GlossaryTermListModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    QString add(const Term& term);
    bool remove(const QString& id);
    bool setEnabled(const QString& id, bool enabled);
    int removeProfileTerms(const QString& profileId);
    void replaceTerms(QList<Term> terms);

  private:
    [[nodiscard]] int indexOf(const QString& id) const;
    QList<Term> m_terms;
};

class GlossaryTermFilterProxyModel final : public QSortFilterProxyModel {
    Q_OBJECT

  public:
    explicit GlossaryTermFilterProxyModel(QObject* parent = nullptr);
    void setProfileId(const QString& profileId);
    void setQuery(const QString& query);

  protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

  private:
    QString m_profileId;
    QString m_query;
};

class GlossaryViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* profiles READ profiles CONSTANT)
    Q_PROPERTY(QAbstractItemModel* terms READ terms CONSTANT)
    Q_PROPERTY(QString selectedProfileId READ selectedProfileId WRITE setSelectedProfileId NOTIFY
                   selectedProfileIdChanged)
    Q_PROPERTY(QString termSearch READ termSearch WRITE setTermSearch NOTIFY termSearchChanged)
    Q_PROPERTY(QString promptPreview READ promptPreview NOTIFY promptPreviewChanged)
    Q_PROPERTY(int promptTokenCount READ promptTokenCount NOTIFY promptPreviewChanged)
    Q_PROPERTY(int promptTokenMaximum READ promptTokenMaximum CONSTANT)

  public:
    explicit GlossaryViewModel(QObject* parent = nullptr);

    // The repository is owned by the application composition root and must
    // outlive this view model. A null repository keeps isolated QML behavior.
    void installRepository(IGlossaryRepository* repository);

    [[nodiscard]] QAbstractItemModel* profiles() noexcept;
    [[nodiscard]] QAbstractItemModel* terms() noexcept;
    [[nodiscard]] QString selectedProfileId() const;
    [[nodiscard]] QString termSearch() const;
    [[nodiscard]] QString promptPreview() const;
    [[nodiscard]] int promptTokenCount() const noexcept;
    [[nodiscard]] int promptTokenMaximum() const noexcept;

    Q_INVOKABLE QString createProfile(const QString& name, const QString& description,
                                      const QString& context);
    Q_INVOKABLE void duplicateProfile(const QString& id);
    Q_INVOKABLE void deleteProfile(const QString& id);
    Q_INVOKABLE QString addTerm(const QString& canonicalText, const QStringList& aliases, int priority);
    Q_INVOKABLE void deleteTerm(const QString& id);
    Q_INVOKABLE void setTermEnabled(const QString& id, bool enabled);
    Q_INVOKABLE void importFile(const QUrl& file);
    Q_INVOKABLE void exportFile(const QUrl& file, const QString& format);

  public slots:
    void setSelectedProfileId(const QString& id);
    void setTermSearch(const QString& text);

  signals:
    void selectedProfileIdChanged();
    void termSearchChanged();
    void promptPreviewChanged();
    void importRequested(const QUrl& file);
    void exportRequested(const QUrl& file, const QString& format);
    void validationError(const QString& message);
    void operationSucceeded(const QString& message);

  private:
    bool reloadProfiles(const QString& preferredProfileId = {});
    bool reloadTerms();
    void rebuildPromptPreview();

    GlossaryProfileListModel m_profiles;
    GlossaryTermListModel m_terms;
    GlossaryTermFilterProxyModel m_termProxy;
    IGlossaryRepository* m_repository{nullptr};
    QString m_selectedProfileId;
    QString m_termSearch;
    QString m_promptPreview;
    int m_promptTokenCount{0};
    static constexpr int PromptTokenMaximum = 512;
};

} // namespace BreezeDesk
