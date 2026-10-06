/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Professional Natron Node Graph View — Faithfully ported from Natron GUI
 * (NodeGui, Edge, NodeGraph, Backdrop, and LinkArrow)
 */

#pragma once

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QJsonObject>
#include <QJsonArray>
#include <QPainterPath>
#include <QMap>

/**
 * @class NatronNodeItem
 * @brief Visual node item mirroring Natron's NodeGui / NodeGraphRectItem architecture.
 */
class NatronNodeItem : public QGraphicsItem
{
public:
    enum NodeType {
        NodeReader,
        NodeWriter,
        NodeMerge,
        NodeKeyer,
        NodeRoto,
        NodeGrade,
        NodeBlur,
        NodeTransform,
        NodeTracker,
        NodeDot,
        NodeBackdrop,
        NodeCustom
    };

    struct InputPort {
        QString name;
        bool isMask{false};
        QPointF localPos;
    };

    NatronNodeItem(const QString &nodeId,
                   const QString &label,
                   NodeType type,
                   const QPointF &pos,
                   QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    QString nodeId() const { return m_nodeId; }
    QString label() const { return m_label; }
    NodeType nodeType() const { return m_type; }

    bool isDisabled() const { return m_disabled; }
    void setDisabled(bool d) { m_disabled = d; update(); }
    void toggleDisabled() { m_disabled = !m_disabled; update(); }

    bool hasExpression() const { return m_hasExpression; }
    void setHasExpression(bool expr) { m_hasExpression = expr; update(); }

    QPointF inputSlotPos(int index = 0) const;
    QPointF outputSlotPos() const;
    int inputPortCount() const { return m_inputs.size(); }
    InputPort inputPort(int index) const { return (index >= 0 && index < m_inputs.size()) ? m_inputs[index] : InputPort{}; }

    void setParams(const QJsonObject &params);
    QJsonObject params() const { return m_params; }

    void setBackdropSize(qreal w, qreal h) { m_width = w; m_height = h; update(); }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;

private:
    void setupPorts();
    void computeColors();

    QString m_nodeId;
    QString m_label;
    NodeType m_type;
    QJsonObject m_params;
    QVector<InputPort> m_inputs;

    QColor m_nodeColor;
    QColor m_headerColor;

    qreal m_width{110};
    qreal m_height{36};
    bool m_disabled{false};
    bool m_hovered{false};
    bool m_hasExpression{false};
};

/**
 * @class NatronEdgeItem
 * @brief Directional connector wire matching Natron's Edge.cpp and LinkArrow.
 */
class NatronEdgeItem : public QGraphicsItem
{
public:
    NatronEdgeItem(NatronNodeItem *sourceNode, NatronNodeItem *destNode, int destInputIndex = 0);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void updatePositions();

    NatronNodeItem *sourceNode() const { return m_source; }
    NatronNodeItem *destNode() const { return m_dest; }
    int destInputIndex() const { return m_destInputIndex; }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

private:
    NatronNodeItem *m_source{nullptr};
    NatronNodeItem *m_dest{nullptr};
    int m_destInputIndex{0};
    QPointF m_sourcePoint;
    QPointF m_destPoint;
    bool m_isMask{false};
    bool m_hovered{false};
};

/**
 * @class NatronNodeGraphView
 * @brief Node Graph Canvas featuring Natron's dot grid, smooth zoom/pan,
 * and node execution topology.
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

    void zoomIn();
    void zoomOut();
    void zoomFit();

    void openSelectToolDialog(const QPointF &scenePos = QPointF());
    NatronNodeItem *insertToolNode(const QString &toolId, const QString &label, NatronNodeItem::NodeType type, const QPointF &pos = QPointF());
    void insertNodeBetween(const QString &toolId, const QString &label, NatronNodeItem::NodeType type, const QString &sourceNodeId, const QString &destNodeId);

Q_SIGNALS:
    void nodeSelected(const QString &nodeId, const QJsonObject &params);
    void nodeDoubleClicked(const QString &nodeId, const QJsonObject &params);
    void toolNodeInserted(const QString &nodeId, const QString &toolId);

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QGraphicsScene *m_scene{nullptr};
    QMap<QString, NatronNodeItem *> m_nodes;
    QList<NatronEdgeItem *> m_edges;

    bool m_panning{false};
    QPoint m_lastPanPos;
    int m_nodeCounter{1};
};
