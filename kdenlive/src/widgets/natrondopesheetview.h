/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Interactive Natron Dope Sheet & Timeline Scrubber View — Multi-track keyframe
 * retiming blocks and global frame scrubber with playback loop control.
 */

#pragma once

#include <QWidget>
#include <QPainter>
#include <QVector>

struct DopeSheetTrack {
    QString name;
    QColor color;
    QVector<int> keyframes;
    int inFrame{0};
    int outFrame{100};
};

/**
 * @class NatronDopeSheetView
 * @brief Multi-track keyframe block retimer and timeline scrubber
 */
class NatronDopeSheetView : public QWidget
{
    Q_OBJECT

public:
    explicit NatronDopeSheetView(QWidget *parent = nullptr);
    ~NatronDopeSheetView() override = default;

    void addTrack(const QString &name, const QColor &color, const QVector<int> &keyframes, int inF, int outF);
    void clearTracks();
    void setCurrentFrame(int frame);
    void setLoopRange(int inFrame, int outFrame);

Q_SIGNALS:
    void seekRequested(int frame);
    void loopRangeChanged(int inFrame, int outFrame);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    qreal frameToPixel(int frame) const;
    int pixelToFrame(qreal x) const;

    QVector<DopeSheetTrack> m_tracks;
    int m_currentFrame{0};
    int m_inFrame{0};
    int m_outFrame{100};
    int m_totalFrames{120};
    int m_trackHeight{24};
    int m_headerWidth{160};
};
