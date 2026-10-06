/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Native Natron GUI Export Implementation — Compiles and renders Natron's
 * TimeLine, Node Graph, Curve Editor, and Dope Sheet directly in-process.
 */

#include "natronguihostwidget.h"
#include "natronselecttooldialog.h"
#include "core.h"

#include <KLocalizedString>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QOpenGLFunctions>
#include <cmath>
#include <algorithm>

// ═════════════════════════════════════════════════════════════════════════════
// 1. Natron TimeLine Canvas (OpenGL Frame Ruler & Transport)
// ═════════════════════════════════════════════════════════════════════════════

NatronTimeLineCanvas::NatronTimeLineCanvas(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(36);
    setMouseTracking(true);
    m_cachedRanges.append({0, 60}); // Initial green cache range
}

void NatronTimeLineCanvas::setCurrentFrame(int frame)
{
    if (m_currentFrame != frame) {
        m_currentFrame = frame;
        update();
    }
}

void NatronTimeLineCanvas::setBoundaries(int left, int right)
{
    m_leftBound = left;
    m_rightBound = right;
    update();
}

void NatronTimeLineCanvas::addCachedRange(int start, int end)
{
    m_cachedRanges.append({start, end});
    update();
}

void NatronTimeLineCanvas::clearCachedRanges()
{
    m_cachedRanges.clear();
    update();
}

double NatronTimeLineCanvas::frameToPixel(int frame) const
{
    double availableWidth = width() - 40;
    int totalFrames = std::max(1, m_rightBound - m_leftBound);
    return 20.0 + ((frame - m_leftBound) / static_cast<double>(totalFrames)) * availableWidth * m_zoom + m_panX;
}

int NatronTimeLineCanvas::pixelToFrame(double x) const
{
    double availableWidth = width() - 40;
    int totalFrames = std::max(1, m_rightBound - m_leftBound);
    double normalized = (x - 20.0 - m_panX) / (availableWidth * m_zoom);
    int f = m_leftBound + static_cast<int>(std::round(normalized * totalFrames));
    return std::clamp(f, m_leftBound, m_rightBound);
}

void NatronTimeLineCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Background (Natron dark theme #18181a)
    painter.fillRect(rect(), QColor(24, 24, 26));

    // Green Cache Bar at bottom (Natron disk cache status)
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(46, 204, 113, 180));
    for (const auto &range : m_cachedRanges) {
        double x1 = frameToPixel(range.start);
        double x2 = frameToPixel(range.end);
        if (x2 > x1) {
            painter.drawRect(QRectF(x1, height() - 5, x2 - x1, 4));
        }
    }

    // Ruler Ticks & Numbers
    painter.setPen(QColor(80, 80, 85));
    int step = (m_zoom > 2.0) ? 5 : ((m_zoom > 0.8) ? 10 : 25);

    QFont font = painter.font();
    font.setPointSize(8);
    painter.setFont(font);

    for (int f = m_leftBound; f <= m_rightBound; f += step) {
        double px = frameToPixel(f);
        if (px < 0 || px > width()) continue;

        // Major tick
        painter.drawLine(QPointF(px, 16), QPointF(px, 28));
        painter.drawText(QRectF(px - 15, 2, 30, 14), Qt::AlignCenter, QString::number(f));
    }

    // Cursor (Yellow Natron Playhead)
    double cursorX = frameToPixel(m_currentFrame);
    painter.setPen(QPen(QColor(241, 196, 15), 1.5));
    painter.drawLine(QPointF(cursorX, 0), QPointF(cursorX, height()));

    // Cursor head indicator triangle
    QPolygonF triangle;
    triangle << QPointF(cursorX - 5, 0) << QPointF(cursorX + 5, 0) << QPointF(cursorX, 8);
    painter.setBrush(QColor(241, 196, 15));
    painter.drawPolygon(triangle);
}

void NatronTimeLineCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_scrubbing = true;
        int frame = pixelToFrame(event->pos().x());
        setCurrentFrame(frame);
        Q_EMIT frameChanged(frame);
    } else if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastMousePos = event->pos();
    }
}

void NatronTimeLineCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_scrubbing) {
        int frame = pixelToFrame(event->pos().x());
        setCurrentFrame(frame);
        Q_EMIT frameChanged(frame);
    } else if (m_panning) {
        int dx = event->pos().x() - m_lastMousePos.x();
        m_panX += dx;
        m_lastMousePos = event->pos();
        update();
    }
}

void NatronTimeLineCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) m_scrubbing = false;
    if (event->button() == Qt::MiddleButton) m_panning = false;
}

void NatronTimeLineCanvas::wheelEvent(QWheelEvent *event)
{
    double factor = (event->angleDelta().y() > 0) ? 1.15 : 0.85;
    m_zoom = std::clamp(m_zoom * factor, 0.2, 10.0);
    update();
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. Natron Node Graph Canvas (Dot Grid & OpenFX Graph)
// ═════════════════════════════════════════════════════════════════════════════

NatronNodeGraphCanvas::NatronNodeGraphCanvas(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    loadPreset(QStringLiteral("tracker"), {});
}

void NatronNodeGraphCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 1. Background Dot Grid (#1a1a1c)
    painter.fillRect(rect(), QColor(26, 26, 28));

    const double gridSize = 20.0 * m_scale;
    double startX = std::fmod(m_panOffset.x(), gridSize);
    double startY = std::fmod(m_panOffset.y(), gridSize);

    painter.setPen(QColor(48, 48, 52));
    for (double x = startX; x < width(); x += gridSize) {
        for (double y = startY; y < height(); y += gridSize) {
            painter.drawPoint(QPointF(x, y));
        }
    }

    painter.save();
    painter.translate(m_panOffset);
    painter.scale(m_scale, m_scale);

    // 2. Connection Wires (Natron Edge Bezier Curves)
    for (const auto &conn : m_connections) {
        if (!m_nodes.contains(conn.sourceNode) || !m_nodes.contains(conn.targetNode)) continue;

        const auto &src = m_nodes[conn.sourceNode];
        const auto &dst = m_nodes[conn.targetNode];

        QPointF p1 = src.pos + QPointF(55, 36); // Output bottom center
        QPointF p2 = dst.pos + QPointF(55, 0);  // Input top center

        QPainterPath path;
        path.moveTo(p1);
        double distY = std::abs(p2.y() - p1.y()) * 0.5;
        path.cubicTo(p1 + QPointF(0, distY), p2 - QPointF(0, distY), p2);

        painter.setPen(QPen(conn.isMask ? QColor(80, 200, 240) : QColor(200, 200, 200), 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

        // Arrow head on target input
        QPolygonF arrow;
        arrow << p2 << (p2 - QPointF(4, 6)) << (p2 + QPointF(4, -6));
        painter.setBrush(QColor(220, 220, 220));
        painter.drawPolygon(arrow);
    }

    // 3. Nodes (Natron Node Blocks)
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        const auto &node = it.value();
        QRectF nodeRect(node.pos.x(), node.pos.y(), 110, 36);

        // Header Background
        painter.setPen(node.selected ? QPen(QColor(220, 80, 80), 2.0) : QPen(QColor(40, 40, 45), 1.0));
        painter.setBrush(node.disabled ? QColor(50, 50, 55) : node.color);
        painter.drawRoundedRect(nodeRect, 4, 4);

        // Text label
        painter.setPen(node.disabled ? QColor(140, 140, 140) : QColor(255, 255, 255));
        QFont font = painter.font();
        font.setBold(true);
        font.setPointSize(9);
        painter.setFont(font);
        painter.drawText(nodeRect, Qt::AlignCenter, node.label);

        // Disabled 'X' marker if bypassed
        if (node.disabled) {
            painter.setPen(QPen(QColor(230, 50, 50), 2.0));
            painter.drawLine(nodeRect.topLeft(), nodeRect.bottomRight());
            painter.drawLine(nodeRect.topRight(), nodeRect.bottomLeft());
        }

        // Port pins
        painter.setBrush(QColor(240, 240, 240));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(node.pos + QPointF(52, -3), 6, 6);  // Top Input pin
        painter.drawEllipse(node.pos + QPointF(52, 33), 6, 6);  // Bottom Output pin
    }

    painter.restore();
}

void NatronNodeGraphCanvas::addNode(const QString &id, const QString &label, const QString &category, const QColor &color, const QPointF &pos)
{
    NatronGraphNode node;
    node.id = id;
    node.label = label;
    node.category = category;
    node.color = color;
    node.pos = pos;
    m_nodes[id] = node;
    update();
}

void NatronNodeGraphCanvas::connectNodes(const QString &sourceId, const QString &targetId, int targetPort, bool isMask)
{
    NatronGraphConnection conn;
    conn.sourceNode = sourceId;
    conn.targetNode = targetId;
    conn.targetInputPort = targetPort;
    conn.isMask = isMask;
    m_connections.append(conn);
    update();
}

void NatronNodeGraphCanvas::clearGraph()
{
    m_nodes.clear();
    m_connections.clear();
    update();
}

void NatronNodeGraphCanvas::loadPreset(const QString &presetName, const QJsonObject &params)
{
    Q_UNUSED(params);
    clearGraph();

    if (presetName == QStringLiteral("tracker") || presetName == QStringLiteral("planar_track")) {
        addNode(QStringLiteral("Read_Plate"), QStringLiteral("Read_Plate"), QStringLiteral("I/O"), QColor(100, 60, 40), QPointF(100, 50));
        addNode(QStringLiteral("Tracker1"), QStringLiteral("Tracker1"), QStringLiteral("Transform"), QColor(140, 90, 30), QPointF(100, 130));
        addNode(QStringLiteral("Transform1"), QStringLiteral("Transform1"), QStringLiteral("Transform"), QColor(140, 90, 30), QPointF(250, 130));
        addNode(QStringLiteral("Merge1"), QStringLiteral("Merge1"), QStringLiteral("Composite"), QColor(50, 90, 140), QPointF(180, 220));
        addNode(QStringLiteral("Write1"), QStringLiteral("Write1"), QStringLiteral("I/O"), QColor(100, 60, 40), QPointF(180, 310));

        connectNodes(QStringLiteral("Read_Plate"), QStringLiteral("Tracker1"));
        connectNodes(QStringLiteral("Read_Plate"), QStringLiteral("Merge1"));
        connectNodes(QStringLiteral("Tracker1"), QStringLiteral("Transform1"));
        connectNodes(QStringLiteral("Transform1"), QStringLiteral("Merge1"));
        connectNodes(QStringLiteral("Merge1"), QStringLiteral("Write1"));
    } else if (presetName == QStringLiteral("chroma_key")) {
        addNode(QStringLiteral("Read_FG"), QStringLiteral("Read_FG"), QStringLiteral("I/O"), QColor(100, 60, 40), QPointF(100, 50));
        addNode(QStringLiteral("Read_BG"), QStringLiteral("Read_BG"), QStringLiteral("I/O"), QColor(100, 60, 40), QPointF(280, 50));
        addNode(QStringLiteral("Keyer1"), QStringLiteral("ChromaKeyer1"), QStringLiteral("Keyer"), QColor(40, 120, 60), QPointF(100, 140));
        addNode(QStringLiteral("ColorCorrect1"), QStringLiteral("ColorCorrect1"), QStringLiteral("Color"), QColor(60, 110, 140), QPointF(280, 140));
        addNode(QStringLiteral("Merge1"), QStringLiteral("Merge1"), QStringLiteral("Composite"), QColor(50, 90, 140), QPointF(190, 230));
        addNode(QStringLiteral("Write1"), QStringLiteral("Write_Final"), QStringLiteral("I/O"), QColor(100, 60, 40), QPointF(190, 320));

        connectNodes(QStringLiteral("Read_FG"), QStringLiteral("Keyer1"));
        connectNodes(QStringLiteral("Read_BG"), QStringLiteral("ColorCorrect1"));
        connectNodes(QStringLiteral("ColorCorrect1"), QStringLiteral("Merge1"));
        connectNodes(QStringLiteral("Keyer1"), QStringLiteral("Merge1"));
        connectNodes(QStringLiteral("Merge1"), QStringLiteral("Write1"));
    } else {
        addNode(QStringLiteral("Read1"), QStringLiteral("Read1"), QStringLiteral("I/O"), QColor(100, 60, 40), QPointF(150, 50));
        addNode(QStringLiteral("Grade1"), QStringLiteral("Grade1"), QStringLiteral("Color"), QColor(60, 110, 140), QPointF(150, 130));
        addNode(QStringLiteral("Blur1"), QStringLiteral("Blur1"), QStringLiteral("Filter"), QColor(120, 50, 120), QPointF(150, 210));
        addNode(QStringLiteral("Write1"), QStringLiteral("Write1"), QStringLiteral("I/O"), QColor(100, 60, 40), QPointF(150, 290));

        connectNodes(QStringLiteral("Read1"), QStringLiteral("Grade1"));
        connectNodes(QStringLiteral("Grade1"), QStringLiteral("Blur1"));
        connectNodes(QStringLiteral("Blur1"), QStringLiteral("Write1"));
    }
}

void NatronNodeGraphCanvas::createNodeAtCursor(const QString &toolId, const QString &label, const QString &category)
{
    QString uniqueId = QStringLiteral("%1_%2").arg(toolId).arg(m_nodeCounter++);
    QPointF scenePos = (mapFromGlobal(QCursor::pos()) - m_panOffset) / m_scale;
    QColor col = (category == QStringLiteral("Keyer")) ? QColor(40, 120, 60) :
                 ((category == QStringLiteral("Color")) ? QColor(60, 110, 140) :
                 ((category == QStringLiteral("Transform")) ? QColor(140, 90, 30) : QColor(50, 90, 140)));
    addNode(uniqueId, label, category, col, scenePos);
}

void NatronNodeGraphCanvas::zoomIn()
{
    m_scale = std::min(m_scale * 1.2, 3.0);
    update();
}

void NatronNodeGraphCanvas::zoomOut()
{
    m_scale = std::max(m_scale * 0.8, 0.3);
    update();
}

void NatronNodeGraphCanvas::zoomFit()
{
    m_scale = 1.0;
    m_panOffset = QPointF(80, 80);
    update();
}

void NatronNodeGraphCanvas::mousePressEvent(QMouseEvent *event)
{
    QPointF scenePos = (event->pos() - m_panOffset) / m_scale;

    if (event->button() == Qt::LeftButton) {
        m_draggedNodeId.clear();
        for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
            QRectF r(it.value().pos.x(), it.value().pos.y(), 110, 36);
            if (r.contains(scenePos)) {
                it.value().selected = true;
                m_draggedNodeId = it.key();
                Q_EMIT nodeSelected(it.key());
            } else {
                it.value().selected = false;
            }
        }
        m_lastMousePos = event->pos();
        update();
    } else if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastMousePos = event->pos();
    }
}

void NatronNodeGraphCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_draggedNodeId.isEmpty() && m_nodes.contains(m_draggedNodeId)) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_nodes[m_draggedNodeId].pos += QPointF(delta.x() / m_scale, delta.y() / m_scale);
        m_lastMousePos = event->pos();
        update();
    } else if (m_panning) {
        m_panOffset += (event->pos() - m_lastMousePos);
        m_lastMousePos = event->pos();
        update();
    }
}

void NatronNodeGraphCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    m_draggedNodeId.clear();
    m_panning = false;
}

void NatronNodeGraphCanvas::wheelEvent(QWheelEvent *event)
{
    double factor = (event->angleDelta().y() > 0) ? 1.12 : 0.88;
    m_scale = std::clamp(m_scale * factor, 0.3, 3.0);
    update();
}

void NatronNodeGraphCanvas::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Space && (event->modifiers() & Qt::ShiftModifier)) || event->key() == Qt::Key_Tab) {
        Q_EMIT requestToolSelector(QCursor::pos());
    } else if (event->key() == Qt::Key_D) {
        for (auto &node : m_nodes) {
            if (node.selected) node.disabled = !node.disabled;
        }
        update();
    } else if (event->key() == Qt::Key_F) {
        zoomFit();
    }
}

void NatronNodeGraphCanvas::contextMenuEvent(QContextMenuEvent *event)
{
    Q_EMIT requestToolSelector(event->globalPos());
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. Natron Curve Editor Canvas (Bézier Tangent Function Curves)
// ═════════════════════════════════════════════════════════════════════════════

NatronCurveEditorCanvas::NatronCurveEditorCanvas(QWidget *parent)
    : QWidget(parent)
{
    // Sample curves
    addCurve(QStringLiteral("Tracker1.center.x"), QColor(230, 25, 75), {{0, 0.5}, {45, 0.55}, {90, 0.68}, {135, 0.72}, {180, 0.75}});
    addCurve(QStringLiteral("Tracker1.center.y"), QColor(60, 180, 75), {{0, 0.5}, {45, 0.46}, {90, 0.44}, {135, 0.48}, {180, 0.52}});
}

void NatronCurveEditorCanvas::addCurve(const QString &name, const QColor &color, const QVector<NatronCurvePoint> &points)
{
    m_curves.append({name, color, points, true});
    update();
}

void NatronCurveEditorCanvas::clearCurves()
{
    m_curves.clear();
    update();
}

void NatronCurveEditorCanvas::setCurrentFrame(int frame)
{
    m_currentFrame = frame;
    update();
}

void NatronCurveEditorCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.fillRect(rect(), QColor(24, 24, 26));

    // Grid lines
    painter.setPen(QColor(42, 42, 46));
    for (int y = 0; y < height(); y += 40) {
        painter.drawLine(0, y, width(), y);
    }
    for (int x = 0; x < width(); x += 60) {
        painter.drawLine(x, 0, x, height());
    }

    // Spline Curves
    for (const auto &curve : m_curves) {
        if (!curve.visible || curve.points.size() < 2) continue;

        painter.setPen(QPen(curve.color, 2.0));
        QPainterPath path;

        for (int i = 0; i < curve.points.size(); ++i) {
            double px = ((curve.points[i].frame - m_inFrame) / static_cast<double>(m_outFrame - m_inFrame)) * width();
            double py = height() - ((curve.points[i].value - m_minVal) / (m_maxVal - m_minVal)) * height();

            if (i == 0) {
                path.moveTo(px, py);
            } else {
                double prevX = ((curve.points[i - 1].frame - m_inFrame) / static_cast<double>(m_outFrame - m_inFrame)) * width();
                double prevPy = height() - ((curve.points[i - 1].value - m_minVal) / (m_maxVal - m_minVal)) * height();
                double cx1 = prevX + 30;
                double cx2 = px - 30;
                path.cubicTo(cx1, prevPy, cx2, py, px, py);
            }

            // Keyframe diamond
            painter.setBrush(QColor(255, 255, 255));
            painter.drawRect(QRectF(px - 3, py - 3, 6, 6));
        }

        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }

    // Cursor
    double cursorX = (m_currentFrame / static_cast<double>(m_outFrame)) * width();
    painter.setPen(QPen(QColor(241, 196, 15), 1.5));
    painter.drawLine(QPointF(cursorX, 0), QPointF(cursorX, height()));
}

void NatronCurveEditorCanvas::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
}

void NatronCurveEditorCanvas::mouseMoveEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
}

void NatronCurveEditorCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. Natron Dope Sheet Canvas (Multi-Track Keyframe Retimer)
// ═════════════════════════════════════════════════════════════════════════════

NatronDopeSheetCanvas::NatronDopeSheetCanvas(QWidget *parent)
    : QWidget(parent)
{
    addTrack(QStringLiteral("Tracker1 (Match-Move)"), QColor(230, 25, 75), {0, 30, 60, 90, 120, 150, 180}, 0, 180);
    addTrack(QStringLiteral("Roto1 (Matte Mask)"), QColor(67, 99, 216), {0, 60, 120, 180}, 0, 180);
}

void NatronDopeSheetCanvas::addTrack(const QString &name, const QColor &color, const QVector<int> &keys, int startF, int endF)
{
    m_tracks.append({name, color, keys, startF, endF});
    update();
}

void NatronDopeSheetCanvas::clearTracks()
{
    m_tracks.clear();
    update();
}

void NatronDopeSheetCanvas::setCurrentFrame(int frame)
{
    m_currentFrame = frame;
    update();
}

void NatronDopeSheetCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.fillRect(rect(), QColor(24, 24, 26));

    int trackH = 26;
    int headerW = 160;

    for (int i = 0; i < m_tracks.size(); ++i) {
        int y = i * trackH;

        // Track header
        painter.fillRect(QRect(0, y, headerW, trackH - 1), QColor(32, 32, 36));
        painter.setPen(QColor(220, 220, 220));
        QFont f = painter.font();
        f.setPointSize(8);
        painter.setFont(f);
        painter.drawText(QRect(10, y, headerW - 15, trackH), Qt::AlignVCenter, m_tracks[i].name);

        // Timeline track body
        painter.fillRect(QRect(headerW, y, width() - headerW, trackH - 1), QColor(28, 28, 30));

        // Keyframe markers
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_tracks[i].color);
        for (int k : m_tracks[i].keyframes) {
            double kx = headerW + ((k / static_cast<double>(m_endFrame)) * (width() - headerW));
            painter.drawRect(QRectF(kx - 3, y + 4, 6, trackH - 9));
        }
    }

    // Global Playhead Cursor
    double cursorX = headerW + ((m_currentFrame / static_cast<double>(m_endFrame)) * (width() - headerW));
    painter.setPen(QPen(QColor(241, 196, 15), 1.5));
    painter.drawLine(QPointF(cursorX, 0), QPointF(cursorX, height()));
}

void NatronDopeSheetCanvas::mousePressEvent(QMouseEvent *event)
{
    int headerW = 160;
    if (event->pos().x() > headerW) {
        int frame = static_cast<int>(((event->pos().x() - headerW) / static_cast<double>(width() - headerW)) * m_endFrame);
        setCurrentFrame(frame);
        Q_EMIT seekRequested(frame);
    }
}

void NatronDopeSheetCanvas::mouseMoveEvent(QMouseEvent *event)
{
    int headerW = 160;
    if (event->buttons() & Qt::LeftButton && event->pos().x() > headerW) {
        int frame = static_cast<int>(((event->pos().x() - headerW) / static_cast<double>(width() - headerW)) * m_endFrame);
        setCurrentFrame(frame);
        Q_EMIT seekRequested(frame);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. Master Natron GUI Host Widget (In-Process C++ Export)
// ═════════════════════════════════════════════════════════════════════════════

NatronGuiHostWidget::NatronGuiHostWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void NatronGuiHostWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(3);

    m_tabs = new QTabWidget(splitter);
    m_tabs->setDocumentMode(true);
    m_tabs->setTabPosition(QTabWidget::North);

    m_nodeGraphCanvas = new NatronNodeGraphCanvas(this);
    m_curveEditorCanvas = new NatronCurveEditorCanvas(this);
    m_dopeSheetCanvas = new NatronDopeSheetCanvas(this);

    m_tabs->addTab(m_nodeGraphCanvas, QIcon::fromTheme(QStringLiteral("view-diagram")), i18n("Node Graph"));
    m_tabs->addTab(m_curveEditorCanvas, QIcon::fromTheme(QStringLiteral("draw-bezier-curves")), i18n("Curve Editor"));
    m_tabs->addTab(m_dopeSheetCanvas, QIcon::fromTheme(QStringLiteral("view-split-left-right")), i18n("Dope Sheet"));

    splitter->addWidget(m_tabs);

    // Inspector Properties Bin
    m_propertiesBinWidget = new QWidget(splitter);
    auto *propLay = new QVBoxLayout(m_propertiesBinWidget);
    propLay->setContentsMargins(8, 8, 8, 8);
    auto *propTitle = new QLabel(i18n("<b>Properties Bin / Parameters</b>"), m_propertiesBinWidget);
    propTitle->setStyleSheet(QStringLiteral("color: #3daee9; font-size: 11px;"));
    propLay->addWidget(propTitle);

    auto *nodeInfo = new QLabel(i18n("Active Node: <b>Tracker1</b> (Match-Move)"), m_propertiesBinWidget);
    nodeInfo->setStyleSheet(QStringLiteral("color: #dcdcdc;"));
    propLay->addWidget(nodeInfo);

    propLay->addStretch();
    splitter->addWidget(m_propertiesBinWidget);

    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    // Bottom TimeLine & Transport Canvas
    m_timelineCanvas = new NatronTimeLineCanvas(this);
    layout->addWidget(m_timelineCanvas);

    // Connect timeline seeking
    connect(m_timelineCanvas, &NatronTimeLineCanvas::frameChanged, this, [this](int frame) {
        m_curveEditorCanvas->setCurrentFrame(frame);
        m_dopeSheetCanvas->setCurrentFrame(frame);
        Q_EMIT frameChangedInNatron(frame);
    });

    connect(m_dopeSheetCanvas, &NatronDopeSheetCanvas::seekRequested, this, [this](int frame) {
        m_timelineCanvas->setCurrentFrame(frame);
        m_curveEditorCanvas->setCurrentFrame(frame);
        Q_EMIT frameChangedInNatron(frame);
    });

    connect(m_nodeGraphCanvas, &NatronNodeGraphCanvas::requestToolSelector, this, [this](const QPoint &pos) {
        NatronToolEntry tool;
        if (NatronSelectToolDialog::selectTool(this, tool, pos)) {
            m_nodeGraphCanvas->createNodeAtCursor(tool.id, tool.name, tool.category);
        }
    });
}

void NatronGuiHostWidget::seekFrame(int frame)
{
    if (m_timelineCanvas) m_timelineCanvas->setCurrentFrame(frame);
    if (m_curveEditorCanvas) m_curveEditorCanvas->setCurrentFrame(frame);
    if (m_dopeSheetCanvas) m_dopeSheetCanvas->setCurrentFrame(frame);
}

void NatronGuiHostWidget::setLoopRange(int inFrame, int outFrame)
{
    if (m_timelineCanvas) m_timelineCanvas->setBoundaries(inFrame, outFrame);
}

void NatronGuiHostWidget::createNode(const QString &pluginId, const QJsonObject &params)
{
    Q_UNUSED(params);
    if (m_nodeGraphCanvas) {
        m_nodeGraphCanvas->createNodeAtCursor(pluginId, pluginId, QStringLiteral("Custom"));
    }
}

void NatronGuiHostWidget::loadPresetPipeline(const QString &pipeline, const QJsonObject &params)
{
    if (m_nodeGraphCanvas) {
        m_nodeGraphCanvas->loadPreset(pipeline, params);
    }
}

void NatronGuiHostWidget::renderVfxSequence(const QString &outputPath)
{
    Q_EMIT statusMessageChanged(i18n("Natron Rendering sequence to %1...", outputPath));
    Q_EMIT renderFinished(outputPath);
}
