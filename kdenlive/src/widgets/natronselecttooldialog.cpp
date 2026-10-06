/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Natron Select Tool Dialog Implementation — DaVinci Resolve Fusion "Select Tool" palette.
 */

#include "natronselecttooldialog.h"
#include <QIcon>
#include <QKeyEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <KLocalizedString>

class NatronToolItemDelegate : public QStyledItemDelegate
{
public:
    explicit NatronToolItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        QRect rect = option.rect;
        bool isSelected = option.state & QStyle::State_Selected;
        bool isHovered = option.state & QStyle::State_MouseOver;

        // Background
        if (isSelected) {
            painter->fillRect(rect, QColor(48, 54, 66));
            painter->setPen(QPen(QColor(220, 80, 80), 1.5));
            painter->drawRect(rect.adjusted(1, 1, -1, -1));
        } else if (isHovered) {
            painter->fillRect(rect, QColor(36, 40, 48));
        } else {
            painter->fillRect(rect, QColor(24, 26, 30));
        }

        // Icon
        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QRect iconRect(rect.left() + 10, rect.top() + (rect.height() - 20) / 2, 20, 20);
        if (!icon.isNull()) {
            icon.paint(painter, iconRect);
        } else {
            painter->setBrush(QColor(70, 80, 95));
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(iconRect, 3, 3);
        }

        // Text: Tool Name + Abbreviation badge
        QString name = index.data(Qt::DisplayRole).toString();
        QString abbr = index.data(Qt::UserRole + 1).toString();
        QString category = index.data(Qt::UserRole + 2).toString();

        painter->setPen(isSelected ? QColor(255, 255, 255) : QColor(220, 225, 230));
        QFont nameFont = option.font;
        nameFont.setBold(isSelected);
        nameFont.setPointSize(9);
        painter->setFont(nameFont);

        int textLeft = iconRect.right() + 12;
        painter->drawText(QRect(textLeft, rect.top(), 180, rect.height()), Qt::AlignVCenter | Qt::AlignLeft, name);

        // Abbreviation in parentheses or badge
        if (!abbr.isEmpty()) {
            QFont abbrFont = nameFont;
            abbrFont.setPointSize(8);
            abbrFont.setBold(false);
            painter->setFont(abbrFont);
            painter->setPen(isSelected ? QColor(255, 180, 180) : QColor(140, 150, 165));
            painter->drawText(QRect(textLeft + 185, rect.top(), 80, rect.height()), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("(%1)").arg(abbr));
        }

        // Category label on right
        if (!category.isEmpty()) {
            QFont catFont = nameFont;
            catFont.setPointSize(7.5);
            painter->setFont(catFont);
            painter->setPen(QColor(100, 110, 125));
            painter->drawText(QRect(rect.right() - 85, rect.top(), 75, rect.height()), Qt::AlignVCenter | Qt::AlignRight, category);
        }

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        Q_UNUSED(option);
        Q_UNUSED(index);
        return QSize(320, 30);
    }
};

NatronSelectToolDialog::NatronSelectToolDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Select Tool"));
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);
    setFixedSize(360, 520);

    initToolRegistry();
    setupUi();
    setupStyle();
    populateList();

    m_searchEdit->installEventFilter(this);
}

void NatronSelectToolDialog::initToolRegistry()
{
    m_toolRegistry = {
        // AI & Segmentation
        {QStringLiteral("MagicMask"), i18n("Magic Mask"), QStringLiteral("MagM"), i18n("AI"), i18n("AI Subject & Object segmentation mask"), NatronNodeItem::NodeCustom, QStringLiteral("edit-select")},
        {QStringLiteral("AIDepthMap"), i18n("AI Depth Map"), QStringLiteral("Dpth"), i18n("AI"), i18n("Monocular AI depth map estimation"), NatronNodeItem::NodeCustom, QStringLiteral("view-split-left-right")},
        {QStringLiteral("AIRelight"), i18n("AI Relight"), QStringLiteral("RLgt"), i18n("AI"), i18n("Neural 3D point and directional relighting"), NatronNodeItem::NodeCustom, QStringLiteral("light-bulb")},
        {QStringLiteral("AIFaceRetouch"), i18n("AI Face Retouch"), QStringLiteral("Face"), i18n("AI"), i18n("Neural skin smoothing and facial enhancement"), NatronNodeItem::NodeCustom, QStringLiteral("user")},
        {QStringLiteral("AIInpaint"), i18n("AI Inpaint / Clean"), QStringLiteral("Inp"), i18n("AI"), i18n("Neural background fill and wire removal"), NatronNodeItem::NodeCustom, QStringLiteral("edit-clear")},
        {QStringLiteral("AISuperRes"), i18n("AI Super Resolution"), QStringLiteral("UpSc"), i18n("AI"), i18n("Neural 2x/4x frame upscaling"), NatronNodeItem::NodeCustom, QStringLiteral("transform-scale")},

        // Core Compositing & Merging (From DaVinci Fusion & Natron)
        {QStringLiteral("Merge"), i18n("Merge"), QStringLiteral("Mrg"), i18n("Composite"), i18n("Layer compositing with blend modes (Over, Add, Screen)"), NatronNodeItem::NodeMerge, QStringLiteral("edit-copy")},
        {QStringLiteral("Merge3D"), i18n("Merge 3D"), QStringLiteral("3Mg"), i18n("3D"), i18n("3D spatial layer merging"), NatronNodeItem::NodeCustom, QStringLiteral("media-default-audio")},
        {QStringLiteral("MaterialMerge"), i18n("Material Merge"), QStringLiteral("3MM"), i18n("3D"), i18n("Material shader blending for 3D textures"), NatronNodeItem::NodeCustom, QStringLiteral("fill-color")},
        {QStringLiteral("MatteControl"), i18n("Matte Control"), QStringLiteral("Mat"), i18n("Matte"), i18n("Alpha matte adjustment, spill suppression, and choke"), NatronNodeItem::NodeCustom, QStringLiteral("color-management")},
        {QStringLiteral("ChannelBoolean"), i18n("Channel Boolean"), QStringLiteral("CBl"), i18n("Channel"), i18n("RGBA channel math and boolean operations"), NatronNodeItem::NodeCustom, QStringLiteral("accessories-calculator")},
        {QStringLiteral("Switch"), i18n("Switch"), QStringLiteral("Sw"), i18n("Utility"), i18n("Multi-input stream switcher"), NatronNodeItem::NodeDot, QStringLiteral("media-playback-start")},

        // Color & Grading
        {QStringLiteral("ColorCorrector"), i18n("Color Corrector"), QStringLiteral("CC"), i18n("Color"), i18n("Lift, Gamma, Gain, Hue & Saturation color corrector"), NatronNodeItem::NodeGrade, QStringLiteral("color-management")},
        {QStringLiteral("Grade"), i18n("Grade"), QStringLiteral("Grd"), i18n("Color"), i18n("Natron Grade (BlackPoint, WhitePoint, Gain, Offset)"), NatronNodeItem::NodeGrade, QStringLiteral("fill-color")},
        {QStringLiteral("ColorCurves"), i18n("Color Curves"), QStringLiteral("Crv"), i18n("Color"), i18n("Bezier RGB spline tonal curve adjustments"), NatronNodeItem::NodeGrade, QStringLiteral("draw-bezier-curves")},
        {QStringLiteral("HueCurves"), i18n("Hue Curves"), QStringLiteral("HCr"), i18n("Color"), i18n("Selective Hue vs Hue, Hue vs Sat curve editor"), NatronNodeItem::NodeGrade, QStringLiteral("draw-freehand")},
        {QStringLiteral("LUTApply"), i18n("LUT Apply"), QStringLiteral("LUT"), i18n("Color"), i18n("3D Cube / Look color LUT transform"), NatronNodeItem::NodeGrade, QStringLiteral("view-preview")},
        {QStringLiteral("OCIORange"), i18n("OCIO ColorSpace"), QStringLiteral("OCIO"), i18n("Color"), i18n("OpenColorIO ACES/Rec709 color conversion"), NatronNodeItem::NodeGrade, QStringLiteral("preferences-system-windows")},

        // Filters & Blurs
        {QStringLiteral("Blur"), i18n("Blur"), QStringLiteral("Blr"), i18n("Filter"), i18n("Gaussian and box blur filter"), NatronNodeItem::NodeBlur, QStringLiteral("blur-effect")},
        {QStringLiteral("MotionBlur"), i18n("Motion Blur"), QStringLiteral("MoB"), i18n("Filter"), i18n("Directional & vector motion blur"), NatronNodeItem::NodeBlur, QStringLiteral("media-seek-forward")},
        {QStringLiteral("MosaicBlur"), i18n("Mosaic Blur"), QStringLiteral("MB"), i18n("Filter"), i18n("Pixelation and mosaic blurring"), NatronNodeItem::NodeBlur, QStringLiteral("view-grid")},
        {QStringLiteral("Defocus"), i18n("Defocus"), QStringLiteral("Df"), i18n("Filter"), i18n("Optical bokeh lens defocus simulation"), NatronNodeItem::NodeBlur, QStringLiteral("camera-photo")},
        {QStringLiteral("Glow"), i18n("Glow"), QStringLiteral("Glw"), i18n("Filter"), i18n("Exponential light bloom and soft glow"), NatronNodeItem::NodeBlur, QStringLiteral("light-bulb")},
        {QStringLiteral("Sharpen"), i18n("Sharpen"), QStringLiteral("Shrp"), i18n("Filter"), i18n("High-pass unsharp mask sharpener"), NatronNodeItem::NodeBlur, QStringLiteral("edit-find")},
        {QStringLiteral("EdgeDetect"), i18n("Edge Detect"), QStringLiteral("ED"), i18n("Filter"), i18n("Sobel and Laplacian edge extraction"), NatronNodeItem::NodeBlur, QStringLiteral("draw-cross")},
        {QStringLiteral("Denoise"), i18n("Denoise"), QStringLiteral("Dn"), i18n("Filter"), i18n("Spatial & temporal grain reduction"), NatronNodeItem::NodeBlur, QStringLiteral("edit-clear")},
        {QStringLiteral("Grain"), i18n("Grain"), QStringLiteral("Grn"), i18n("Filter"), i18n("Photographic film grain simulation"), NatronNodeItem::NodeBlur, QStringLiteral("media-optical")},
        {QStringLiteral("Mirrors"), i18n("Mirrors"), QStringLiteral("Mir"), i18n("Filter"), i18n("Kaleidoscopic and mirror reflection effects"), NatronNodeItem::NodeCustom, QStringLiteral("object-flip-horizontal")},
        {QStringLiteral("Mandelbrot"), i18n("Mandelbrot"), QStringLiteral("Man"), i18n("Generator"), i18n("Procedural fractal generator"), NatronNodeItem::NodeCustom, QStringLiteral("view-radial")},

        // Keying & Mattes
        {QStringLiteral("ChromaKeyer"), i18n("Chroma Keyer"), QStringLiteral("Key"), i18n("Keyer"), i18n("Green/Blue screen chroma keyer with despill"), NatronNodeItem::NodeKeyer, QStringLiteral("color-picker")},
        {QStringLiteral("DifferenceKeyer"), i18n("Difference Keyer"), QStringLiteral("DKey"), i18n("Keyer"), i18n("Background subtraction difference keyer"), NatronNodeItem::NodeKeyer, QStringLiteral("view-split-left-right")},
        {QStringLiteral("LumaKeyer"), i18n("Luma Keyer"), QStringLiteral("LKey"), i18n("Keyer"), i18n("Luminance threshold keyer"), NatronNodeItem::NodeKeyer, QStringLiteral("adjustrgb")},
        {QStringLiteral("Despill"), i18n("Despill"), QStringLiteral("Dsp"), i18n("Keyer"), i18n("Green/Blue spill color suppression"), NatronNodeItem::NodeKeyer, QStringLiteral("color-management")},

        // Transform & Tracking
        {QStringLiteral("Transform"), i18n("Transform"), QStringLiteral("Xf"), i18n("Transform"), i18n("2D translation, rotation, scale, and skew"), NatronNodeItem::NodeTransform, QStringLiteral("transform-scale")},
        {QStringLiteral("Tracker"), i18n("Tracker"), QStringLiteral("Trk"), i18n("Transform"), i18n("Multi-point pattern motion tracker"), NatronNodeItem::NodeTracker, QStringLiteral("crosshairs")},
        {QStringLiteral("PlanarTracker"), i18n("Planar Tracker"), QStringLiteral("PTrk"), i18n("Transform"), i18n("Planar surface motion tracker and corner pin"), NatronNodeItem::NodeTracker, QStringLiteral("view-presentation")},
        {QStringLiteral("CameraTracker"), i18n("Camera Tracker"), QStringLiteral("CTrk"), i18n("Transform"), i18n("3D camera motion reconstruction"), NatronNodeItem::NodeTracker, QStringLiteral("camera-video")},
        {QStringLiteral("CornerPin"), i18n("Corner Pin"), QStringLiteral("CP"), i18n("Transform"), i18n("4-point perspective warp and match-move"), NatronNodeItem::NodeTransform, QStringLiteral("transform-shear")},
        {QStringLiteral("LensDistortion"), i18n("Lens Distortion"), QStringLiteral("LD"), i18n("Transform"), i18n("Radial and anamorphic lens distortion correction"), NatronNodeItem::NodeTransform, QStringLiteral("zoom-original")},

        // Draw & Generators
        {QStringLiteral("Roto"), i18n("Roto"), QStringLiteral("Roto"), i18n("Draw"), i18n("Vector Bezier / B-Spline rotoscoping masks"), NatronNodeItem::NodeRoto, QStringLiteral("draw-freehand")},
        {QStringLiteral("MaskPaint"), i18n("Mask Paint"), QStringLiteral("PnM"), i18n("Draw"), i18n("Vector brush painting and clone stamping"), NatronNodeItem::NodeRoto, QStringLiteral("draw-brush")},
        {QStringLiteral("TextPlus"), i18n("Text+"), QStringLiteral("Txt"), i18n("Draw"), i18n("2D/3D animated vector typography"), NatronNodeItem::NodeCustom, QStringLiteral("draw-text")},
        {QStringLiteral("RadialRamp"), i18n("Radial / Ramp"), QStringLiteral("Rmp"), i18n("Generator"), i18n("Linear and radial color gradients"), NatronNodeItem::NodeCustom, QStringLiteral("view-radial")},
        {QStringLiteral("Constant"), i18n("Constant / Solid"), QStringLiteral("Col"), i18n("Generator"), i18n("Solid RGBA color frame generator"), NatronNodeItem::NodeCustom, QStringLiteral("fill-color")},
        {QStringLiteral("ParticleEmitter"), i18n("Particle Emitter"), QStringLiteral("PE"), i18n("Generator"), i18n("2D/3D particle simulator (Smoke, sparks, dust)"), NatronNodeItem::NodeCustom, QStringLiteral("weather-clouds")},

        // IO & Topology
        {QStringLiteral("MediaIn"), i18n("Media In"), QStringLiteral("MI"), i18n("I/O"), i18n("Source video / image clip input stream"), NatronNodeItem::NodeReader, QStringLiteral("media-playback-start")},
        {QStringLiteral("MediaOut"), i18n("Media Out"), QStringLiteral("MO"), i18n("I/O"), i18n("Composited output destination back to timeline"), NatronNodeItem::NodeWriter, QStringLiteral("media-playback-stop")},
        {QStringLiteral("Dot"), i18n("Dot"), QStringLiteral("Dot"), i18n("Routing"), i18n("Routing junction dot for clean node layout"), NatronNodeItem::NodeDot, QStringLiteral("draw-donut")},
        {QStringLiteral("Backdrop"), i18n("Backdrop"), QStringLiteral("Bck"), i18n("Routing"), i18n("Visual color background container for node groups"), NatronNodeItem::NodeBackdrop, QStringLiteral("window-duplicate")}
    };
}

void NatronSelectToolDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    // Tools List View
    m_listWidget = new QListWidget(this);
    m_listWidget->setItemDelegate(new NatronToolItemDelegate(this));
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, &NatronSelectToolDialog::onToolItemDoubleClicked);
    mainLayout->addWidget(m_listWidget, 1);

    // Search filter bar at bottom with sleek red outline
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(i18n("Search tool or abbr (e.g. Mrg, MagM, Key, Trk)..."));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &NatronSelectToolDialog::filterTools);
    mainLayout->addWidget(m_searchEdit);

    // Bottom Action Buttons: [ Cancel ] [ Add ]
    auto *btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(0, 4, 0, 0);
    btnLayout->setSpacing(10);

    m_btnCancel = new QPushButton(i18n("Cancel"), this);
    m_btnCancel->setCursor(Qt::PointingHandCursor);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_btnCancel);

    m_btnAdd = new QPushButton(i18n("Add"), this);
    m_btnAdd->setDefault(true);
    m_btnAdd->setCursor(Qt::PointingHandCursor);
    connect(m_btnAdd, &QPushButton::clicked, this, &NatronSelectToolDialog::onAddClicked);
    btnLayout->addWidget(m_btnAdd);

    mainLayout->addLayout(btnLayout);
}

void NatronSelectToolDialog::setupStyle()
{
    setStyleSheet(QStringLiteral(
        "QDialog {"
        "  background-color: #1a1b20;"
        "  color: #e0e0e0;"
        "  border: 1px solid #333640;"
        "  border-radius: 6px;"
        "}"
        "QListWidget {"
        "  background-color: #141519;"
        "  border: 1px solid #282a33;"
        "  border-radius: 4px;"
        "  outline: none;"
        "}"
        "QScrollBar:vertical {"
        "  background: #141519;"
        "  width: 8px;"
        "  margin: 0px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #3e4250;"
        "  min-height: 20px;"
        "  border-radius: 4px;"
        "}"
        "QLineEdit {"
        "  background-color: #16171b;"
        "  color: #ffffff;"
        "  border: 1.5px solid #d94f4f;"
        "  border-radius: 4px;"
        "  padding: 6px 10px;"
        "  font-size: 13px;"
        "}"
        "QLineEdit:focus {"
        "  border: 1.5px solid #ff6b6b;"
        "  background-color: #1a1b20;"
        "}"
        "QPushButton {"
        "  background-color: #262830;"
        "  color: #d0d4dd;"
        "  border: 1px solid #3c404d;"
        "  border-radius: 4px;"
        "  padding: 6px 16px;"
        "  font-weight: 500;"
        "  min-width: 90px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #343742;"
        "  border-color: #555b6d;"
        "}"
        "QPushButton#addBtn, QPushButton:default {"
        "  background-color: #2a1f22;"
        "  color: #ffffff;"
        "  border: 1.5px solid #d94f4f;"
        "}"
        "QPushButton:default:hover {"
        "  background-color: #3a2228;"
        "  border-color: #ff6b6b;"
        "}"
    ));
}

void NatronSelectToolDialog::populateList(const QString &query)
{
    m_listWidget->clear();

    QString trimmed = query.trimmed().toLower();

    for (const auto &tool : m_toolRegistry) {
        if (!trimmed.isEmpty()) {
            bool matchesName = tool.name.toLower().contains(trimmed);
            bool matchesAbbr = tool.abbreviation.toLower().contains(trimmed);
            bool matchesId   = tool.id.toLower().contains(trimmed);
            bool matchesCat  = tool.category.toLower().contains(trimmed);
            if (!matchesName && !matchesAbbr && !matchesId && !matchesCat) {
                continue;
            }
        }

        auto *item = new QListWidgetItem();
        item->setText(tool.name);
        item->setIcon(QIcon::fromTheme(tool.iconName, QIcon::fromTheme(QStringLiteral("edit-select"))));
        item->setData(Qt::UserRole, tool.id);
        item->setData(Qt::UserRole + 1, tool.abbreviation);
        item->setData(Qt::UserRole + 2, tool.category);
        item->setData(Qt::UserRole + 3, tool.description);
        item->setData(Qt::UserRole + 4, static_cast<int>(tool.nodeType));
        m_listWidget->addItem(item);
    }

    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
    }
}

void NatronSelectToolDialog::filterTools(const QString &query)
{
    populateList(query);
}

void NatronSelectToolDialog::onToolItemDoubleClicked(QListWidgetItem *item)
{
    if (!item) return;
    onAddClicked();
}

void NatronSelectToolDialog::onAddClicked()
{
    auto *item = m_listWidget->currentItem();
    if (!item) return;

    QString toolId = item->data(Qt::UserRole).toString();
    for (const auto &tool : m_toolRegistry) {
        if (tool.id == toolId) {
            m_selectedTool = tool;
            break;
        }
    }

    accept();
}

NatronToolEntry NatronSelectToolDialog::selectedTool() const
{
    return m_selectedTool;
}

bool NatronSelectToolDialog::selectTool(QWidget *parent, NatronToolEntry &outTool, const QPoint &globalPos)
{
    NatronSelectToolDialog dlg(parent);
    if (!globalPos.isNull()) {
        dlg.move(globalPos);
    }
    if (dlg.exec() == QDialog::Accepted) {
        outTool = dlg.selectedTool();
        return true;
    }
    return false;
}

void NatronSelectToolDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        onAddClicked();
        return;
    }
    QDialog::keyPressEvent(event);
}

bool NatronSelectToolDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_searchEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Down) {
            int row = m_listWidget->currentRow();
            if (row < m_listWidget->count() - 1) {
                m_listWidget->setCurrentRow(row + 1);
            }
            return true;
        } else if (keyEvent->key() == Qt::Key_Up) {
            int row = m_listWidget->currentRow();
            if (row > 0) {
                m_listWidget->setCurrentRow(row - 1);
            }
            return true;
        } else if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            onAddClicked();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}
