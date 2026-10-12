#pragma once

#include "../data/fla_document.h"
#include "../edit/layer_commands.h"

#include <QIcon>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QWidget>

#include <map>
#include <string>
#include <vector>

class QToolButton;

/// One row of the layers list as shown: which layer of which timeline.
/// The timeline grid draws exactly these rows, so a collapsed folder hides
/// its layers' lanes and every label lines up with its track.
struct LayerRow
{
    const fla::Timeline* timeline = nullptr;
    int layerIndex = -1;
    const fla::Layer* layer = nullptr;
};

/// The layer list beside the timeline, laid out as Animate's: a header
/// whose eye, padlock and outline square toggle every layer at once, a row
/// per layer with a pencil on the current one and the same three toggles,
/// folders that open and close, and under the list the New Layer, New
/// Folder and Delete buttons. Names are edited by double-click.
///
/// The panel changes a layer's visibility, lock, outline mode and current
/// flag itself, since those are view state rather than edits; everything
/// that belongs in the undo history (adding, deleting, renaming) is asked
/// for through a signal and carried out by the window.
class LayersPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LayersPanel(QWidget *parent = nullptr);

    void setDocument(const fla::FLADocument* document);

    /// Shows one timeline's layers only, as when a symbol is being edited;
    /// null shows the document's.
    void setTimeline(fla::Timeline* timeline);

    /// Rebuilds the list from the document, keeping which folders are open.
    void refresh();

    /// Rebuilds the list only if the layers it shows (which, in what order,
    /// with what names) no longer match the document, as after an undo.
    void refreshIfStale();

    /// Makes \a layerIndex of \a timeline the current layer, as a click on
    /// its name would, and says so.
    void setCurrentLayer(const fla::Timeline* timeline, int layerIndex);

    /// The current layer of the shown timeline. False when there is none.
    bool currentLayer(fla::Timeline*& timeline, int& layerIndex) const;

    /// The rows on show, top to bottom: the layers inside a collapsed
    /// folder are left out.
    std::vector<LayerRow> visibleRows() const;

    /// The list scrolls by the pixel and shows no scroll bar of its own: it
    /// follows the frame grid's, so the names stay level with their lanes.
    class QScrollBar* verticalScrollBar() const;

    /// The height the rows are shown in, with no bottom margin.
    int rowsViewportHeight() const;

    /// Empty space under the last row, so the list scrolls exactly as far as
    /// the frame grid beside it.
    void setBottomMargin(int margin);

signals:
    void layerVisibilityChanged(const QString& timelineName, int layerIndex, bool visible);

    /// Locking changes what can be picked and drawn on, so the stage hears
    /// about it the same way it hears about visibility.
    void layerLockChanged(const QString& timelineName, int layerIndex, bool locked);

    /// The current layer changed, so the stage draws into a different frame.
    void currentLayerChanged(const QString& timelineName, int layerIndex);

    /// A layer went into or out of outline view, or a header toggle changed
    /// every layer at once: the stage redraws.
    void layersChanged();

    /// The rows on show changed: a folder opened or closed, or the list was
    /// rebuilt.
    void rowsChanged();

    /// The New Layer or New Folder button.
    void addLayerRequested(bool folder);

    /// The Delete button, for the current layer.
    void deleteLayerRequested();

    /// A name was typed for a layer.
    void layerRenamed(const fla::Timeline* timeline, int layerIndex, const QString& name);

    /// The layers of \a timeline are to be put as \a after has them, as one
    /// undo step named \a name: a move, a change of kind, the Layer
    /// Properties dialog.
    void layerEditRequested(fla::Timeline* timeline, const fla::LayerSnapshot& after, const QString& name);
    /// Layer parenting: \a layer to follow \a parent (null for none).
    void parentLayerRequested(fla::Timeline* timeline, fla::Layer* layer, fla::Layer* parent);
    /// An effect layer's settings, to be edited.
    void effectSettingsRequested(fla::Layer* layer);

private:
    enum Column
    {
        NameColumn = 0,
        CurrentColumn,
        VisibleColumn,
        LockColumn,
        ColorColumn,
        ColumnCount
    };

    const fla::FLADocument* _flaDocument;
    fla::Timeline* _timeline = nullptr;
    QTreeWidget* _treeWidget;
    int _bottomMargin = 0;
    /// The header and glyphs in the current theme's colors.
    void applyTheme();
    QWidget* _header = nullptr;
    QWidget* _buttonRow = nullptr;
    QToolButton* _allVisible = nullptr;
    QToolButton* _allLocked = nullptr;
    QToolButton* _allOutline = nullptr;

    std::vector<fla::Timeline*> shownTimelines() const;
    QIcon _layerIcon;
    QIcon _folderIcon;
    QIcon _openFolderIcon;
    QIcon _visibleIcon;
    QIcon _hiddenIcon;
    QIcon _unlockedIcon;
    QIcon _lockedIcon;
    QIcon _pencilIcon;

    /// What the list was last built from, to tell when it is stale.
    struct Built
    {
        const fla::Layer* layer;
        std::string name;
        bool selected;
        const fla::Layer* parent;
        fla::Layer::Type type;
    };
    std::vector<Built> _built;
    std::vector<Built> snapshot() const;

    /// Which folders are open, by layer, so a rebuild keeps them so.
    std::map<const fla::Layer*, bool> _expanded;

    /// True while the list is being rebuilt, so item changes are not edits.
    bool _refreshing = false;

    /// The layer and timeline an item stands for, or null.
    fla::Layer* layerFor(const QTreeWidgetItem* item) const;
    fla::Timeline* timelineFor(const QTreeWidgetItem* item) const;

    /// Puts the pencil on the current layer's row and nowhere else.
    void markCurrent();

    /// Redraws one row's toggles from its layer.
    void refreshRow(QTreeWidgetItem* item);

    /// The right-click menu of a row.
    void showLayerMenu(QTreeWidgetItem* item, const QPoint& globalPos);

    /// The Layer Properties dialog for a row.
    void showLayerProperties(QTreeWidgetItem* item);

    /// A row dropped on, above or below another (or on empty space, with no
    /// target).
    void dropLayer(QTreeWidgetItem* dragged, QTreeWidgetItem* target, int position);

    /// The layer just below everything \a item holds, or null at the bottom.
    fla::Layer* layerAfterBlock(QTreeWidgetItem* item) const;

private slots:
    void onItemExpanded(QTreeWidgetItem* item);
    void onItemCollapsed(QTreeWidgetItem* item);
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onItemChanged(QTreeWidgetItem* item, int column);
};
