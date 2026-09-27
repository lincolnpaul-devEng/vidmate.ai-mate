/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Interactive Natron Curve Editor View — Bézier function curves, tangents,
 * and keyframe interpolation grid.
 */

#pragma once

#include <QWidget>
#include <QPainter>
#include <QPointF>
#include <QVector>

struct CurveKeyframe {
    double frame;
    double value;
    double leftTangentX{-5.0};
    double leftTangentY{0.0};
    double rightTangentX{5.0};
    double rightTangentY{0.0};
};

struct AnimationCurve {
    QString name;
    QColor color;
    QVector<CurveKeyframe> keyframes;
    bool visible{true};
};

/**
 * @class NatronCurveEditorView
 * @brief Interactive Bézier function curve editor matching Natron's CurveGui
 */
class NatronCurveEditorView : public QWidget
{
    Q_OBJECT

public:
    explicit NatronCurveEditorView(QWidget *parent = nullptr);
    ~NatronCurveEditorView() override = default;

    void addCurve(const QString &name, const QColor &color, const QVector<CurveKeyframe> &keyframes);
    void clearCurves();
    void setCurrentFrame(int frame);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QPointF frameValueToPixel(double frame, double value) const;
    QPointF pixelToFrameValue(const QPointF &pixel) const;

    QVector<AnimationCurve> m_curves;
    int m_currentFrame{0};
    double m_minFrame{0.0};
    double m_maxFrame{100.0};
    double m_minValue{0.0};
    double m_maxValue{1.0};

    int m_selectedCurveIdx{-1};
    int m_selectedKeyIdx{-1};
    bool m_draggingKey{false};
};
