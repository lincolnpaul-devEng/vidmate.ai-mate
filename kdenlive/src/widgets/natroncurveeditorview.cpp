/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Interactive Natron Curve Editor View Implementation
 */

#include "natroncurveeditorview.h"
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>

NatronCurveEditorView::NatronCurveEditorView(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(120);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);

    // Initial default curves (e.g. Transform Opacity & Scale)
    QVector<CurveKeyframe> kfs1 = {
        {0.0, 0.0, -5.0, 0.0, 8.0, 0.3},
        {30.0, 1.0, -8.0, 0.0, 8.0, 0.0},
        {90.0, 0.8, -10.0, 0.0, 10.0, -0.4},
        {100.0, 0.0, -5.0, 0.0, 5.0, 0.0}
    };
    addCurve(QStringLiteral("Transform.opacity"), QColor(80, 200, 120), kfs1);

    QVector<CurveKeyframe> kfs2 = {
        {0.0, 0.5, -5.0, 0.0, 10.0, 0.1},
        {50.0, 1.2, -10.0, 0.0, 10.0, 0.0},
        {100.0, 1.0, -10.0, 0.0, 5.0, 0.0}
    };
    addCurve(QStringLiteral("Keyer.threshold"), QColor(240, 140, 60), kfs2);
}

void NatronCurveEditorView::addCurve(const QString &name, const QColor &color, const QVector<CurveKeyframe> &keyframes)
{
    m_curves.append({name, color, keyframes, true});
    update();
}

void NatronCurveEditorView::clearCurves()
{
    m_curves.clear();
    update();
}

void NatronCurveEditorView::setCurrentFrame(int frame)
{
    m_currentFrame = frame;
    update();
}

QPointF NatronCurveEditorView::frameValueToPixel(double frame, double value) const
{
    qreal margin = 40.0;
    qreal w = width() - margin * 2.0;
    qreal h = height() - margin * 2.0;

    qreal x = margin + ((frame - m_minFrame) / (m_maxFrame - m_minFrame)) * w;
    qreal y = height() - margin - ((value - m_minValue) / (m_maxValue - m_minValue)) * h;
    return QPointF(x, y);
}

QPointF NatronCurveEditorView::pixelToFrameValue(const QPointF &pixel) const
{
    qreal margin = 40.0;
    qreal w = width() - margin * 2.0;
    qreal h = height() - margin * 2.0;

    double frame = m_minFrame + ((pixel.x() - margin) / w) * (m_maxFrame - m_minFrame);
    double value = m_minValue + ((height() - margin - pixel.y()) / h) * (m_maxValue - m_minValue);
    return QPointF(frame, value);
}

void NatronCurveEditorView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Background
    painter.fillRect(rect(), QColor(24, 24, 26));

    qreal margin = 40.0;
    QRectF graphArea(margin, margin, width() - margin * 2.0, height() - margin * 2.0);

    // Grid lines (horizontal value lines)
    painter.setPen(QPen(QColor(45, 45, 50), 1));
    for (int i = 0; i <= 4; i++) {
        double val = m_minValue + (m_maxValue - m_minValue) * (i / 4.0);
        QPointF p = frameValueToPixel(m_minFrame, val);
        painter.drawLine(margin, p.y(), width() - margin, p.y());
        painter.setPen(QColor(120, 120, 130));
        painter.drawText(6, p.y() + 4, QString::number(val, 'f', 1));
        painter.setPen(QPen(QColor(45, 45, 50), 1));
    }

    // Vertical frame lines
    for (int f = 0; f <= 100; f += 20) {
        QPointF p = frameValueToPixel(f, m_minValue);
        painter.drawLine(p.x(), margin, p.x(), height() - margin);
        painter.setPen(QColor(120, 120, 130));
        painter.drawText(p.x() - 10, height() - margin + 18, QString::number(f));
        painter.setPen(QPen(QColor(45, 45, 50), 1));
    }

    // Draw Bézier Curves
    for (int c = 0; c < m_curves.size(); c++) {
        const auto &curve = m_curves[c];
        if (!curve.visible || curve.keyframes.isEmpty()) continue;

        QPainterPath path;
        QPointF p0 = frameValueToPixel(curve.keyframes[0].frame, curve.keyframes[0].value);
        path.moveTo(p0);

        for (int k = 0; k < curve.keyframes.size() - 1; k++) {
            const auto &k1 = curve.keyframes[k];
            const auto &k2 = curve.keyframes[k + 1];

            QPointF p2 = frameValueToPixel(k2.frame, k2.value);

            QPointF c1 = frameValueToPixel(k1.frame + k1.rightTangentX, k1.value + k1.rightTangentY);
            QPointF c2 = frameValueToPixel(k2.frame + k2.leftTangentX, k2.value + k2.leftTangentY);

            path.cubicTo(c1, c2, p2);
        }

        painter.setPen(QPen(curve.color, 2));
        painter.drawPath(path);

        // Draw keyframe diamonds and tangent handles
        for (int k = 0; k < curve.keyframes.size(); k++) {
            const auto &kf = curve.keyframes[k];
            QPointF pt = frameValueToPixel(kf.frame, kf.value);

            // Diamond keyframe
            painter.setPen(QPen(QColor(20, 20, 20), 1.5));
            painter.setBrush(curve.color);
            static const QPointF diamond[4] = { QPointF(0, -5), QPointF(5, 0), QPointF(0, 5), QPointF(-5, 0) };
            painter.save();
            painter.translate(pt);
            painter.drawPolygon(diamond, 4);
            painter.restore();
        }

        // Legend label top left
        painter.setPen(curve.color);
        painter.drawText(margin + 10 + c * 160, margin - 10, curve.name);
    }

    // Playhead red vertical bar
    QPointF playheadPt = frameValueToPixel(m_currentFrame, m_minValue);
    painter.setPen(QPen(QColor(240, 60, 60), 1.5));
    painter.drawLine(playheadPt.x(), margin, playheadPt.x(), height() - margin);
}

void NatronCurveEditorView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QPointF fv = pixelToFrameValue(event->pos());
        m_currentFrame = qBound(0, (int)qRound(fv.x()), 100);
        update();
    }
}

void NatronCurveEditorView::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        QPointF fv = pixelToFrameValue(event->pos());
        m_currentFrame = qBound(0, (int)qRound(fv.x()), 100);
        update();
    }
}

void NatronCurveEditorView::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    m_draggingKey = false;
}

void NatronCurveEditorView::wheelEvent(QWheelEvent *event)
{
    double zoom = event->angleDelta().y() > 0 ? 0.9 : 1.1;
    m_maxFrame *= zoom;
    update();
}
