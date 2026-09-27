/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Interactive Natron Node Graph View Implementation
 */

#include "natronnodegraphview.h"
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QGraphicsSceneMouseEvent>
#include <QLinearGradient>
#include <QtMath>

// ── NatronNodeItem ──────────────────────────────────────────────────────────

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

    // Color theme matching Natron / Nuke conventions
    switch (type) {
    case NodeReader:
        m_headerColor = QColor(200, 160, 60);  // Gold
        m_bodyColor   = QColor(50, 45, 35);
        break;
    case NodeWriter:
        m_headerColor = QColor(190, 80, 80);   // Rust red
        m_bodyColor   = QColor(48, 35, 35);
        break;
    case NodeKeyer:
        m_headerColor = QColor(70, 170, 100);  // Emerald green
        m_bodyColor   = QColor(32, 45, 36);
        break;
    case NodeRoto:
        m_headerColor = QColor(160, 90, 190);  // Purple
        m_bodyColor   = QColor(42, 35, 48);
        break;
    case NodeGrade:
        m_headerColor = QColor(70, 130, 200);  // Blue
        m_bodyColor   = QColor(32, 40, 50);
        break;
    case NodeTransform:
    case NodeTracker:
        m_headerColor = QColor(200, 110, 50);  // Orange
        m_bodyColor   = QColor(48, 38, 30);
        break;
    case NodeMerge:
        m_headerColor = QColor(70, 170, 170);  // Teal
        m_bodyColor   = QColor(30, 45, 45);
        break;
    default:
        m_headerColor = QColor(130, 130, 130);
        m_bodyColor   = QColor(40, 40, 40);
        break;
    }
}

QRectF NatronNodeItem::boundingRect() const
{
    return QRectF(-2, -2, m_width + 4, m_height + 4);
}

QPointF NatronNodeItem::inputSlotPos(int index) const
{
    // Inputs are on top edge
    int count = 1;
    if (m_type == NodeMerge) count = 3;
    qreal step = m_width / (count + 1.0);
    return mapToScene(QPointF((index + 1) * step, 0));
}

QPointF NatronNodeItem::outputSlotPos() const
{
    // Output is at bottom center
    return mapToScene(QPointF(m_width / 2.0, m_height));
}

void NatronNodeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    painter->setRenderHint(QPainter::Antialiasing, true);

    // Node body
    QRectF bodyRect(0, 0, m_width, m_height);
    painter->setPen(isSelected() ? QPen(QColor(255, 200, 80), 2) : QPen(QColor(70, 70, 70), 1));
    painter->setBrush(m_bodyColor);
    painter->drawRoundedRect(bodyRect, 6, 6);

    // Node header
    QRectF headerRect(0, 0, m_width, 20);
    QPainterPath headerPath;
    headerPath.addRoundedRect(headerRect, 6, 6);
    painter->setPen(Qt::NoPen);
    painter->setBrush(m_headerColor);
    painter->drawPath(headerPath);

    // Label text
    painter->setPen(Qt::white);
    QFont font = painter->font();
    font.setBold(true);
    font.setPointSize(9);
    painter->setFont(font);
    painter->drawText(headerRect.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, m_label);

    // Node ID / Type subtitle in body
    font.setBold(false);
    font.setPointSize(8);
    painter->setFont(font);
    painter->setPen(QColor(180, 180, 180));
    QRectF subRect(6, 22, m_width - 12, m_height - 24);
    painter->drawText(subRect, Qt::AlignVCenter | Qt::AlignLeft, m_nodeId);

    // Input slot dots (top)
    int inputCount = (m_type == NodeMerge) ? 3 : 1;
    qreal inStep = m_width / (inputCount + 1.0);
    painter->setPen(QColor(40, 40, 40));
    painter->setBrush(QColor(220, 220, 220));
    for (int i = 0; i < inputCount; i++) {
        painter->drawEllipse(QPointF((i + 1) * inStep, 0), 3.5, 3.5);
    }

    // Output slot dot (bottom)
    if (m_type != NodeWriter) {
        painter->drawEllipse(QPointF(m_width / 2.0, m_height), 3.5, 3.5);
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

// ── NatronEdgeItem ──────────────────────────────────────────────────────────

NatronEdgeItem::NatronEdgeItem(NatronNodeItem *sourceNode, NatronNodeItem *destNode, int destInputIndex)
    : m_source(sourceNode)
    , m_dest(destNode)
    , m_destInputIndex(destInputIndex)
{
    setZValue(-1);
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

QRectF NatronEdgeItem::boundingRect() const
{
    qreal extra = 20;
    return QRectF(m_sourcePoint, QSizeF(m_destPoint.x() - m_sourcePoint.x(),
                                        m_destPoint.y() - m_sourcePoint.y()))
        .normalized()
        .adjusted(-extra, -extra, extra, extra);
}

void NatronEdgeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)
    if (!m_source || !m_dest) return;

    painter->setRenderHint(QPainter::Antialiasing, true);

    QPainterPath path(m_sourcePoint);
    qreal dy = m_destPoint.y() - m_sourcePoint.y();
    QPointF c1(m_sourcePoint.x(), m_sourcePoint.y() + qAbs(dy) * 0.5);
    QPointF c2(m_destPoint.x(), m_destPoint.y() - qAbs(dy) * 0.5);
    path.cubicTo(c1, c2, m_destPoint);

    // Shadow line
    painter->setPen(QPen(QColor(10, 10, 10, 120), 4));
    painter->drawPath(path);

    // Connecting wire
    painter->setPen(QPen(QColor(200, 200, 200), 2));
    painter->drawPath(path);

    // Direction arrow at midpoint
    QPointF mid = path.pointAtPercent(0.5);
    qreal angle = path.angleAtPercent(0.5);
    painter->save();
    painter->translate(mid);
    painter->rotate(-angle);
    painter->setBrush(QColor(230, 200, 80));
    painter->setPen(Qt::NoPen);
    static const QPointF arrowHead[3] = { QPointF(0, -4), QPointF(6, 0), QPointF(0, 4) };
    painter->drawPolygon(arrowHead, 3);
    painter->restore();
}

// ── NatronNodeGraphView ─────────────────────────────────────────────────────

NatronNodeGraphView::NatronNodeGraphView(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing, true);
    setDragMode(QGraphicsView::RubberBandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    setBackgroundBrush(QColor(28, 28, 30));
    setSceneRect(-5000, -5000, 10000, 10000);

    // Build default initial node graph
    loadGraphFromPipeline(QStringLiteral("chroma_key"), {});
}

void NatronNodeGraphView::drawBackground(QPainter *painter, const QRectF &rect)
{
    QGraphicsView::drawBackground(painter, rect);

    // Draw grid lines
    painter->setPen(QPen(QColor(42, 42, 45), 1));
    qreal gridSize = 25.0;

    qreal left = qFloor(rect.left() / gridSize) * gridSize;
    qreal top = qFloor(rect.top() / gridSize) * gridSize;

    QVector<QLineF> lines;
    for (qreal x = left; x <= rect.right(); x += gridSize) {
        lines.append(QLineF(x, rect.top(), x, rect.bottom()));
    }
    for (qreal y = top; y <= rect.bottom(); y += gridSize) {
        lines.append(QLineF(rect.left(), y, rect.right(), y));
    }
    painter->drawLines(lines);
}

void NatronNodeGraphView::wheelEvent(QWheelEvent *event)
{
    double factor = event->angleDelta().y() > 0 ? 1.15 : (1.0 / 1.15);
    scale(factor, factor);
}

void NatronNodeGraphView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        setDragMode(QGraphicsView::ScrollHandDrag);
        QMouseEvent fakeEvent(event->type(), event->position(), event->globalPosition(),
                              Qt::LeftButton, Qt::LeftButton, event->modifiers());
        QGraphicsView::mousePressEvent(&fakeEvent);
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void NatronNodeGraphView::clearGraph()
{
    m_nodes.clear();
    m_edges.clear();
    m_scene->clear();
}

void NatronNodeGraphView::addNode(const QString &id,
                                  const QString &label,
                                  NatronNodeItem::NodeType type,
                                  const QPointF &pos,
                                  const QJsonObject &params)
{
    auto *node = new NatronNodeItem(id, label, type, pos);
    node->setParams(params);
    m_scene->addItem(node);
    m_nodes[id] = node;
}

void NatronNodeGraphView::connectNodes(const QString &sourceId, const QString &destId, int destInput)
{
    if (!m_nodes.contains(sourceId) || !m_nodes.contains(destId)) return;
    auto *edge = new NatronEdgeItem(m_nodes[sourceId], m_nodes[destId], destInput);
    m_scene->addItem(edge);
    m_edges.append(edge);
}

void NatronNodeGraphView::loadGraphFromPipeline(const QString &pipeline, const QJsonObject &params)
{
    clearGraph();

    if (pipeline == QStringLiteral("chroma_key")) {
        addNode(QStringLiteral("Read1"), QStringLiteral("Read (Source)"), NatronNodeItem::NodeReader, QPointF(100, 50), params);
        addNode(QStringLiteral("Keyer1"), QStringLiteral("ChromaKeyer"), NatronNodeItem::NodeKeyer, QPointF(100, 150), params);
        addNode(QStringLiteral("Blur1"), QStringLiteral("EdgeBlur"), NatronNodeItem::NodeEffect, QPointF(100, 250), params);
        addNode(QStringLiteral("Write1"), QStringLiteral("Write (Output)"), NatronNodeItem::NodeWriter, QPointF(100, 350), params);

        connectNodes(QStringLiteral("Read1"), QStringLiteral("Keyer1"));
        connectNodes(QStringLiteral("Keyer1"), QStringLiteral("Blur1"));
        connectNodes(QStringLiteral("Blur1"), QStringLiteral("Write1"));
    } else if (pipeline == QStringLiteral("rotoscope")) {
        addNode(QStringLiteral("Read_FG"), QStringLiteral("Read (FG)"), NatronNodeItem::NodeReader, QPointF(40, 50), params);
        addNode(QStringLiteral("Read_BG"), QStringLiteral("Read (BG)"), NatronNodeItem::NodeReader, QPointF(220, 50), params);
        addNode(QStringLiteral("Roto1"), QStringLiteral("Roto / Matte"), NatronNodeItem::NodeRoto, QPointF(40, 150), params);
        addNode(QStringLiteral("Merge1"), QStringLiteral("Merge (Over)"), NatronNodeItem::NodeMerge, QPointF(130, 250), params);
        addNode(QStringLiteral("Write1"), QStringLiteral("Write (Output)"), NatronNodeItem::NodeWriter, QPointF(130, 350), params);

        connectNodes(QStringLiteral("Read_FG"), QStringLiteral("Roto1"));
        connectNodes(QStringLiteral("Read_BG"), QStringLiteral("Merge1"), 0); // B input
        connectNodes(QStringLiteral("Read_FG"), QStringLiteral("Merge1"), 1); // A input
        connectNodes(QStringLiteral("Roto1"), QStringLiteral("Merge1"), 2);   // Mask input
        connectNodes(QStringLiteral("Merge1"), QStringLiteral("Write1"));
    } else if (pipeline == QStringLiteral("color_grade")) {
        addNode(QStringLiteral("Read1"), QStringLiteral("Read (Source)"), NatronNodeItem::NodeReader, QPointF(100, 50), params);
        addNode(QStringLiteral("Grade1"), QStringLiteral("Grade (Lift/Gain)"), NatronNodeItem::NodeGrade, QPointF(100, 150), params);
        addNode(QStringLiteral("ColorCorrect1"), QStringLiteral("ColorCorrect"), NatronNodeItem::NodeGrade, QPointF(100, 250), params);
        addNode(QStringLiteral("Write1"), QStringLiteral("Write (Output)"), NatronNodeItem::NodeWriter, QPointF(100, 350), params);

        connectNodes(QStringLiteral("Read1"), QStringLiteral("Grade1"));
        connectNodes(QStringLiteral("Grade1"), QStringLiteral("ColorCorrect1"));
        connectNodes(QStringLiteral("ColorCorrect1"), QStringLiteral("Write1"));
    } else {
        // Generic Pipeline
        addNode(QStringLiteral("Read1"), QStringLiteral("Read (Source)"), NatronNodeItem::NodeReader, QPointF(100, 50), params);
        addNode(QStringLiteral("Transform1"), QStringLiteral("Transform"), NatronNodeItem::NodeTransform, QPointF(100, 150), params);
        addNode(QStringLiteral("Write1"), QStringLiteral("Write (Output)"), NatronNodeItem::NodeWriter, QPointF(100, 250), params);

        connectNodes(QStringLiteral("Read1"), QStringLiteral("Transform1"));
        connectNodes(QStringLiteral("Transform1"), QStringLiteral("Write1"));
    }

    centerOn(100, 200);
}
