#include "properties_panel.h"

#include "drawing_aids_view.h"
#include "hot_text.h"
#include "phoenix_view.h"

#include "../edit/drawing_style.h"

#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QSignalBlocker>

#include <algorithm>

void PropertiesPanel::addDrawingAidOptions(
    const std::function<void(const QString&, QWidget*, const std::vector<ToolIcons::Tool>&)>& addOption)
{
    using IconTool = ToolIcons::Tool;
    namespace aids = fla::aids;
    const std::vector<IconTool> freehand = {IconTool::Pencil, IconTool::PaintBrush, IconTool::FluidBrush,
        IconTool::Brush, IconTool::InkBrush, IconTool::Eraser, IconTool::DrawingGuides};
    const std::vector<IconTool> guidesTool = {IconTool::DrawingGuides};

    // Symmetry: the document's axis, which every freehand tool draws across.
    QComboBox* symmetryMode = new QComboBox(this);
    symmetryMode->addItems({"Off", "Mirror left and right", "Mirror top and bottom", "Mirror four ways", "Radial"});
    symmetryMode->setToolTip("Copy each stroke across the symmetry axis as you draw. Move the axis with the Drawing "
                             "Guides tool");
    connect(symmetryMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!_links.view || index < 0)
            return;
        aids::Symmetry symmetry = drawaids::symmetry(*_links.view);
        symmetry.on = index > 0;
        if (index > 0)
            symmetry.mode = static_cast<aids::SymmetryMode>(index - 1);
        drawaids::setSymmetry(*_links.view, symmetry);
        emit toolSettingChanged("symmetry");
    });
    addOption("Symmetry", symmetryMode, freehand);

    HotText* copies = makeHotText(2, 64, 0);
    copies->setToolTip("Radial symmetry: how many times each stroke is drawn round the center");
    connect(copies, &HotText::valueChanged, this, [this](double value, bool) {
        if (!_links.view)
            return;
        aids::Symmetry symmetry = drawaids::symmetry(*_links.view);
        symmetry.copies = std::clamp(static_cast<int>(value), 2, 64);
        drawaids::setSymmetry(*_links.view, symmetry);
        emit toolSettingChanged("symmetry");
    });
    addOption("Copies", copies, freehand);

    QCheckBox* mirrorCopies = new QCheckBox("Mirror copies", this);
    mirrorCopies->setToolTip("Radial symmetry: mirror each copy as well, as a kaleidoscope does");
    connect(mirrorCopies, &QCheckBox::toggled, this, [this](bool on) {
        if (!_links.view)
            return;
        aids::Symmetry symmetry = drawaids::symmetry(*_links.view);
        symmetry.mirror = on;
        drawaids::setSymmetry(*_links.view, symmetry);
        emit toolSettingChanged("symmetry");
    });
    addOption(QString(), mirrorCopies, freehand);

    // The Drawing Guides tool's own.
    QComboBox* guideKind = new QComboBox(this);
    for (int kind = 0; kind <= static_cast<int>(aids::GuideKind::Perspective3); ++kind)
        guideKind->addItem(aids::kindName(static_cast<aids::GuideKind>(kind)));
    guideKind->setCurrentIndex(std::clamp(_links.style->drawingGuideKind, 0, guideKind->count() - 1));
    guideKind->setToolTip("The guide a click on empty stage adds");
    connect(guideKind, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0)
            _links.style->drawingGuideKind = index;
    });
    addOption("Add", guideKind, guidesTool);

    const auto changeGuides = [this](const std::function<void(aids::DrawingGuides&)>& change) {
        if (!_links.view)
            return;
        aids::DrawingGuides guides = drawaids::guides(*_links.view);
        change(guides);
        drawaids::setGuides(*_links.view, guides);
        emit toolSettingChanged("drawingGuides");
    };

    QCheckBox* snap = new QCheckBox("Snap to drawing guides", this);
    snap->setToolTip("Freehand strokes keep to the guide they begin near, or run the way the guides lead");
    connect(snap, &QCheckBox::toggled, this, [changeGuides](bool on) {
        changeGuides([on](aids::DrawingGuides& guides) { guides.snap = on; });
    });
    addOption(QString(), snap, guidesTool);

    HotText* magnetism = makeHotText(0, 100, 0, " %");
    magnetism->setToolTip("How firmly strokes keep to a guide: 100 is exactly on it, less lets the hand wander");
    connect(magnetism, &HotText::valueChanged, this, [changeGuides](double value, bool) {
        changeGuides([value](aids::DrawingGuides& guides) { guides.magnetism = std::clamp(static_cast<int>(value), 0, 100); });
    });
    addOption("Magnetism", magnetism, guidesTool);

    QCheckBox* grid = new QCheckBox("Perspective grid", this);
    grid->setToolTip("Show lines fanning out from the vanishing points");
    connect(grid, &QCheckBox::toggled, this, [changeGuides](bool on) {
        changeGuides([on](aids::DrawingGuides& guides) {
            for (aids::DrawingGuide& guide : guides.guides)
                guide.grid = on;
        });
    });
    addOption(QString(), grid, guidesTool);

    QCheckBox* hide = new QCheckBox("Hide drawing guides", this);
    hide->setToolTip("Hide the guides; strokes do not snap to them while they are hidden");
    connect(hide, &QCheckBox::toggled, this, [changeGuides](bool on) {
        changeGuides([on](aids::DrawingGuides& guides) { guides.hidden = on; });
    });
    addOption(QString(), hide, guidesTool);

    QPushButton* removeAll = new QPushButton("Remove All Guides", this);
    removeAll->setToolTip("Take every drawing guide off the stage");
    connect(removeAll, &QPushButton::clicked, this, [this, changeGuides]() {
        changeGuides([](aids::DrawingGuides& guides) { guides.guides.clear(); });
        if (_links.view)
            drawaids::setSelectedGuide(*_links.view, -1);
    });
    addOption(QString(), removeAll, guidesTool);

    _toolRefreshers.push_back([this, symmetryMode, copies, mirrorCopies, guideKind, snap, magnetism, grid, hide]() {
        if (!_links.view)
            return;
        const aids::Symmetry symmetry = drawaids::symmetry(*_links.view);
        const aids::DrawingGuides guides = drawaids::guides(*_links.view);
        const QSignalBlocker b1(symmetryMode);
        const QSignalBlocker b2(copies);
        const QSignalBlocker b3(mirrorCopies);
        const QSignalBlocker b4(guideKind);
        const QSignalBlocker b5(snap);
        const QSignalBlocker b6(magnetism);
        const QSignalBlocker b7(grid);
        const QSignalBlocker b8(hide);
        symmetryMode->setCurrentIndex(symmetry.on ? static_cast<int>(symmetry.mode) + 1 : 0);
        copies->setValue(symmetry.copies);
        mirrorCopies->setChecked(symmetry.mirror);
        guideKind->setCurrentIndex(std::clamp(_links.style->drawingGuideKind, 0, guideKind->count() - 1));
        snap->setChecked(guides.snap);
        magnetism->setValue(guides.magnetism);
        bool anyGrid = guides.guides.empty();
        for (const aids::DrawingGuide& guide : guides.guides)
            anyGrid = anyGrid || guide.grid;
        grid->setChecked(anyGrid);
        hide->setChecked(guides.hidden);
    });
}
