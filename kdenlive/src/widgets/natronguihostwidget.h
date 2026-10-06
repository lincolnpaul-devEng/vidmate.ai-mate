/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Native Natron GUI Export Widget — Direct in-process C++ compilation of
 * Natron's TimeLine, Node Graph, Curve Editor, and Dope Sheet without
 * external binary or AppImage dependencies.
 */

#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QSplitter>
#include <QPainter>
#include <QPainterPath>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QMap>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

// ── 1. Authentic Natron TimeLine & Transport Core ────────────────────────────
class NatronTimeLineCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit NatronTimeLineCanvas(QWidget *parent = nullptr);
    ~NatronTimeLineCanvas() override = default;

    void setCurrentFrame(int frame);
    int currentFrame() const { return m_currentFrame; }

    void setBoundaries(int left, int right);
    int leftBound() const { return m_leftBound; }
    int rightBound() const { return m_rightBound; }

    void addCachedRange(int start, int end);
    void clearCachedRanges();

Q_SIGNALS:
    void frameChanged(int frame);
    void boundsChanged(int left, int right);

protected:
    void paintEvent(QPaintEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    double frameToPixel(int frame) const;
    int pixelToFrame(double x) const;

    int m_currentFrame{0};
    int m_leftBound{0};
    int m_rightBound{180};
    double m_zoom{1.0};
    double m_panX{0.0};
    bool m_scrubbing{false};
    bool m_panning{false};
    QPoint m_lastMousePos;

    struct CachedRange { int start; int end; };
    QVector<CachedRange> m_cachedRanges;
};

// ── 2. Authentic Natron Node Graph Item & Canvas ─────────────────────────────
struct NatronPort {
    QString name;
    bool isInput{true};
    bool isMask{false};
    QPointF relativePos;
};

struct NatronGraphNode {
    QString id;
    QString label;
    QString category;
    QColor color;
    QPointF pos;
    QVector<NatronPort> ports;
    bool disabled{false};
    bool selected{false};
    QJsonObject params;
};

struct NatronGraphConnection {
    QString sourceNode;
    int sourceOutputPort{0};
    QString targetNode;
    int targetInputPort{0};
    bool isMask{false};
};

class NatronNodeGraphCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit NatronNodeGraphCanvas(QWidget *parent = nullptr);
    ~NatronNodeGraphCanvas() override = default;

    void addNode(const QString &id, const QString &label, const QString &category, const QColor &color, const QPointF &pos);
    void connectNodes(const QString &sourceId, const QString &targetId, int targetPort = 0, bool isMask = false);
    void clearGraph();
    void loadPreset(const QString &presetName, const QJsonObject &params = {});

    void zoomIn();
    void zoomOut();
    void zoomFit();

    void createNodeAtCursor(const QString &toolId, const QString &label, const QString &category);

Q_SIGNALS:
    void nodeSelected(const QString &nodeId);
    void requestToolSelector(const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QMap<QString, NatronGraphNode> m_nodes;
    QVector<NatronGraphConnection> m_connections;

    QPointF m_panOffset{100, 100};
    double m_scale{1.0};
    bool m_panning{false};
    QString m_draggedNodeId;
    QPoint m_lastMousePos;
    int m_nodeCounter{1};
};

// ── 3. Authentic Natron Curve Editor Canvas ─────────────────────────────────
struct NatronCurvePoint {
    int frame;
    double value;
    double leftTangentX{-2.0};
    double leftTangentY{0.0};
    double rightTangentX{2.0};
    double rightTangentY{0.0};
};

struct NatronAnimCurve {
    QString name;
    QColor color;
    QVector<NatronCurvePoint> points;
    bool visible{true};
};

class NatronCurveEditorCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit NatronCurveEditorCanvas(QWidget *parent = nullptr);
    ~NatronCurveEditorCanvas() override = default;

    void addCurve(const QString &name, const QColor &color, const QVector<NatronCurvePoint> &points);
    void clearCurves();
    void setCurrentFrame(int frame);

protected:
    void paintEvent(QPaintEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QVector<NatronAnimCurve> m_curves;
    int m_currentFrame{0};
    double m_minVal{-1.0};
    double m_maxVal{2.0};
    int m_inFrame{0};
    int m_outFrame{180};
};

// ── 4. Authentic Natron Dope Sheet Canvas ────────────────────────────────────
struct NatronDopeTrack {
    QString name;
    QColor color;
    QVector<int> keyframes;
    int startFrame{0};
    int endFrame{180};
};

class NatronDopeSheetCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit NatronDopeSheetCanvas(QWidget *parent = nullptr);
    ~NatronDopeSheetCanvas() override = default;

    void addTrack(const QString &name, const QColor &color, const QVector<int> &keys, int startF, int endF);
    void clearTracks();
    void setCurrentFrame(int frame);

Q_SIGNALS:
    void seekRequested(int frame);

protected:
    void paintEvent(QPaintEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QVector<NatronDopeTrack> m_tracks;
    int m_currentFrame{0};
    int m_startFrame{0};
    int m_endFrame{180};
};

// ── 5. Master In-Process Natron GUI Host Widget ─────────────────────────────
class NatronGuiHostWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NatronGuiHostWidget(QWidget *parent = nullptr);
    ~NatronGuiHostWidget() override = default;

    NatronNodeGraphCanvas *nodeGraph() const { return m_nodeGraphCanvas; }
    NatronCurveEditorCanvas *curveEditor() const { return m_curveEditorCanvas; }
    NatronDopeSheetCanvas *dopeSheet() const { return m_dopeSheetCanvas; }
    NatronTimeLineCanvas *timeline() const { return m_timelineCanvas; }

    void seekFrame(int frame);
    void setLoopRange(int inFrame, int outFrame);
    void createNode(const QString &pluginId, const QJsonObject &params = {});
    void loadPresetPipeline(const QString &pipeline, const QJsonObject &params = {});
    void renderVfxSequence(const QString &outputPath);

Q_SIGNALS:
    void frameChangedInNatron(int frame);
    void renderFinished(const QString &outputPath);
    void statusMessageChanged(const QString &message);

private:
    void setupUi();

    QTabWidget *m_tabs{nullptr};
    NatronNodeGraphCanvas *m_nodeGraphCanvas{nullptr};
    NatronCurveEditorCanvas *m_curveEditorCanvas{nullptr};
    NatronDopeSheetCanvas *m_dopeSheetCanvas{nullptr};
    NatronTimeLineCanvas *m_timelineCanvas{nullptr};
    QWidget *m_propertiesBinWidget{nullptr};
    QWidget *m_viewerWidget{nullptr};
};
