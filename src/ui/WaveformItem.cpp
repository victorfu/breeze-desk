#include "breezedesk/ui/WaveformItem.h"

#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGSimpleRectNode>

#include <cmath>

namespace BreezeDesk {

WaveformItem::WaveformItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton);
}

QVariantList WaveformItem::peaks() const {
    QVariantList result;
    result.reserve(m_peaks.size());
    for (qreal value : m_peaks) {
        result.append(value);
    }
    return result;
}

qint64 WaveformItem::durationMs() const noexcept {
    return m_durationMs;
}
qint64 WaveformItem::positionMs() const noexcept {
    return m_positionMs;
}
qint64 WaveformItem::selectionStartMs() const noexcept {
    return m_selectionStartMs;
}
qint64 WaveformItem::selectionEndMs() const noexcept {
    return m_selectionEndMs;
}
QColor WaveformItem::waveformColor() const {
    return m_waveformColor;
}
QColor WaveformItem::cursorColor() const {
    return m_cursorColor;
}
QColor WaveformItem::selectionColor() const {
    return m_selectionColor;
}

void WaveformItem::setPeaks(const QVariantList& peaks) {
    QVector<qreal> values;
    values.reserve(peaks.size());
    for (const QVariant& peak : peaks) {
        values.append(qBound(0.0, peak.toReal(), 1.0));
    }
    if (m_peaks == values) {
        return;
    }
    m_peaks = std::move(values);
    update();
    emit peaksChanged();
}

void WaveformItem::setDurationMs(qint64 duration) {
    const qint64 bounded = qMax<qint64>(0, duration);
    if (m_durationMs != bounded) {
        m_durationMs = bounded;
        update();
        emit durationMsChanged();
    }
}

void WaveformItem::setPositionMs(qint64 position) {
    const qint64 bounded = qBound<qint64>(0, position, m_durationMs);
    if (m_positionMs != bounded) {
        m_positionMs = bounded;
        update();
        emit positionMsChanged();
    }
}

void WaveformItem::setSelectionStartMs(qint64 position) {
    const qint64 bounded = qBound<qint64>(0, position, m_durationMs);
    if (m_selectionStartMs != bounded) {
        m_selectionStartMs = bounded;
        if (m_selectionEndMs < bounded) {
            m_selectionEndMs = bounded;
        }
        update();
        emit selectionChanged();
    }
}

void WaveformItem::setSelectionEndMs(qint64 position) {
    const qint64 bounded = qBound(m_selectionStartMs, position, m_durationMs);
    if (m_selectionEndMs != bounded) {
        m_selectionEndMs = bounded;
        update();
        emit selectionChanged();
    }
}

void WaveformItem::setWaveformColor(const QColor& color) {
    if (color.isValid() && m_waveformColor != color) {
        m_waveformColor = color;
        update();
        emit colorsChanged();
    }
}

void WaveformItem::setCursorColor(const QColor& color) {
    if (color.isValid() && m_cursorColor != color) {
        m_cursorColor = color;
        update();
        emit colorsChanged();
    }
}

void WaveformItem::setSelectionColor(const QColor& color) {
    if (color.isValid() && m_selectionColor != color) {
        m_selectionColor = color;
        update();
        emit colorsChanged();
    }
}

QSGNode* WaveformItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    delete oldNode;
    auto* root = new QSGNode;
    if (width() <= 0.0 || height() <= 0.0) {
        return root;
    }

    // Snap logical coordinates onto the physical pixel grid so 1-2px features
    // (bar gaps, the playhead) stay crisp on fractional and 2x display scales.
    const qreal dpr = window() != nullptr ? window()->effectiveDevicePixelRatio() : 1.0;
    const auto snap = [dpr](qreal value) { return std::round(value * dpr) / dpr; };

    if (m_durationMs > 0 && m_selectionEndMs > m_selectionStartMs) {
        const qreal x =
            snap(width() * static_cast<qreal>(m_selectionStartMs) / static_cast<qreal>(m_durationMs));
        const qreal endX =
            snap(width() * static_cast<qreal>(m_selectionEndMs) / static_cast<qreal>(m_durationMs));
        root->appendChildNode(new QSGSimpleRectNode(QRectF(x, 0.0, endX - x, height()), m_selectionColor));
    }

    if (!m_peaks.isEmpty()) {
        const int visibleBars =
            qMin(static_cast<int>(m_peaks.size()), qMax(1, static_cast<int>(width() / 2.0)));
        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), visibleBars * 6);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        auto* vertices = geometry->vertexDataAsPoint2D();
        const qreal barSlot = width() / static_cast<qreal>(visibleBars);
        const qreal barWidth = qMax<qreal>(1.0, barSlot * 0.65);
        const qreal minBarWidth = 1.0 / dpr;
        for (int bar = 0; bar < visibleBars; ++bar) {
            const int source = static_cast<int>((static_cast<qint64>(bar) * m_peaks.size()) / visibleBars);
            const qreal amplitude = m_peaks.at(qMin(source, m_peaks.size() - 1));
            const qreal halfHeight = qMax<qreal>(1.0, amplitude * height() * 0.48);
            const qreal x1 = snap(bar * barSlot + (barSlot - barWidth) / 2.0);
            const qreal x2 = qMax(x1 + minBarWidth, snap(x1 + barWidth));
            const qreal y1 = snap(height() / 2.0 - halfHeight);
            const qreal y2 = snap(height() / 2.0 + halfHeight);
            const int offset = bar * 6;
            vertices[offset].set(static_cast<float>(x1), static_cast<float>(y1));
            vertices[offset + 1].set(static_cast<float>(x2), static_cast<float>(y1));
            vertices[offset + 2].set(static_cast<float>(x1), static_cast<float>(y2));
            vertices[offset + 3].set(static_cast<float>(x1), static_cast<float>(y2));
            vertices[offset + 4].set(static_cast<float>(x2), static_cast<float>(y1));
            vertices[offset + 5].set(static_cast<float>(x2), static_cast<float>(y2));
        }
        auto* material = new QSGFlatColorMaterial;
        material->setColor(m_waveformColor);
        auto* node = new QSGGeometryNode;
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(material);
        node->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(node);
    }

    if (m_durationMs > 0) {
        const qreal cursorX =
            snap(width() * static_cast<qreal>(m_positionMs) / static_cast<qreal>(m_durationMs));
        root->appendChildNode(
            new QSGSimpleRectNode(QRectF(snap(cursorX - 1.0), 0.0, 2.0, height()), m_cursorColor));
    }
    return root;
}

void WaveformItem::mousePressEvent(QMouseEvent* event) {
    m_selecting = true;
    m_dragAnchorMs = positionForX(event->position().x());
    m_selectionStartMs = m_dragAnchorMs;
    m_selectionEndMs = m_dragAnchorMs;
    update();
    event->accept();
}

void WaveformItem::mouseMoveEvent(QMouseEvent* event) {
    if (!m_selecting) {
        return;
    }
    const qint64 current = positionForX(event->position().x());
    m_selectionStartMs = qMin(m_dragAnchorMs, current);
    m_selectionEndMs = qMax(m_dragAnchorMs, current);
    update();
    emit selectionChanged();
    event->accept();
}

void WaveformItem::mouseReleaseEvent(QMouseEvent* event) {
    if (!m_selecting) {
        return;
    }
    m_selecting = false;
    const qint64 current = positionForX(event->position().x());
    if (qAbs(current - m_dragAnchorMs) < 100) {
        m_selectionStartMs = 0;
        m_selectionEndMs = 0;
        emit seekRequested(current);
    } else {
        m_selectionStartMs = qMin(m_dragAnchorMs, current);
        m_selectionEndMs = qMax(m_dragAnchorMs, current);
        emit selectionRequested(m_selectionStartMs, m_selectionEndMs);
    }
    update();
    emit selectionChanged();
    event->accept();
}

qint64 WaveformItem::positionForX(qreal x) const {
    if (width() <= 0.0 || m_durationMs <= 0) {
        return 0;
    }
    const qreal ratio = qBound(0.0, x / width(), 1.0);
    return static_cast<qint64>(ratio * static_cast<qreal>(m_durationMs));
}

} // namespace BreezeDesk
