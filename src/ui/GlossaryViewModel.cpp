#include "breezedesk/ui/GlossaryViewModel.h"

#include "breezedesk/glossary/GlossarySerializer.h"
#include "breezedesk/glossary/IGlossaryRepository.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

namespace BreezeDesk {
namespace {

QString errorMessage(const UserFacingError& error) {
    return error.message.isEmpty() ? error.diagnosticString() : error.message;
}

GlossaryProfileListModel::Profile uiProfile(const GlossaryProfile& profile, int termCount) {
    return {profile.id, profile.name, profile.description, profile.projectContext, termCount};
}

GlossaryTermListModel::Term uiTerm(const GlossaryTerm& term) {
    return {term.id,       term.profileId, term.canonicalText, term.aliases, term.category,
            term.language, term.priority,  term.caseSensitive, term.enabled, term.notes};
}

} // namespace

GlossaryProfileListModel::GlossaryProfileListModel(QObject* parent) : QAbstractListModel(parent) {}

int GlossaryProfileListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_profiles.size());
}

QVariant GlossaryProfileListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_profiles.size()) {
        return {};
    }
    const Profile& item = m_profiles.at(index.row());
    switch (role) {
    case IdRole:
        return item.id;
    case NameRole:
        return item.name;
    case DescriptionRole:
        return item.description;
    case ProjectContextRole:
        return item.projectContext;
    case TermCountRole:
        return item.termCount;
    default:
        return {};
    }
}

QHash<int, QByteArray> GlossaryProfileListModel::roleNames() const {
    return {{IdRole, "profileId"},
            {NameRole, "name"},
            {DescriptionRole, "description"},
            {ProjectContextRole, "projectContext"},
            {TermCountRole, "termCount"}};
}

QString GlossaryProfileListModel::add(const QString& name, const QString& description,
                                      const QString& context) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    Profile item{QUuid::createUuid().toString(QUuid::WithoutBraces), trimmed, description.trimmed(),
                 context.trimmed(), 0};
    const int insertionRow = static_cast<int>(m_profiles.size());
    beginInsertRows({}, insertionRow, insertionRow);
    m_profiles.append(item);
    endInsertRows();
    return item.id;
}

QString GlossaryProfileListModel::duplicate(const QString& id) {
    const int row = indexOf(id);
    if (row < 0) {
        return {};
    }
    const Profile original = m_profiles.at(row);
    return add(tr("%1 Copy").arg(original.name), original.description, original.projectContext);
}

bool GlossaryProfileListModel::remove(const QString& id) {
    const int row = indexOf(id);
    if (row < 0) {
        return false;
    }
    beginRemoveRows({}, row, row);
    m_profiles.removeAt(row);
    endRemoveRows();
    return true;
}

bool GlossaryProfileListModel::rename(const QString& id, const QString& name) {
    const int row = indexOf(id);
    const QString trimmed = name.trimmed();
    if (row < 0 || trimmed.isEmpty()) {
        return false;
    }
    m_profiles[row].name = trimmed;
    emit dataChanged(index(row), index(row), {NameRole});
    return true;
}

QString GlossaryProfileListModel::firstId() const {
    return m_profiles.isEmpty() ? QString{} : m_profiles.constFirst().id;
}

QVariantMap GlossaryProfileListModel::profile(const QString& id) const {
    const int row = indexOf(id);
    if (row < 0) {
        return {};
    }
    const Profile& item = m_profiles.at(row);
    return {{"id", item.id},
            {"name", item.name},
            {"description", item.description},
            {"projectContext", item.projectContext},
            {"termCount", item.termCount}};
}

void GlossaryProfileListModel::adjustTermCount(const QString& id, int delta) {
    const int row = indexOf(id);
    if (row < 0) {
        return;
    }
    m_profiles[row].termCount = qMax(0, m_profiles.at(row).termCount + delta);
    emit dataChanged(index(row), index(row), {TermCountRole});
}

void GlossaryProfileListModel::replaceProfiles(QList<Profile> profiles) {
    beginResetModel();
    m_profiles = std::move(profiles);
    endResetModel();
}

int GlossaryProfileListModel::indexOf(const QString& id) const {
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

GlossaryTermListModel::GlossaryTermListModel(QObject* parent) : QAbstractListModel(parent) {}

int GlossaryTermListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_terms.size());
}

QVariant GlossaryTermListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_terms.size()) {
        return {};
    }
    const Term& item = m_terms.at(index.row());
    switch (role) {
    case IdRole:
        return item.id;
    case ProfileIdRole:
        return item.profileId;
    case CanonicalTextRole:
        return item.canonicalText;
    case AliasesRole:
        return item.aliases;
    case CategoryRole:
        return item.category;
    case LanguageRole:
        return item.language;
    case PriorityRole:
        return item.priority;
    case CaseSensitiveRole:
        return item.caseSensitive;
    case EnabledRole:
        return item.enabled;
    case NotesRole:
        return item.notes;
    default:
        return {};
    }
}

bool GlossaryTermListModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_terms.size()) {
        return false;
    }
    Term& item = m_terms[index.row()];
    switch (role) {
    case EnabledRole:
        item.enabled = value.toBool();
        break;
    case CanonicalTextRole:
        if (value.toString().trimmed().isEmpty())
            return false;
        item.canonicalText = value.toString().trimmed();
        break;
    case AliasesRole:
        item.aliases = value.toStringList();
        break;
    case PriorityRole:
        item.priority = qBound(0, value.toInt(), 100);
        break;
    case NotesRole:
        item.notes = value.toString();
        break;
    default:
        return false;
    }
    emit dataChanged(index, index, {role});
    return true;
}

QHash<int, QByteArray> GlossaryTermListModel::roleNames() const {
    return {{IdRole, "termId"},
            {ProfileIdRole, "profileId"},
            {CanonicalTextRole, "canonicalText"},
            {AliasesRole, "aliases"},
            {CategoryRole, "category"},
            {LanguageRole, "language"},
            {PriorityRole, "priority"},
            {CaseSensitiveRole, "caseSensitive"},
            {EnabledRole, "termEnabled"},
            {NotesRole, "notes"}};
}

QString GlossaryTermListModel::add(const Term& term) {
    if (term.profileId.isEmpty() || term.canonicalText.trimmed().isEmpty()) {
        return {};
    }
    Term item = term;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.canonicalText = item.canonicalText.trimmed();
    item.priority = qBound(0, item.priority, 100);
    const int insertionRow = static_cast<int>(m_terms.size());
    beginInsertRows({}, insertionRow, insertionRow);
    m_terms.append(item);
    endInsertRows();
    return item.id;
}

bool GlossaryTermListModel::remove(const QString& id) {
    const int row = indexOf(id);
    if (row < 0) {
        return false;
    }
    beginRemoveRows({}, row, row);
    m_terms.removeAt(row);
    endRemoveRows();
    return true;
}

bool GlossaryTermListModel::setEnabled(const QString& id, bool enabled) {
    const int row = indexOf(id);
    return row >= 0 && setData(index(row), enabled, EnabledRole);
}

int GlossaryTermListModel::removeProfileTerms(const QString& profileId) {
    int removed = 0;
    for (int row = static_cast<int>(m_terms.size()) - 1; row >= 0; --row) {
        if (m_terms.at(row).profileId == profileId) {
            beginRemoveRows({}, row, row);
            m_terms.removeAt(row);
            endRemoveRows();
            ++removed;
        }
    }
    return removed;
}

void GlossaryTermListModel::replaceTerms(QList<Term> terms) {
    beginResetModel();
    m_terms = std::move(terms);
    endResetModel();
}

int GlossaryTermListModel::indexOf(const QString& id) const {
    for (int i = 0; i < m_terms.size(); ++i) {
        if (m_terms.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

GlossaryTermFilterProxyModel::GlossaryTermFilterProxyModel(QObject* parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
}

void GlossaryTermFilterProxyModel::setProfileId(const QString& profileId) {
    if (m_profileId != profileId) {
        m_profileId = profileId;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
    }
}

void GlossaryTermFilterProxyModel::setQuery(const QString& query) {
    if (m_query != query) {
        m_query = query;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
    }
}

bool GlossaryTermFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    const QModelIndex item = sourceModel()->index(sourceRow, 0, sourceParent);
    if (sourceModel()->data(item, GlossaryTermListModel::ProfileIdRole).toString() != m_profileId) {
        return false;
    }
    if (m_query.isEmpty()) {
        return true;
    }
    const QString canonical = sourceModel()->data(item, GlossaryTermListModel::CanonicalTextRole).toString();
    const QStringList aliases = sourceModel()->data(item, GlossaryTermListModel::AliasesRole).toStringList();
    return canonical.contains(m_query, Qt::CaseInsensitive) ||
           aliases.join(QLatin1Char(' ')).contains(m_query, Qt::CaseInsensitive);
}

GlossaryViewModel::GlossaryViewModel(QObject* parent) : QObject(parent), m_termProxy(this) {
    m_termProxy.setSourceModel(&m_terms);
    connect(&m_terms, &QAbstractItemModel::rowsInserted, this, &GlossaryViewModel::rebuildPromptPreview);
    connect(&m_terms, &QAbstractItemModel::rowsRemoved, this, &GlossaryViewModel::rebuildPromptPreview);
    connect(&m_terms, &QAbstractItemModel::dataChanged, this, &GlossaryViewModel::rebuildPromptPreview);
    connect(&m_terms, &QAbstractItemModel::modelReset, this, &GlossaryViewModel::rebuildPromptPreview);
}

void GlossaryViewModel::installRepository(IGlossaryRepository* repository) {
    m_repository = repository;
    if (m_repository != nullptr) {
        reloadProfiles(m_selectedProfileId);
    }
}

QAbstractItemModel* GlossaryViewModel::profiles() noexcept {
    return &m_profiles;
}
QAbstractItemModel* GlossaryViewModel::terms() noexcept {
    return &m_termProxy;
}
QString GlossaryViewModel::selectedProfileId() const {
    return m_selectedProfileId;
}
QString GlossaryViewModel::termSearch() const {
    return m_termSearch;
}
QString GlossaryViewModel::promptPreview() const {
    return m_promptPreview;
}
int GlossaryViewModel::promptTokenCount() const noexcept {
    return m_promptTokenCount;
}
int GlossaryViewModel::promptTokenMaximum() const noexcept {
    return PromptTokenMaximum;
}

QString GlossaryViewModel::createProfile(const QString& name, const QString& description,
                                         const QString& context) {
    if (m_repository != nullptr) {
        GlossaryProfile profile;
        profile.name = name.trimmed();
        profile.description = description.trimmed();
        profile.projectContext = context.trimmed();
        const auto result = m_repository->createProfile(profile);
        if (!result) {
            emit validationError(errorMessage(result.error()));
            return {};
        }
        reloadProfiles(result.value());
        return result.value();
    }
    const QString id = m_profiles.add(name, description, context);
    if (id.isEmpty()) {
        emit validationError(tr("Profile name cannot be empty."));
    } else {
        setSelectedProfileId(id);
    }
    return id;
}

void GlossaryViewModel::duplicateProfile(const QString& id) {
    if (m_repository != nullptr) {
        const QVariantMap source = m_profiles.profile(id);
        if (source.isEmpty()) {
            emit validationError(tr("The glossary profile no longer exists."));
            return;
        }
        const auto result = m_repository->duplicateProfile(
            id, tr("%1 Copy").arg(source.value(QStringLiteral("name")).toString()));
        if (!result) {
            emit validationError(errorMessage(result.error()));
            return;
        }
        reloadProfiles(result.value());
        return;
    }
    const QString duplicateId = m_profiles.duplicate(id);
    if (!duplicateId.isEmpty()) {
        setSelectedProfileId(duplicateId);
    }
}

void GlossaryViewModel::deleteProfile(const QString& id) {
    if (m_repository != nullptr) {
        const auto result = m_repository->deleteProfile(id);
        if (!result) {
            emit validationError(errorMessage(result.error()));
            return;
        }
        reloadProfiles();
        return;
    }
    if (m_profiles.remove(id)) {
        m_terms.removeProfileTerms(id);
        setSelectedProfileId(m_profiles.firstId());
    }
}

QString GlossaryViewModel::addTerm(const QString& canonicalText, const QStringList& aliases, int priority) {
    if (m_repository != nullptr) {
        GlossaryTerm term;
        term.profileId = m_selectedProfileId;
        term.canonicalText = canonicalText.trimmed();
        term.aliases = aliases;
        term.language = QStringLiteral("zh");
        term.priority = qBound(0, priority, 100);
        const auto result = m_repository->createTerm(term);
        if (!result) {
            emit validationError(errorMessage(result.error()));
            return {};
        }
        reloadProfiles(m_selectedProfileId);
        return result.value();
    }
    GlossaryTermListModel::Term term;
    term.profileId = m_selectedProfileId;
    term.canonicalText = canonicalText;
    term.aliases = aliases;
    term.priority = priority;
    const QString id = m_terms.add(term);
    if (id.isEmpty()) {
        emit validationError(tr("Choose a profile and enter a canonical term."));
    } else {
        m_profiles.adjustTermCount(m_selectedProfileId, 1);
    }
    return id;
}

void GlossaryViewModel::deleteTerm(const QString& id) {
    if (m_repository != nullptr) {
        const auto result = m_repository->deleteTerm(id);
        if (!result) {
            emit validationError(errorMessage(result.error()));
            return;
        }
        reloadProfiles(m_selectedProfileId);
        return;
    }
    if (m_terms.remove(id)) {
        m_profiles.adjustTermCount(m_selectedProfileId, -1);
    }
}

void GlossaryViewModel::setTermEnabled(const QString& id, bool enabled) {
    if (m_repository != nullptr) {
        const auto result = m_repository->setTermsEnabled({id}, enabled);
        if (!result) {
            emit validationError(errorMessage(result.error()));
            return;
        }
        reloadTerms();
        return;
    }
    m_terms.setEnabled(id, enabled);
}
void GlossaryViewModel::importFile(const QUrl& file) {
    if (m_repository == nullptr) {
        emit importRequested(file);
        return;
    }
    QFile input(file.toLocalFile());
    if (!input.open(QIODevice::ReadOnly)) {
        emit validationError(tr("The glossary file could not be opened: %1").arg(input.errorString()));
        return;
    }
    const QByteArray data = input.readAll();
    const QString suffix = QFileInfo(input.fileName()).suffix().toLower();
    if (suffix == QLatin1String("json")) {
        auto documentResult = GlossarySerializer::fromJson(data);
        if (!documentResult) {
            emit validationError(errorMessage(documentResult.error()));
            return;
        }
        GlossaryDocument document = std::move(documentResult).value();
        document.profile.id.clear();
        auto profileResult = m_repository->createProfile(document.profile);
        if (!profileResult) {
            emit validationError(errorMessage(profileResult.error()));
            return;
        }
        QStringList createdTerms;
        for (GlossaryTerm term : std::as_const(document.terms)) {
            term.id.clear();
            term.profileId = profileResult.value();
            auto termResult = m_repository->createTerm(term);
            if (!termResult) {
                for (const QString& createdId : std::as_const(createdTerms)) {
                    const auto ignored = m_repository->deleteTerm(createdId);
                    Q_UNUSED(ignored)
                }
                const auto ignored = m_repository->deleteProfile(profileResult.value());
                Q_UNUSED(ignored)
                emit validationError(errorMessage(termResult.error()));
                return;
            }
            createdTerms.append(termResult.value());
        }
        reloadProfiles(profileResult.value());
        emit operationSucceeded(tr("Glossary profile imported."));
        return;
    }
    if (suffix == QLatin1String("csv")) {
        if (m_selectedProfileId.isEmpty()) {
            emit validationError(tr("Choose a glossary profile before importing CSV terms."));
            return;
        }
        auto termsResult = GlossarySerializer::termsFromCsv(data, m_selectedProfileId);
        if (!termsResult) {
            emit validationError(errorMessage(termsResult.error()));
            return;
        }
        QStringList createdTerms;
        for (GlossaryTerm term : termsResult.value()) {
            auto termResult = m_repository->createTerm(term);
            if (!termResult) {
                for (const QString& createdId : std::as_const(createdTerms)) {
                    const auto ignored = m_repository->deleteTerm(createdId);
                    Q_UNUSED(ignored)
                }
                emit validationError(errorMessage(termResult.error()));
                reloadProfiles(m_selectedProfileId);
                return;
            }
            createdTerms.append(termResult.value());
        }
        reloadProfiles(m_selectedProfileId);
        emit operationSucceeded(tr("Glossary terms imported."));
        return;
    }
    emit validationError(tr("Glossary import supports JSON and CSV files."));
}
void GlossaryViewModel::exportFile(const QUrl& file, const QString& format) {
    if (m_repository == nullptr) {
        emit exportRequested(file, format);
        return;
    }
    const auto profileResult = m_repository->profile(m_selectedProfileId);
    if (!profileResult) {
        emit validationError(errorMessage(profileResult.error()));
        return;
    }
    if (!profileResult.value()) {
        emit validationError(tr("Choose a glossary profile before exporting."));
        return;
    }
    const auto termsResult = m_repository->terms(m_selectedProfileId);
    if (!termsResult) {
        emit validationError(errorMessage(termsResult.error()));
        return;
    }
    QByteArray contents;
    const QString normalizedFormat = format.toLower();
    if (normalizedFormat == QLatin1String("json")) {
        contents = GlossarySerializer::toJson({*profileResult.value(), termsResult.value()});
    } else if (normalizedFormat == QLatin1String("csv")) {
        contents = GlossarySerializer::termsToCsv(termsResult.value());
    } else {
        emit validationError(tr("Glossary export supports JSON and CSV files."));
        return;
    }
    QSaveFile output(file.toLocalFile());
    if (!output.open(QIODevice::WriteOnly) || output.write(contents) != contents.size() || !output.commit()) {
        emit validationError(tr("The glossary export could not be saved: %1").arg(output.errorString()));
        return;
    }
    emit operationSucceeded(tr("Glossary exported."));
}

void GlossaryViewModel::setSelectedProfileId(const QString& id) {
    if (m_selectedProfileId == id) {
        return;
    }
    m_selectedProfileId = id;
    m_termProxy.setProfileId(id);
    emit selectedProfileIdChanged();
    if (m_repository != nullptr) {
        reloadTerms();
    } else {
        rebuildPromptPreview();
    }
}

void GlossaryViewModel::setTermSearch(const QString& text) {
    if (m_termSearch == text) {
        return;
    }
    m_termSearch = text;
    m_termProxy.setQuery(text);
    emit termSearchChanged();
}

bool GlossaryViewModel::reloadProfiles(const QString& preferredProfileId) {
    if (m_repository == nullptr) {
        return false;
    }
    const auto result = m_repository->profiles();
    if (!result) {
        emit validationError(errorMessage(result.error()));
        return false;
    }
    QList<GlossaryProfileListModel::Profile> profiles;
    profiles.reserve(result.value().size());
    for (const GlossaryProfile& profile : result.value()) {
        const auto termsResult = m_repository->terms(profile.id);
        if (!termsResult) {
            emit validationError(errorMessage(termsResult.error()));
            return false;
        }
        profiles.append(uiProfile(profile, static_cast<int>(termsResult.value().size())));
    }
    m_profiles.replaceProfiles(std::move(profiles));
    QString selected = preferredProfileId.isEmpty() ? m_selectedProfileId : preferredProfileId;
    if (m_profiles.profile(selected).isEmpty()) {
        selected = m_profiles.firstId();
    }
    if (selected != m_selectedProfileId) {
        setSelectedProfileId(selected);
    } else {
        reloadTerms();
    }
    return true;
}

bool GlossaryViewModel::reloadTerms() {
    if (m_repository == nullptr) {
        return false;
    }
    if (m_selectedProfileId.isEmpty()) {
        m_terms.replaceTerms({});
        return true;
    }
    const auto result = m_repository->terms(m_selectedProfileId);
    if (!result) {
        emit validationError(errorMessage(result.error()));
        return false;
    }
    QList<GlossaryTermListModel::Term> terms;
    terms.reserve(result.value().size());
    for (const GlossaryTerm& term : result.value()) {
        terms.append(uiTerm(term));
    }
    m_terms.replaceTerms(std::move(terms));
    return true;
}

void GlossaryViewModel::rebuildPromptPreview() {
    const QVariantMap profile = m_profiles.profile(m_selectedProfileId);
    QStringList terms;
    for (int row = 0; row < m_termProxy.rowCount() && terms.size() < 40; ++row) {
        const QModelIndex item = m_termProxy.index(row, 0);
        if (m_termProxy.data(item, GlossaryTermListModel::EnabledRole).toBool()) {
            terms.append(m_termProxy.data(item, GlossaryTermListModel::CanonicalTextRole).toString());
        }
    }
    const QString context = profile.value("projectContext").toString();
    if (profile.isEmpty()) {
        m_promptPreview.clear();
    } else if (terms.isEmpty()) {
        m_promptPreview = context;
    } else {
        m_promptPreview =
            tr("Context: %1. Important terms: %2.").arg(context, terms.join(QStringLiteral(", ")));
    }
    m_promptTokenCount = qMin(PromptTokenMaximum, static_cast<int>((m_promptPreview.size() + 2) / 3));
    emit promptPreviewChanged();
}

} // namespace BreezeDesk
