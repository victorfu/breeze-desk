#pragma once

#include <QColor>
#include <QQuickItem>
#include <QVariantList>

namespace BreezeDesk {

class WaveformItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList peaks READ peaks WRITE setPeaks NOTIFY peaksChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs WRITE setDurationMs NOTIFY durationMsChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs WRITE setPositionMs NOTIFY positionMsChanged)
    Q_PROPERTY(
        qint64 selectionStartMs READ selectionStartMs WRITE setSelectionStartMs NOTIFY selectionChanged)
    Q_PROPERTY(qint64 selectionEndMs READ selectionEndMs WRITE setSelectionEndMs NOTIFY selectionChanged)
    Q_PROPERTY(QColor waveformColor READ waveformColor WRITE setWaveformColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor cursorColor READ cursorColor WRITE setCursorColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor NOTIFY colorsChanged)

  public:
    explicit WaveformItem(QQuickItem* parent = nullptr);

    [[nodiscard]] QVariantList peaks() const;
    [[nodiscard]] qint64 durationMs() const noexcept;
    [[nodiscard]] qint64 positionMs() const noexcept;
    [[nodiscard]] qint64 selectionStartMs() const noexcept;
    [[nodiscard]] qint64 selectionEndMs() const noexcept;
    [[nodiscard]] QColor waveformColor() const;
    [[nodiscard]] QColor cursorColor() const;
    [[nodiscard]] QColor selectionColor() const;

  public slots:
    void setPeaks(const QVariantList& peaks);
    void setDurationMs(qint64 duration);
    void setPositionMs(qint64 position);
    void setSelectionStartMs(qint64 position);
    void setSelectionEndMs(qint64 position);
    void setWaveformColor(const QColor& color);
    void setCursorColor(const QColor& color);
    void setSelectionColor(const QColor& color);

  signals:
    void peaksChanged();
    void durationMsChanged();
    void positionMsChanged();
    void selectionChanged();
    void colorsChanged();
    void seekRequested(qint64 positionMs);
    void selectionRequested(qint64 startMs, qint64 endMs);

  protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

  private:
    [[nodiscard]] qint64 positionForX(qreal x) const;

    QVector<qreal> m_peaks;
    qint64 m_durationMs{0};
    qint64 m_positionMs{0};
    qint64 m_selectionStartMs{0};
    qint64 m_selectionEndMs{0};
    QColor m_waveformColor{"#4B70E2"};
    QColor m_cursorColor{"#E05252"};
    QColor m_selectionColor{"#554B70E2"};
    bool m_selecting{false};
    qint64 m_dragAnchorMs{0};
};

} // namespace BreezeDesk
