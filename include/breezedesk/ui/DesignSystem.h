#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace BreezeDesk {

class DesignSystem final : public QObject {
    Q_OBJECT
    Q_PROPERTY(Theme theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale NOTIFY textScaleChanged)
    Q_PROPERTY(bool compact READ compact WRITE setCompact NOTIFY compactChanged)
    Q_PROPERTY(QString systemFontFamily READ systemFontFamily CONSTANT)
    Q_PROPERTY(QString systemFixedFontFamily READ systemFixedFontFamily CONSTANT)

  public:
    enum class Theme { System, Light, Dark };
    Q_ENUM(Theme)

    explicit DesignSystem(QObject* parent = nullptr);

    [[nodiscard]] Theme theme() const noexcept;
    [[nodiscard]] bool dark() const noexcept;
    [[nodiscard]] qreal textScale() const noexcept;
    [[nodiscard]] bool compact() const noexcept;
    [[nodiscard]] QString systemFontFamily() const;
    [[nodiscard]] QString systemFixedFontFamily() const;

    // QML's `font` grouped property only exposes a single `family`, so a glyph
    // fallback chain can only be installed from C++ onto the application font.
    // Call once during startup, before any QML is loaded.
    static void applyApplicationFontFallback();
    [[nodiscard]] static QStringList uiFontFamilies();
    [[nodiscard]] static QStringList fixedFontFamilies();

  public slots:
    void setTheme(Theme theme);
    void setTextScale(qreal scale);
    void setCompact(bool compact);

  signals:
    void themeChanged();
    void darkChanged();
    void textScaleChanged();
    void compactChanged();

  private:
    void updateSystemColorScheme();

    Theme m_theme{Theme::Light};
    bool m_systemDark{false};
    qreal m_textScale{1.0};
    bool m_compact{false};
};

} // namespace BreezeDesk
