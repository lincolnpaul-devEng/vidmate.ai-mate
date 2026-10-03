/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Professional Natron Node Graph View Implementation
 * Faithfully ported from Natron GUI (NodeGui, Edge, NodeGraph, Backdrop, and LinkArrow)
 */

#include "natronnodegraphview.h"
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGraphicsSceneMouseEvent>
#include <QScrollBar>
#include <QtMath>
#include <cmath>

// ── Natron Category Colors (Matching Natron Engine/Settings.cpp & NodeGui.cpp) ──
static const QColor COLOR_READER     = QColor(204, 153, 51);  // Gold (#cc9933)
static const QColor COLOR_WRITER     = QColor(204, 51, 51);   // Crimson Red (#cc3333)
static const QColor COLOR_MERGE      = QColor(51, 128, 128);  // Teal (#338080)
static const QColor COLOR_COLOR      = QColor(51, 102, 204);  // Blue (#3366cc)
static const QColor COLOR_FILTER     = QColor(102, 153, 51);  // Olive Green (#669933)
static const QColor COLOR_KEYER      = QColor(38, 153, 84);   // Emerald (#269954)
static const QColor COLOR_TRANSFORM  = QColor(204, 102, 25);  // Orange (#cc6619)
static const QColor COLOR_ROTO       = QColor(128, 51, 128);  // Purple (#803380)
static const QColor COLOR_TIME       = QColor(153, 153, 51);  // Khaki (#999933)
static const QColor COLOR_DOT        = QColor(110, 110, 110); // Slate Dot (#6e6e6e)
static const QColor COLOR_BACKDROP   = QColor(40, 48, 56, 175); // Translucent Slate
static const QColor COLOR_DEFAULT    = QColor(100, 100, 100);

// ── NatronNodeItem Implementation ───────────────────────────────────────────

NatronNodeItem::NatronNodeItem(const QString &nodeId,
                               const QString &label,
                               NodeType type,
                               const QPointF &pos,
                               QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_nodeId(nodeId)
    , m_label(label)
    , m_type(type)
{
    setPos(pos);
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);

    if (m_type == NodeDot) {
        m_width = 14;
        m_height = 14;
    } else if (m_type == NodeBackdrop) {
        m_width = 320;
        m_height = 220;
        setZValue(-10); // Backdrops stay behind nodes
    } else {
        // Standard Natron Node dimensions (NodeGui.cpp)
        m_width = 110;
        m_height = 34;
    }

    computeColors();
    setupPorts();
}

void NatronNodeItem::computeColors()
{
    switch (m_type) {
    case NodeReader:    m_nodeColor = COLOR_READER;    m_headerColor = COLOR_READER.darker(120); break;
    case NodeWriter:    m_nodeColor = COLOR_WRITER;    m_headerColor = COLOR_WRITER.darker(120); break;
    case NodeMerge:     m_nodeColor = COLOR_MERGE;     m_headerColor = COLOR_MERGE.darker(120); break;
    case NodeGrade:     m_nodeColor = COLOR_COLOR;     m_headerColor = COLOR_COLOR.darker(120); break;
    case NodeBlur:      m_nodeColor = COLOR_FILTER;    m_headerColor = COLOR_FILTER.darker(120); break;
    case NodeKeyer:     m_nodeColor = COLOR_KEYER;     m_headerColor = COLOR_KEYER.darker(120); break;
    case NodeTransform:
    case NodeTracker:   m_nodeColor = COLOR_TRANSFORM; m_headerColor = COLOR_TRANSFORM.darker(120); break;
    case NodeRoto:      m_nodeColor = COLOR_ROTO;      m_headerColor = COLOR_ROTO.darker(120); break;
    case NodeDot:       m_nodeColor = COLOR_DOT;       m_headerColor = COLOR_DOT; break;
    case NodeBackdrop:  m_nodeColor = COLOR_BACKDROP;  m_headerColor = QColor(60, 75, 95); break;
    default:            m_nodeColor = COLOR_DEFAULT;   m_headerColor = COLOR_DEFAULT.darker(120); break;
    }
}

void NatronNodeItem::setupPorts()
{
    m_inputs.clear();

    if (m_type == NodeDot) {
        m_inputs.append(InputPort{QStringLiteral("in"), false, QPointF(m_width / 2.0, 0)});
        return;
    }
    if (m_type == NodeBackdrop) {
        return;
    }

    if (m_type == NodeMerge) {
        // Natron Merge: B (Background - top center/left), A (Foreground - top right), Mask (Side right)
        m_inputs.append(InputPort{QStringLiteral("B"), false, QPointF(m_width * 0.35, 0)});
        m_inputs.append(InputPort{QStringLiteral("A"), false, QPointF(m_width * 0.70, 0)});
        m_inputs.append(InputPort{QStringLiteral("mask"), true, QPointF(m_width, m_height / 2.0)});
    } else if (m_type == NodeKeyer) {
        // Keyer: Source (top), Bg (top right), InM / Mask (side)
        m_inputs.append(InputPort{QStringLiteral("Source"), false, QPointF(m_width * 0.40, 0)});
        m_inputs.append(InputPort{QStringLiteral("Bg"), false, QPointF(m_width * 0.75, 0)});
        m_inputs.append(InputPort{QStringLiteral("mask"), true, QPointF(m_width, m_height / 2.0)});
    } else if (m_type == NodeGrade || m_type == NodeBlur || m_type == NodeTransform) {
        // Filter nodes: Main input (top center), Mask input (side right)
        m_inputs.append(InputPort{QStringLiteral("Source"), false, QPointF(m_width / 2.0, 0)});
        m_inputs.append(InputPort{QStringLiteral("mask"), true, QPointF(m_width, m_height / 2.0)});
    } else if (m_type != NodeReader) {
        // Standard single-input node
        m_inputs.append(InputPort{QStringLiteral("Source"), false, QPointF(m_width / 2.0, 0)});
    }
}

QRectF NatronNodeItem::boundingRect() const
{
    return QRectF(-6, -6, m_width + 12, m_height + 12);
}

QPainterPath NatronNodeItem::shape() const
{
    QPainterPath path;
    if (m_type == NodeDot) {
        path.addEllipse(0, 0, m_width, m_height);
    } else {
        path.addRoundedRect(0, 0, m_width, m_height, 2, 2);
    }
    return path;
}

QPointF NatronNodeItem::inputSlotPos(int index) const
{
    if (index >= 0 && index < m_inputs.size()) {
        return mapToScene(m_inputs[index].localPos);
    }
    return mapToScene(QPointF(m_width / 2.0, 0));
}

QPointF NatronNodeItem::outputSlotPos() const
{
    if (m_type == NodeDot) {
        return mapToScene(QPointF(m_width / 2.0, m_height / 2.0));
    }
    return mapToScene(QPointF(m_width / 2.0, m_height));
}

void NatronNodeItem::setParams(const QJsonObject &params)
{
    m_params = params;
    update();
}

void NatronNodeItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    Q_UNUSED(event)
    m_hovered = true;
    update();
}

void NatronNodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    Q_UNUSED(event)
    m_hovered = false;
    update();
}

void NatronNodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    Q_UNUSED(event)
    if (auto *view = qobject_cast<NatronNodeGraphView *>(scene()->views().value(0))) {
        Q_EMIT view->nodeDoubleClicked(m_nodeId, m_params);
    }
}

void NatronNodeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    painter->setRenderHint(QPainter::Antialiasing, true);

    // ── Dot Node (Natron junction point) ────────────────────────────────────
    if (m_type == NodeDot) {
        painter->setPen(isSelected() ? QPen(QColor(243, 137, 0), 2) : QPen(QColor(70, 70, 70), 1));
        painter->setBrush(isSelected() ? QColor(243, 137, 0) : m_nodeColor);
        painter->drawEllipse(0, 0, m_width, m_height);
        return;
    }

    // ── Backdrop Node (Natron BackdropGui container) ────────────────────────
    if (m_type == NodeBackdrop) {
        // Main container rect
        QRectF bdRect(0, 0, m_width, m_height);
        painter->setPen(isSelected() ? QPen(QColor(243, 137, 0), 2) : QPen(QColor(80, 95, 115), 1.5));
        painter->setBrush(m_nodeColor);
        painter->drawRoundedRect(bdRect, 4, 4);

        // Backdrop header title bar
        QRectF headerRect(0, 0, m_width, 24);
        painter->setBrush(m_headerColor);
        painter->setPen(Qt::NoPen);
        QPainterPath headerPath;
        headerPath.addRoundedRect(headerRect, 4, 4);
        painter->drawPath(headerPath);

        // Header Title
        painter->setPen(Qt::white);
        QFont f = painter->font();
        f.setBold(true);
        f.setPointSize(9.5);
        painter->setFont(f);
        painter->drawText(headerRect.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, m_label);

        // Triangular Resize Handle in bottom-right corner (_resizeHandle in BackdropGui.cpp)
        QPolygonF resizeHandle;
        resizeHandle << QPointF(m_width - 12, m_height)
                     << QPointF(m_width, m_height - 12)
                     << QPointF(m_width, m_height);
        painter->setBrush(QColor(140, 155, 175));
        painter->setPen(Qt::NoPen);
        painter->drawPolygon(resizeHandle);
        return;
    }

    // ── Standard Natron Solid Node Body (NodeGui.cpp / NodeGraphRectItem) ───
    QRectF bodyRect(0, 0, m_width, m_height);

    // Subtle drop shadow behind node
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 110));
    painter->drawRoundedRect(bodyRect.translated(2, 2), 3, 3);

    // Node Body Fill (Solid Natron Category Color)
    painter->setBrush(m_nodeColor);

    if (isSelected()) {
        // Natron's signature amber selection outline (#f38900, 2px)
        painter->setPen(QPen(QColor(243, 137, 0), 2));
    } else if (m_hovered) {
        painter->setPen(QPen(QColor(220, 220, 220), 1.2));
    } else {
        painter->setPen(QPen(QColor(40, 40, 40), 1));
    }
    painter->drawRoundedRect(bodyRect, 3, 3);

    // Node Label (Crisp bold text with shadow for maximum readability)
    QFont font = painter->font();
    font.setBold(true);
    font.setPointSize(8.5);
    font.setFamily(QStringLiteral("sans-serif"));
    painter->setFont(font);

    // Text drop shadow
    painter->setPen(QColor(0, 0, 0, 160));
    painter->drawText(bodyRect.translated(1, 1), Qt::AlignCenter, m_label);

    // Foreground white label
    painter->setPen(Qt::white);
    painter->drawText(bodyRect, Qt::AlignCenter, m_label);

    // Port input indicators (Top port names like "A", "B")
    if (m_inputs.size() > 1) {
        font.setPointSize(6.5);
        font.setBold(true);
        painter->setFont(font);
        for (int i = 0; i < m_inputs.size(); i++) {
            const InputPort &port = m_inputs[i];
            if (!port.isMask && !port.name.isEmpty()) {
                painter->setPen(QColor(255, 255, 255, 200));
                painter->drawText(QRectF(port.localPos.x() - 8, port.localPos.y() + 2, 16, 10), Qt::AlignCenter, port.name);
            }
        }
    }

    // Mask indicator badge on the right ("M" for mask inputs)
    for (int i = 0; i < m_inputs.size(); i++) {
        const InputPort &port = m_inputs[i];
        if (port.isMask) {
            painter->setPen(QPen(QColor(40, 40, 40), 1));
            painter->setBrush(QColor(200, 200, 240));
            painter->drawEllipse(port.localPos, 3.5, 3.5);

            font.setPointSize(6.5);
            font.setBold(true);
            painter->setFont(font);
            painter->setPen(QColor(255, 255, 255, 220));
            painter->drawText(QRectF(port.localPos.x() - 14, port.localPos.y() - 6, 10, 12), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("M"));
        }
    }

    // Expression Indicator Badge (Top-right 'E' - NodeGuiIndicator in Natron)
    if (m_hasExpression) {
        QRectF exprRect(m_width - 12, -4, 12, 12);
        painter->setPen(QPen(QColor(30, 80, 30), 1));
        painter->setBrush(QColor(60, 180, 75));
        painter->drawEllipse(exprRect);

        font.setPointSize(6.5);
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(Qt::white);
        painter->drawText(exprRect, Qt::AlignCenter, QStringLiteral("E"));
    }

    // Disabled / Bypassed State (Natron's signature Red 'X' cross from corners)
    if (m_disabled) {
        painter->setPen(QPen(QColor(235, 45, 45, 230), 2.5));
        painter->drawLine(QPointF(0, 0), QPointF(m_width, m_height));
        painter->drawLine(QPointF(0, m_height), QPointF(m_width, 0));
    }
}

QVariant NatronNodeItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionHasChanged && scene()) {
        const auto items = scene()->items();
        for (auto *item : items) {
            if (auto *edge = dynamic_cast<NatronEdgeItem *>(item)) {
                if (edge->sourceNode() == this || edge->destNode() == this) {
                    edge->updatePositions();
                }
            }
        }
    }
    return QGraphicsItem::itemChange(change, value);
}

// ── NatronEdgeItem Implementation (Directional Connection Wires) ───────────

NatronEdgeItem::NatronEdgeItem(NatronNodeItem *sourceNode, NatronNodeItem *destNode, int destInputIndex)
    : m_source(sourceNode)
    , m_dest(destNode)
    , m_destInputIndex(destInputIndex)
{
    setZValue(-1); // Connections stay behind nodes
    setAcceptHoverEvents(true);

    if (m_dest && m_destInputIndex < m_dest->inputPortCount()) {
        m_isMask = m_dest->inputPort(m_destInputIndex).isMask;
    }
    updatePositions();
}

void NatronEdgeItem::updatePositions()
{
    if (!m_source || !m_dest) return;

    prepareGeometryChange();
    m_sourcePoint = m_source->outputSlotPos();
    m_destPoint = m_dest->inputSlotPos(m_destInputIndex);
    update();
}

void NatronEdgeItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    Q_UNUSED(event)
    m_hovered = true;
    update();
}

void NatronEdgeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    Q_UNUSED(event)
    m_hovered = false;
    update();
}

QRectF NatronEdgeItem::boundingRect() const
{
    qreal extra = 20.0;
    return QRectF(m_sourcePoint, m_destPoint).normalized().adjusted(-extra, -extra, extra, extra);
}

QPainterPath NatronEdgeItem::shape() const
{
    QPainterPath path;
    path.moveTo(m_sourcePoint);

    qreal dy = m_destPoint.y() - m_sourcePoint.y();
    if (m_isMask) {
        // Mask inputs curve sideways into the side port
        QPointF c1(m_sourcePoint.x(), m_sourcePoint.y() + qMax(20.0, dy * 0.5));
        QPointF c2(m_destPoint.x() + 30.0, m_destPoint.y());
        path.cubicTo(c1, c2, m_destPoint);
    } else {
        // Main flow curves downward into top ports
        qreal ctrlOffsetY = qMax(25.0, std::abs(dy) * 0.45);
        QPointF c1(m_sourcePoint.x(), m_sourcePoint.y() + ctrlOffsetY);
        QPointF c2(m_destPoint.x(), m_destPoint.y() - ctrlOffsetY);
        path.cubicTo(c1, c2, m_destPoint);
    }
    return path;
}

void NatronEdgeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    if (!m_source || !m_dest) return;

    painter->setRenderHint(QPainter::Antialiasing, true);

    QPainterPath path = shape();

    bool isSelected = (m_source && m_source->isSelected()) || (m_dest && m_dest->isSelected());

    if (m_isMask) {
        // Mask input: Dashed cyan line (matching Natron Edge.cpp paintWithDash)
        QPen maskPen(isSelected ? Qt::white : (m_hovered ? QColor(100, 220, 255) : QColor(130, 170, 210, 210)), 1.5, Qt::DashLine);
        QVector<qreal> dashes;
        dashes << 3 << 4;
        maskPen.setDashPattern(dashes);
        painter->setPen(maskPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    } else {
        // Standard data connection: Solid wire (matching Natron Edge.cpp)
        QPen wirePen(isSelected ? Qt::white : (m_hovered ? QColor(80, 220, 100) : QColor(170, 170, 170, 230)), 1.8);
        painter->setPen(wirePen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    }

    // Directional LinkArrow at midpoint (LinkArrow::paint in Natron Edge.cpp)
    qreal t = 0.5;
    QPointF midPoint = path.pointAtPercent(t);
    qreal angle = path.angleAtPercent(t);

    painter->save();
    painter->translate(midPoint);
    painter->rotate(-angle);

    painter->setPen(Qt::NoPen);
    painter->setBrush(isSelected ? Qt::white : (m_isMask ? QColor(140, 180, 220) : QColor(210, 210, 210)));
    QPolygonF arrowHead;
    arrowHead << QPointF(5, 0) << QPointF(-4, -3.5) << QPointF(-2, 0) << QPointF(-4, 3.5);
    painter->drawPolygon(arrowHead);
    painter->restore();

    // Bend Point indicator when hovered (paintBendPoint in Natron Edge.cpp)
    if (m_hovered) {
        painter->setPen(QPen(QColor(40, 40, 40), 1));
        painter->setBrush(Qt::yellow);
        painter->drawEllipse(midPoint, 4, 4);
    }
}

// ── NatronNodeGraphView Implementation (Canvas & Dot Grid) ──────────────────

NatronNodeGraphView::NatronNodeGraphView(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    m_scene->setSceneRect(-2000, -2000, 4000, 4000);

    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setDragMode(QGraphicsView::RubberBandDrag);

    setStyleSheet(QStringLiteral("border: none; background-color: #1a1a1a;"));

    // Connect node selection signal
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this]() {
        const auto selected = m_scene->selectedItems();
        if (!selected.isEmpty()) {
            if (auto *node = dynamic_cast<NatronNodeItem *>(selected.first())) {
                Q_EMIT nodeSelected(node->nodeId(), node->params());
            }
        }
    });

    // Load default professional chroma-key compositing tree
    loadGraphFromPipeline(QStringLiteral("chroma_key"), {});
}

void NatronNodeGraphView::clearGraph()
{
    m_edges.clear();
    m_nodes.clear();
    m_scene->clear();
}

void NatronNodeGraphView::addNode(const QString &id,
                                  const QString &label,
                                  NatronNodeItem::NodeType type,
                                  const QPointF &pos,
                                  const QJsonObject &params)
{
    if (m_nodes.contains(id)) return;

    auto *node = new NatronNodeItem(id, label, type, pos);
    node->setParams(params);
    m_scene->addItem(node);
    m_nodes[id] = node;
}

void NatronNodeGraphView::connectNodes(const QString &sourceId, const QString &destId, int destInput)
{
    if (!m_nodes.contains(sourceId) || !m_nodes.contains(destId)) return;

    auto *source = m_nodes[sourceId];
    auto *dest = m_nodes[destId];

    auto *edge = new NatronEdgeItem(source, dest, destInput);
    m_scene->addItem(edge);
    m_edges.append(edge);
}

void NatronNodeGraphView::loadGraphFromPipeline(const QString &pipeline, const QJsonObject &params)
{
    clearGraph();

    if (pipeline == QStringLiteral("chroma_key")) {
        // Professional Chroma Keying Node Graph Topology:
        // Read1 (FG Green Screen) -> Keyer1 (ChromaKeyer) -> Merge1 (Over) -> Write1
        // Read2 (BG Plate) -> ColorCorrect1 -> Merge1 (B input)

        addNode(QStringLiteral("Backdrop1"), QStringLiteral("Keying & Despill"), NatronNodeItem::NodeBackdrop, QPointF(-40, -10));
        if (auto *bd = m_nodes.value(QStringLiteral("Backdrop1"))) {
            bd->setBackdropSize(320, 240);
        }

        addNode(QStringLiteral("Read_FG"), QStringLiteral("Read_Foreground"), NatronNodeItem::NodeReader, QPointF(0, 30));
        addNode(QStringLiteral("Read_BG"), QStringLiteral("Read_Background"), NatronNodeItem::NodeReader, QPointF(240, 30));

        QJsonObject keyerParams = params;
        if (keyerParams.isEmpty()) {
            keyerParams[QStringLiteral("keyColor")] = QStringLiteral("#00ff00");
            keyerParams[QStringLiteral("screenGain")] = 1.0;
            keyerParams[QStringLiteral("despill")] = 0.8;
        }
        addNode(QStringLiteral("ChromaKeyer1"), QStringLiteral("ChromaKeyer"), NatronNodeItem::NodeKeyer, QPointF(0, 130), keyerParams);

        addNode(QStringLiteral("ColorCorrect1"), QStringLiteral("ColorCorrect"), NatronNodeItem::NodeGrade, QPointF(240, 130));
        addNode(QStringLiteral("Merge1"), QStringLiteral("Merge (Over)"), NatronNodeItem::NodeMerge, QPointF(120, 240));
        addNode(QStringLiteral("Write1"), QStringLiteral("Write_Output"), NatronNodeItem::NodeWriter, QPointF(120, 340));

        // Connect graph
        connectNodes(QStringLiteral("Read_FG"), QStringLiteral("ChromaKeyer1"), 0);
        connectNodes(QStringLiteral("Read_BG"), QStringLiteral("ColorCorrect1"), 0);
        connectNodes(QStringLiteral("ColorCorrect1"), QStringLiteral("Merge1"), 0); // B (Background)
        connectNodes(QStringLiteral("ChromaKeyer1"), QStringLiteral("Merge1"), 1); // A (Foreground)
        connectNodes(QStringLiteral("Merge1"), QStringLiteral("Write1"), 0);

    } else if (pipeline == QStringLiteral("rotoscope")) {
        // Rotoscope Graph Topology:
        // Read1 -> Roto1 -> Transform1 -> Write1
        addNode(QStringLiteral("Read1"), QStringLiteral("Read_Plate"), NatronNodeItem::NodeReader, QPointF(50, 30));
        addNode(QStringLiteral("Roto1"), QStringLiteral("Roto"), NatronNodeItem::NodeRoto, QPointF(50, 120), params);
        addNode(QStringLiteral("Transform1"), QStringLiteral("Transform"), NatronNodeItem::NodeTransform, QPointF(50, 200));
        addNode(QStringLiteral("Write1"), QStringLiteral("Write_Render"), NatronNodeItem::NodeWriter, QPointF(50, 290));

        connectNodes(QStringLiteral("Read1"), QStringLiteral("Roto1"), 0);
        connectNodes(QStringLiteral("Roto1"), QStringLiteral("Transform1"), 0);
        connectNodes(QStringLiteral("Transform1"), QStringLiteral("Write1"), 0);

    } else {
        // Generic Composite Graph
        addNode(QStringLiteral("Read1"), QStringLiteral("Read_Media"), NatronNodeItem::NodeReader, QPointF(50, 30));
        addNode(QStringLiteral("Grade1"), QStringLiteral("Grade"), NatronNodeItem::NodeGrade, QPointF(50, 120), params);
        addNode(QStringLiteral("Blur1"), QStringLiteral("Blur"), NatronNodeItem::NodeBlur, QPointF(50, 200));
        addNode(QStringLiteral("Write1"), QStringLiteral("Write_Final"), NatronNodeItem::NodeWriter, QPointF(50, 290));

        connectNodes(QStringLiteral("Read1"), QStringLiteral("Grade1"), 0);
        connectNodes(QStringLiteral("Grade1"), QStringLiteral("Blur1"), 0);
        connectNodes(QStringLiteral("Blur1"), QStringLiteral("Write1"), 0);
    }

    centerOn(120, 180);
}

// ── Natron Signature Dot Grid Background ────────────────────────────────────

void NatronNodeGraphView::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->setRenderHint(QPainter::Antialiasing, false);

    // Dark canvas background
    painter->fillRect(rect, QColor(26, 26, 26));

    const qreal gridSize = 20.0;
    const qreal majorGridSize = 100.0;

    qreal left = std::floor(rect.left() / gridSize) * gridSize;
    qreal top = std::floor(rect.top() / gridSize) * gridSize;

    // Minor dot grid (20px pitch)
    painter->setPen(QColor(48, 48, 48));
    for (qreal x = left; x <= rect.right(); x += gridSize) {
        for (qreal y = top; y <= rect.bottom(); y += gridSize) {
            painter->drawPoint(QPointF(x, y));
        }
    }

    // Major grid crosses (100px pitch)
    qreal majorLeft = std::floor(rect.left() / majorGridSize) * majorGridSize;
    qreal majorTop = std::floor(rect.top() / majorGridSize) * majorGridSize;

    painter->setPen(QColor(68, 68, 68));
    for (qreal x = majorLeft; x <= rect.right(); x += majorGridSize) {
        for (qreal y = majorTop; y <= rect.bottom(); y += majorGridSize) {
            painter->drawLine(QPointF(x - 2, y), QPointF(x + 2, y));
            painter->drawLine(QPointF(x, y - 2), QPointF(x, y + 2));
        }
    }
}

// ── Navigation & Key Interactions ──────────────────────────────────────────

void NatronNodeGraphView::wheelEvent(QWheelEvent *event)
{
    qreal factor = (event->angleDelta().y() > 0) ? 1.15 : 0.85;
    scale(factor, factor);
    event->accept();
}

void NatronNodeGraphView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier))) {
        m_panning = true;
        m_lastPanPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void NatronNodeGraphView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        QPoint delta = event->pos() - m_lastPanPos;
        m_lastPanPos = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void NatronNodeGraphView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void NatronNodeGraphView::keyPressEvent(QKeyEvent *event)
{
    // Natron 'D' key shortcut to toggle disable/bypass on selected nodes
    if (event->key() == Qt::Key_D) {
        const auto selected = m_scene->selectedItems();
        for (auto *item : selected) {
            if (auto *node = dynamic_cast<NatronNodeItem *>(item)) {
                node->toggleDisabled();
            }
        }
        event->accept();
        return;
    }

    // Natron 'F' key shortcut to fit in view
    if (event->key() == Qt::Key_F) {
        zoomFit();
        event->accept();
        return;
    }

    QGraphicsView::keyPressEvent(event);
}

void NatronNodeGraphView::zoomIn()
{
    scale(1.2, 1.2);
}

void NatronNodeGraphView::zoomOut()
{
    scale(0.8, 0.8);
}

void NatronNodeGraphView::zoomFit()
{
    if (!m_nodes.isEmpty()) {
        fitInView(m_scene->itemsBoundingRect().adjusted(-40, -40, 40, 40), Qt::KeepAspectRatio);
    }
}
