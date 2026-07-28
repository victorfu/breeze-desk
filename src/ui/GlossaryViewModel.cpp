#include "breezedesk/ui/GlossaryViewModel.h"

#include "breezedesk/glossary/IGlossaryRepository.h"

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
    m_profiles.replaceProfiles({{DefaultGlossaryProfileId, QStringLiteral("Glossary"), {}, {}, 0}});
    setSelectedProfileId(DefaultGlossaryProfileId);
}

void GlossaryViewModel::installRepository(IGlossaryRepository* repository) {
    m_repository = repository;
    if (m_repository != nullptr) {
        reloadProfiles();
    }
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
        m_profiles.adjustTermCount(m_selectedProfileId, 1);
        reloadTerms();
        return result.value();
    }
    GlossaryTermListModel::Term term;
    term.profileId = m_selectedProfileId;
    term.canonicalText = canonicalText;
    term.aliases = aliases;
    term.priority = priority;
    const QString id = m_terms.add(term);
    if (id.isEmpty()) {
        emit validationError(tr("Enter a canonical term."));
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
        m_profiles.adjustTermCount(m_selectedProfileId, -1);
        reloadTerms();
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
void GlossaryViewModel::setSelectedProfileId(const QString& id) {
    if (m_selectedProfileId == id) {
        return;
    }
    m_selectedProfileId = id;
    m_termProxy.setProfileId(id);
    if (m_repository != nullptr) {
        reloadTerms();
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

bool GlossaryViewModel::reloadProfiles() {
    if (m_repository == nullptr) {
        return false;
    }
    const auto result = m_repository->profiles();
    if (!result) {
        emit validationError(errorMessage(result.error()));
        return false;
    }
    QList<GlossaryProfileListModel::Profile> profiles;
    for (const GlossaryProfile& profile : result.value()) {
        if (profile.id != DefaultGlossaryProfileId) {
            continue;
        }
        const auto termsResult = m_repository->terms(profile.id);
        if (!termsResult) {
            emit validationError(errorMessage(termsResult.error()));
            return false;
        }
        profiles.append(uiProfile(profile, static_cast<int>(termsResult.value().size())));
    }
    m_profiles.replaceProfiles(std::move(profiles));
    const QString selected = m_profiles.firstId();
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

} // namespace BreezeDesk
