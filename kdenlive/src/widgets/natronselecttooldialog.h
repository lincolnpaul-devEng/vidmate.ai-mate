/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Natron Select Tool Dialog — Quick Node/Tool Chooser popup matching
 * DaVinci Resolve Fusion's "Select Tool" palette (Shift+Space / Tab / Node Click).
 */

#pragma once

#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "natronnodegraphview.h"

struct NatronToolEntry {
    QString id;
    QString name;
    QString abbreviation;
    QString category;
    QString description;
    NatronNodeItem::NodeType nodeType;
    QString iconName;
};

class NatronSelectToolDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NatronSelectToolDialog(QWidget *parent = nullptr);
    ~NatronSelectToolDialog() override = default;

    NatronToolEntry selectedTool() const;

    static bool selectTool(QWidget *parent, NatronToolEntry &outTool, const QPoint &globalPos = QPoint());

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private Q_SLOTS:
    void filterTools(const QString &query);
    void onToolItemDoubleClicked(QListWidgetItem *item);
    void onAddClicked();

private:
    void initToolRegistry();
    void populateList(const QString &query = QString());
    void setupUi();
    void setupStyle();

    QLineEdit *m_searchEdit{nullptr};
    QListWidget *m_listWidget{nullptr};
    QPushButton *m_btnAdd{nullptr};
    QPushButton *m_btnCancel{nullptr};

    QList<NatronToolEntry> m_toolRegistry;
    NatronToolEntry m_selectedTool;
};
