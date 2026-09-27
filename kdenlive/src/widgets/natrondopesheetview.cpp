/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Interactive Natron Dope Sheet & Timeline Scrubber View Implementation
 */

#include "natrondopesheetview.h"
#include <QMouseEvent>
#include <QtMath>

NatronDopeSheetView::NatronDopeSheetView(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(120);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);

    // Default sample tracks
    addTrack(QStringLiteral("Keyer1 (Chroma)"), QColor(70, 170, 100), {0, 24, 48, 96}, 0, 100);
    addTrack(QStringLiteral("Roto1 (Matte)"), QColor(160, 90, 190), {0, 15, 30, 60, 90}, 0, 100);
    addTrack(QStringLiteral("Transform1 (Track)"), QColor(200, 110, 50), {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100}, 0, 100);
    addTrack(QStringLiteral("Grade1 (Color)"), QColor(70, 130, 200), {0, 50, 100}, 0, 100);
}

void NatronDopeSheetView::addTrack(const QString &name, const QColor &color, const QVector<int> &keyframes, int inF, int outF)
{
    m_tracks.append({name, color, keyframes, inF, outF});
    update();
}

void NatronDopeSheetView::clearTracks()
{
    m_tracks.clear();
    update();
}

void NatronDopeSheetView::setCurrentFrame(int frame)
{
    m_currentFrame = frame;
    update();
}

void NatronDopeSheetView::setLoopRange(int inFrame, int outFrame)
{
    m_inFrame = inFrame;
    m_outFrame = outFrame;
    update();
}

qreal NatronDopeSheetView::frameToPixel(int frame) const
{
    qreal availableW = width() - m_headerWidth - 20;
    return m_headerWidth + (qreal(frame) / m_totalFrames) * availableW;
}

int NatronDopeSheetView::pixelToFrame(qreal x) const
{
    qreal availableW = width() - m_headerWidth - 20;
    if (availableW <= 0) return 0;
    int f = qRound(((x - m_headerWidth) / availableW) * m_totalFrames);
    return qBound(0, f, m_totalFrames);
}

void NatronDopeSheetView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Background
    painter.fillRect(rect(), QColor(26, 26, 28));

    // Top Ruler Header (Timeline Scrubber)
    QRect rulerRect(0, 0, width(), 26);
    painter.fillRect(rulerRect, QColor(34, 34, 38));
    painter.setPen(QColor(60, 60, 65));
    painter.drawLine(0, 26, width(), 26);

    // Draw frame ticks on ruler
    painter.setFont(QFont(QStringLiteral("SansSerif"), 8));
    for (int f = 0; f <= m_totalFrames; f += 10) {
        qreal x = frameToPixel(f);
        painter.setPen(QColor(80, 80, 90));
        painter.drawLine(x, 14, x, 26);
        painter.setPen(QColor(160, 160, 170));
        painter.drawText(x - 8, 12, QString::number(f));
    }

    // In/Out Active Loop Region
    qreal inX = frameToPixel(m_inFrame);
    qreal outX = frameToPixel(m_outFrame);
    painter.fillRect(QRectF(inX, 26, outX - inX, height() - 26), QColor(255, 255, 255, 6));

    // Left Track List Hierarchy
    painter.fillRect(0, 26, m_headerWidth, height() - 26, QColor(30, 30, 34));
    painter.setPen(QColor(50, 50, 55));
    painter.drawLine(m_headerWidth, 0, m_headerWidth, height());

    // Draw Dope Sheet Tracks
    for (int i = 0; i < m_tracks.size(); i++) {
        const auto &track = m_tracks[i];
        int y = 28 + i * m_trackHeight;

        // Alternate row shading
        if (i % 2 == 0) {
            painter.fillRect(m_headerWidth, y, width() - m_headerWidth, m_trackHeight, QColor(32, 32, 36));
        }

        // Track name in header
        painter.setPen(track.color);
        painter.drawText(10, y + 16, track.name);

        // Track duration block
        qreal tInX = frameToPixel(track.inFrame);
        qreal tOutX = frameToPixel(track.outFrame);
        QRectF blockRect(tInX, y + 4, tOutX - tInX, m_trackHeight - 8);
        painter.fillRect(blockRect, QColor(track.color.red(), track.color.green(), track.color.blue(), 60));
        painter.setPen(QPen(track.color, 1));
        painter.drawRect(blockRect);

        // Keyframe markers (vertical tick diamonds)
        painter.setBrush(QColor(240, 240, 240));
        painter.setPen(QColor(20, 20, 20));
        for (int kf : track.keyframes) {
            qreal kx = frameToPixel(kf);
            painter.drawRect(QRectF(kx - 3, y + 6, 6, m_trackHeight - 12));
        }
    }

    // Global Playhead Scrubber (Red Line)
    qreal playX = frameToPixel(m_currentFrame);
    painter.setPen(QPen(QColor(240, 60, 60), 2));
    painter.drawLine(playX, 0, playX, height());

    // Playhead top handle
    static const QPointF handle[3] = { QPointF(-6, 0), QPointF(6, 0), QPointF(0, 10) };
    painter.save();
    painter.translate(playX, 0);
    painter.setBrush(QColor(240, 60, 60));
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(handle, 3);
    painter.restore();
}

void NatronDopeSheetView::mousePressEvent(QMouseEvent *event)
{
    if (event->pos().x() >= m_headerWidth) {
        m_currentFrame = pixelToFrame(event->pos().x());
        update();
        Q_EMIT seekRequested(m_currentFrame);
    }
}

void NatronDopeSheetView::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton && event->pos().x() >= m_headerWidth) {
        m_currentFrame = pixelToFrame(event->pos().x());
        update();
        Q_EMIT seekRequested(m_currentFrame);
    }
}
