/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Interactive Natron Node Graph View — Renders nodes, input/output connection
 * splines, parameter badges, and active compositing trees.
 */

#pragma once

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QJsonObject>
#include <QJsonArray>
#include <QPainterPath>

/**
 * @class NatronNodeItem
 * @brief Interactive visual node block on the Natron Node Graph canvas
 */
class NatronNodeItem : public QGraphicsItem
{
public:
    enum NodeType {
        NodeReader,
        NodeWriter,
        NodeKeyer,
        NodeRoto,
        NodeGrade,
        NodeTransform,
        NodeTracker,
        NodeMerge,
        NodeEffect,
        NodeCustom
    };

    NatronNodeItem(const QString &nodeId,
                   const QString &label,
                   NodeType type,
                   const QPointF &pos,
                   QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    QString nodeId() const { return m_nodeId; }
    QString label() const { return m_label; }
    NodeType nodeType() const { return m_type; }

    QPointF inputSlotPos(int index = 0) const;
    QPointF outputSlotPos() const;

    void setParams(const QJsonObject &params) { m_params = params; update(); }
    QJsonObject params() const { return m_params; }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    QString m_nodeId;
    QString m_label;
    NodeType m_type;
    QJsonObject m_params;
    QColor m_headerColor;
    QColor m_bodyColor;
    int m_width{140};
    int m_height{50};
};

/**
 * @class NatronEdgeItem
 * @brief Cubic Bézier connecting wire between node output and input slots
 */
class NatronEdgeItem : public QGraphicsItem
{
public:
    NatronEdgeItem(NatronNodeItem *sourceNode, NatronNodeItem *destNode, int destInputIndex = 0);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void updatePositions();

    NatronNodeItem *sourceNode() const { return m_source; }
    NatronNodeItem *destNode() const { return m_dest; }

private:
    NatronNodeItem *m_source{nullptr};
    NatronNodeItem *m_dest{nullptr};
    int m_destInputIndex{0};
    QPointF m_sourcePoint;
    QPointF m_destPoint;
};

/**
 * @class NatronNodeGraphView
 * @brief Interactive QGraphicsView canvas hosting Natron node graphs
 */
class NatronNodeGraphView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit NatronNodeGraphView(QWidget *parent = nullptr);
    ~NatronNodeGraphView() override = default;

    void clearGraph();
    void loadGraphFromPipeline(const QString &pipeline, const QJsonObject &params);
    void addNode(const QString &id, const QString &label, NatronNodeItem::NodeType type, const QPointF &pos, const QJsonObject &params = {});
    void connectNodes(const QString &sourceId, const QString &destId, int destInput = 0);

Q_SIGNALS:
    void nodeSelected(const QString &nodeId, const QJsonObject &params);

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QGraphicsScene *m_scene{nullptr};
    QMap<QString, NatronNodeItem *> m_nodes;
    QList<NatronEdgeItem *> m_edges;
};
