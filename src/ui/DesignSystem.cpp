#include "breezedesk/ui/DesignSystem.h"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QStyleHints>
#include <QtGlobal>

namespace BreezeDesk {

DesignSystem::DesignSystem(QObject* parent) : QObject(parent) {
    updateSystemColorScheme();
    if (auto* styleHints = QGuiApplication::styleHints()) {
        connect(styleHints, &QStyleHints::colorSchemeChanged, this, [this] {
            const bool wasDark = dark();
            updateSystemColorScheme();
            if (m_theme == Theme::System && wasDark != dark()) {
                emit darkChanged();
            }
        });
    }
}

DesignSystem::Theme DesignSystem::theme() const noexcept {
    return m_theme;
}

bool DesignSystem::dark() const noexcept {
    if (m_theme == Theme::Light) {
        return false;
    }
    if (m_theme == Theme::Dark) {
        return true;
    }
    return m_systemDark;
}

qreal DesignSystem::textScale() const noexcept {
    return m_textScale;
}

bool DesignSystem::compact() const noexcept {
    return m_compact;
}

QString DesignSystem::systemFontFamily() const {
    const QString family = QGuiApplication::font().family().trimmed();
    if (family.compare(QStringLiteral("Sans Serif"), Qt::CaseInsensitive) == 0) {
        return {};
    }
    return family;
}

QString DesignSystem::systemFixedFontFamily() const {
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

namespace {

// The Latin UI fonts shipped by every desktop platform (Segoe UI, Helvetica
// Neue, Cantarell) carry no CJK glyphs.  Naming the platform's CJK face
// explicitly keeps Chinese text on a predictable, well-hinted family instead of
// whatever Qt's generic fallback happens to resolve first.
QStringList cjkFallbackFamilies() {
#if defined(Q_OS_MACOS)
    return {QStringLiteral("PingFang TC"), QStringLiteral("PingFang SC"),
            QStringLiteral("Heiti TC")};
#elif defined(Q_OS_WIN)
    return {QStringLiteral("Microsoft JhengHei UI"), QStringLiteral("Microsoft YaHei UI"),
            QStringLiteral("Microsoft JhengHei")};
#else
    return {QStringLiteral("Noto Sans CJK TC"), QStringLiteral("Noto Sans CJK SC"),
            QStringLiteral("WenQuanYi Micro Hei")};
#endif
}

QStringList cjkFixedFallbackFamilies() {
#if defined(Q_OS_MACOS)
    return {QStringLiteral("PingFang TC"), QStringLiteral("Heiti TC")};
#elif defined(Q_OS_WIN)
    return {QStringLiteral("Microsoft JhengHei UI"), QStringLiteral("Microsoft YaHei UI")};
#else
    return {QStringLiteral("Noto Sans Mono CJK TC"), QStringLiteral("Noto Sans CJK TC")};
#endif
}

QStringList withFallbacks(const QString& primary, const QStringList& fallbacks) {
    QStringList families;
    if (!primary.isEmpty()) {
        families.append(primary);
    }
    for (const QString& family : fallbacks) {
        if (!families.contains(family, Qt::CaseInsensitive)) {
            families.append(family);
        }
    }
    return families;
}

} // namespace

QStringList DesignSystem::uiFontFamilies() {
    return withFallbacks(QGuiApplication::font().family().trimmed(), cjkFallbackFamilies());
}

QStringList DesignSystem::fixedFontFamilies() {
    return withFallbacks(QFontDatabase::systemFont(QFontDatabase::FixedFont).family(),
                         cjkFixedFallbackFamilies());
}

void DesignSystem::applyApplicationFontFallback() {
    QFont font = QGuiApplication::font();
    font.setFamilies(uiFontFamilies());
    QGuiApplication::setFont(font);
}

void DesignSystem::setTheme(Theme theme) {
    if (m_theme == theme) {
        return;
    }
    const bool wasDark = dark();
    m_theme = theme;
    emit themeChanged();
    if (wasDark != dark()) {
        emit darkChanged();
    }
}

void DesignSystem::setTextScale(qreal scale) {
    const qreal bounded = qBound(0.8, scale, 1.5);
    if (qFuzzyCompare(m_textScale, bounded)) {
        return;
    }
    m_textScale = bounded;
    emit textScaleChanged();
}

void DesignSystem::setCompact(bool compact) {
    if (m_compact == compact) {
        return;
    }
    m_compact = compact;
    emit compactChanged();
}

void DesignSystem::updateSystemColorScheme() {
    if (const auto* styleHints = QGuiApplication::styleHints()) {
        m_systemDark = styleHints->colorScheme() == Qt::ColorScheme::Dark;
    }
}

} // namespace BreezeDesk
