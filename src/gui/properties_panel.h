#pragma once

#include "tool_icons.h"

#include <QMap>
#include <QWidget>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class HotText;
class PhoenixView;
class Player;
class Tool;
class QAction;
class QGridLayout;
class QLabel;
class QToolButton;
class QVBoxLayout;

namespace fla {
class Command;
class DrawingStyle;
class Element;
class EditContext;
class Frame;
class Layer;
class Selection;
class SymbolInstance;
class Shape;
class Document;
}

/// One collapsible group of the Properties panel: a bold title with a
/// disclosure arrow over a grid of labeled fields, as Animate lays out
/// "Position and Size" or "Color and Style".
class PropertySection : public QWidget
{
public:
    explicit PropertySection(const QString& title, QWidget* parent = nullptr);

    /// The grid the fields go in. Labels sit in even columns and fields in
    /// odd ones, so two label-and-field pairs share a row.
    QGridLayout* grid() const { return _grid; }

    /// Puts \a label and \a field at \a row, in the pair of columns \a slot
    /// (0 for the left pair, 1 for the right). A null label leaves the
    /// field to span its pair.
    void addField(int row, int slot, const QString& label, QWidget* field);

    /// Puts \a widget across the whole of \a row.
    void addWide(int row, QWidget* widget);

    /// Puts \a label in the first column and \a field across the rest.
    void addLong(int row, const QString& label, QWidget* field);

    void setExpanded(bool expanded);
    bool isExpanded() const { return _expanded; }

private:
    QToolButton* _header;
    QWidget* _body;
    QGridLayout* _grid;
    bool _expanded = true;
};

/// Animate's Properties inspector: tabs for the active tool, the selected
/// object, the current frame and the document, each a stack of collapsible
/// sections of hot-text fields.
///
/// The Tool tab writes straight into the drawing style and reports which
/// setting changed, so the window can carry out any side effect (the text
/// tool restyling its selection, the eraser changing its cursor). The other
/// tabs edit the document through undoable commands on the edit context's
/// stack, and report when something that is drawn changed.
class PropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    enum class Tab
    {
        Tool,
        Object,
        Frame,
        Doc
    };

    /// What the panel reads and edits. None of these are owned.
    struct Links
    {
        fla::DrawingStyle* style = nullptr;
        fla::Selection* selection = nullptr;
        fla::EditContext* editContext = nullptr;
        PhoenixView* view = nullptr;
        Player* player = nullptr;
        QAction* smoothAction = nullptr;
        QAction* straightenAction = nullptr;
    };

    explicit PropertiesPanel(const Links& links, QWidget* parent = nullptr);

    /// The tool that has the stage, and the icon it is shown with.
    void setActiveTool(Tool* tool, ToolIcons::Tool icon);

    void setTab(Tab tab);
    Tab tab() const { return _tab; }

    /// Re-reads the selection, the current frame and the document. Safe to
    /// call at any time; a refresh asked for in the middle of one of the
    /// panel's own edits waits until the edit is done.
    void refresh();

    /// Redraws the fill and stroke swatches after the style changed from
    /// outside the panel, such as from the eyedropper.
    void refreshStyleSwatches();

    /// The font face or size just chosen on the Tool tab, read on a
    /// toolSettingChanged of "textFace" or "textSize": they go to an open
    /// text box's selection if there is one, else into the style.
    const std::string& pendingTextFace() const { return _pendingTextFace; }
    double pendingTextSize() const { return _pendingTextSize; }

signals:
    /// A Tool tab setting was changed; \a key names it ("fillColor",
    /// "textFace", "textSize", "textType", "transformMode", "faucet").
    void toolSettingChanged(const QString& key);

    /// Something drawn was edited through the Object, Frame or Doc tab.
    void documentEdited();

private:
    struct ToolRow
    {
        QWidget* label;
        QWidget* field;
        std::vector<ToolIcons::Tool> tools;
    };

    void buildTabBar();
    void buildToolTab();
    void applyToolVisibility();
    void rebuildObjectTab();
    void rebuildFrameTab();
    void rebuildDocTab();

    /// A header line: an icon, a title and, if given, a field beside it.
    QWidget* makeHeader(const QPixmap& icon, const QString& title, QWidget* field = nullptr);
    PropertySection* addSection(QVBoxLayout* layout, const QString& title);
    HotText* makeHotText(double minimum, double maximum, int decimals, const QString& suffix = QString());
    QToolButton* makeSwatch(const uint8_t rgba[4], const QString& tip);

    /// Hooks a hot text up to a live edit: \a start snapshots what the edit
    /// changes, \a apply applies each value as it comes, and \a commit
    /// records the whole edit as one undo step once it is over.
    void bindLive(HotText* hot, std::function<void()> start, std::function<void(double)> apply,
        std::function<void()> commit);

    void addPositionAndSize(QVBoxLayout* layout, const std::vector<fla::Element*>& elements);
    void addShapeSections(QVBoxLayout* layout, fla::Shape* shape);
    void addInstanceSections(QVBoxLayout* layout, fla::SymbolInstance* instance);

    fla::Frame* currentFrame(fla::Layer** layerOut) const;
    fla::Document* document() const;

    /// Pushes \a command on the edit context's stack.
    void push(std::unique_ptr<fla::Command> command);

    /// A refresh that arrived during one of the panel's own edits, to run
    /// when the edit is done.
    void flushRefresh();

    Links _links;
    Tab _tab = Tab::Tool;
    QWidget* _tabBarHolder = nullptr;
    std::string _pendingTextFace;
    double _pendingTextSize = 12.0;
    QToolButton* _tabButtons[4] = {nullptr, nullptr, nullptr, nullptr};
    QWidget* _pages[4] = {nullptr, nullptr, nullptr, nullptr};
    QVBoxLayout* _pageLayouts[4] = {nullptr, nullptr, nullptr, nullptr};

    // Tool tab, built once.
    Tool* _tool = nullptr;
    ToolIcons::Tool _toolIcon = ToolIcons::Tool::Selection;
    QLabel* _toolIconLabel = nullptr;
    QLabel* _toolNameLabel = nullptr;
    PropertySection* _colorSection = nullptr;
    PropertySection* _optionsSection = nullptr;
    std::vector<ToolRow> _toolRows;
    QToolButton* _fillSwatch = nullptr;
    QToolButton* _strokeSwatch = nullptr;
    HotText* _cornerRadius = nullptr;

    bool _inEdit = false;
    bool _refreshPending = false;
    bool _aspectLocked = true;

    /// The color effect style chosen for an instance whose transform is
    /// still the identity (Alpha at 100%, say), so the choice shows its
    /// fields rather than falling back to None. Keys are compared, never
    /// followed.
    QMap<const fla::SymbolInstance*, int> _colorStyleChoice;
};
