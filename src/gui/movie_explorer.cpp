#include "movie_explorer.h"

#include "../data/bitmap_instance.h"
#include "../data/document.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/static_text.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "../data/action_script.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int MatchRole = Qt::UserRole + 1;
constexpr int SymbolRole = Qt::UserRole + 2;

QString kindOf(const fla::SymbolInstance& instance)
{
    switch (instance.symbolType)
    {
    case fla::SymbolType::MovieClip: return "Movie Clip";
    case fla::SymbolType::Button: return "Button";
    case fla::SymbolType::Graphic: break;
    }
    return "Graphic";
}

QString textOf(const fla::StaticText& text)
{
    QString all;
    for (const fla::TextRun& run : text.runs)
        all += QString::fromStdString(run.text);
    return all.simplified();
}

} // namespace

MovieExplorer::MovieExplorer(std::function<fla::Document*()> document, QWidget* parent)
    : QWidget(parent)
    , _document(std::move(document))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    // Show: the kinds of entry, as toggles.
    QHBoxLayout* filters = new QHBoxLayout();
    filters->setContentsMargins(0, 0, 0, 0);
    const auto toggle = [&](const QString& text, const QString& tip, bool& flag) {
        QToolButton* button = new QToolButton(this);
        button->setText(text);
        button->setToolTip(tip);
        button->setCheckable(true);
        button->setChecked(flag);
        bool* target = &flag;
        connect(button, &QToolButton::toggled, this, [this, target](bool on) {
            *target = on;
            refresh();
        });
        filters->addWidget(button);
    };
    toggle("Text", "Show text fields", _shown.text);
    toggle("Symbols", "Show movie clips, buttons and graphics", _shown.symbols);
    toggle("Scripts", "Show frame scripts", _shown.scripts);
    toggle("Media", "Show bitmaps and sounds", _shown.media);
    toggle("Frames", "Show layers and frames even where nothing else is shown", _shown.framesAndLayers);
    filters->addStretch(1);
    QToolButton* menuButton = new QToolButton(this);
    menuButton->setText(QString(QChar(0x2630)));
    menuButton->setAutoRaise(true);
    filters->addWidget(menuButton);
    layout->addLayout(filters);

    _search = new QLineEdit(this);
    _search->setPlaceholderText("Find by name or text");
    _search->setClearButtonEnabled(true);
    connect(_search, &QLineEdit::textChanged, this, &MovieExplorer::applySearch);
    layout->addWidget(_search);

    _tree = new QTreeWidget(this);
    _tree->setColumnCount(2);
    _tree->setHeaderLabels({"Name", "Kind"});
    _tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(_tree, 1);
    connect(_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        const int index = item->data(0, MatchRole).toInt() - 1;
        if (index >= 0 && index < static_cast<int>(_matches.size()))
            emit goToRequested(_matches[static_cast<size_t>(index)]);
    });

    QMenu* menu = new QMenu(menuButton);
    QAction* library = menu->addAction("Show in Library");
    connect(library, &QAction::triggered, this, [this]() {
        QTreeWidgetItem* item = _tree->currentItem();
        const QString name = item ? item->data(0, SymbolRole).toString() : QString();
        if (!name.isEmpty())
            emit showInLibraryRequested(name);
    });
    QAction* expand = menu->addAction("Expand All");
    connect(expand, &QAction::triggered, _tree, &QTreeWidget::expandAll);
    QAction* collapse = menu->addAction("Collapse All");
    connect(collapse, &QAction::triggered, _tree, &QTreeWidget::collapseAll);
    QAction* copyText = menu->addAction("Copy All Text to Clipboard");
    connect(copyText, &QAction::triggered, this, [this]() { QApplication::clipboard()->setText(allText()); });
    QAction* reload = menu->addAction("Refresh");
    connect(reload, &QAction::triggered, this, &MovieExplorer::refresh);
    menuButton->setMenu(menu);
    menuButton->setPopupMode(QToolButton::InstantPopup);
}

void MovieExplorer::addElement(QTreeWidgetItem* parent, fla::Element* element, fla::Element* top,
    const fla::find::Match& where)
{
    if (!element)
        return;
    QString name;
    QString kind;
    QString symbol;
    bool shown = false;
    switch (element->elementType())
    {
    case fla::Element::Type::SymbolInstance:
    {
        auto* instance = static_cast<fla::SymbolInstance*>(element);
        symbol = QString::fromStdString(instance->libraryItemName);
        name = instance->name.empty() ? symbol : QString("%1 (%2)").arg(QString::fromStdString(instance->name), symbol);
        kind = kindOf(*instance);
        shown = _shown.symbols;
        break;
    }
    case fla::Element::Type::StaticText:
        name = textOf(*static_cast<fla::StaticText*>(element));
        kind = "Text";
        shown = _shown.text;
        break;
    case fla::Element::Type::BitmapInstance:
        name = QString::fromStdString(static_cast<fla::BitmapInstance*>(element)->libraryItemName);
        kind = "Bitmap";
        shown = _shown.media;
        break;
    case fla::Element::Type::Group:
        for (fla::Element* member : static_cast<fla::Group*>(element)->members)
            addElement(parent, member, top, where);
        return;
    default:
        return;
    }
    if (!shown)
        return;
    fla::find::Match match = where;
    match.topElement = top;
    match.element = element;
    _matches.push_back(match);
    QTreeWidgetItem* item = new QTreeWidgetItem(parent, {name, kind});
    item->setData(0, MatchRole, static_cast<int>(_matches.size()));
    item->setData(0, SymbolRole, symbol);
}

void MovieExplorer::addTimeline(QTreeWidgetItem* parent, fla::Timeline* timeline, fla::Symbol* symbol, int scene)
{
    if (!timeline)
        return;
    for (size_t l = 0; l < timeline->layers.size(); ++l)
    {
        fla::Layer* layer = timeline->layers[l];
        if (!layer)
            continue;
        QTreeWidgetItem* layerItem = new QTreeWidgetItem({QString::fromStdString(layer->name), "Layer"});
        for (fla::Frame* frame : layer->frames)
        {
            if (!frame)
                continue;
            fla::find::Match where;
            where.symbol = symbol;
            where.scene = scene;
            where.layer = static_cast<int>(l);
            where.frame = frame->index;
            QString title = QString("Frame %1").arg(frame->index + 1);
            if (!frame->name.empty())
                title += QString(" \"%1\"").arg(QString::fromStdString(frame->name));
            QTreeWidgetItem* frameItem = new QTreeWidgetItem({title, "Keyframe"});
            _matches.push_back(where);
            frameItem->setData(0, MatchRole, static_cast<int>(_matches.size()));
            if (_shown.scripts && frame->actionScript && !frame->actionScript->code.empty())
            {
                QTreeWidgetItem* script = new QTreeWidgetItem(frameItem,
                    {QString::fromStdString(frame->actionScript->code).simplified().left(80), "Script"});
                script->setData(0, MatchRole, static_cast<int>(_matches.size()));
            }
            if (_shown.media)
                for (const auto& attribute : frame->extraAttributes)
                    if (attribute.first == "soundName")
                    {
                        QTreeWidgetItem* sound = new QTreeWidgetItem(frameItem, {QString::fromStdString(attribute.second), "Sound"});
                        sound->setData(0, MatchRole, static_cast<int>(_matches.size()));
                    }
            for (fla::Element* element : frame->elements)
                addElement(frameItem, element, element, where);
            if (frameItem->childCount() > 0 || (_shown.framesAndLayers && !frame->name.empty()))
                layerItem->addChild(frameItem);
            else
                delete frameItem;
        }
        if (layerItem->childCount() > 0 || _shown.framesAndLayers)
            parent->addChild(layerItem);
        else
            delete layerItem;
    }
}

void MovieExplorer::refresh()
{
    _tree->clear();
    _matches.clear();
    fla::Document* document = _document ? _document() : nullptr;
    if (!document)
        return;
    for (size_t i = 0; i < document->timelines.size(); ++i)
    {
        fla::Timeline* scene = document->timelines[i];
        if (!scene)
            continue;
        QTreeWidgetItem* item = new QTreeWidgetItem(_tree, {QString::fromStdString(scene->name), "Scene"});
        addTimeline(item, scene, nullptr, static_cast<int>(i));
        item->setExpanded(true);
    }
    if (document->symbolList && !document->symbolList->symbols.empty())
    {
        QTreeWidgetItem* definitions = new QTreeWidgetItem(_tree, {"Symbol Definitions", QString()});
        for (fla::Symbol* symbol : document->symbolList->symbols)
        {
            if (!symbol || symbol->timelines.empty())
                continue;
            QTreeWidgetItem* item = new QTreeWidgetItem(definitions, {QString::fromStdString(symbol->name), "Symbol"});
            item->setData(0, SymbolRole, QString::fromStdString(symbol->name));
            addTimeline(item, symbol->timelines.front(), symbol, -1);
        }
    }
    applySearch();
}

void MovieExplorer::applySearch()
{
    const QString text = _search->text().trimmed();
    std::function<bool(QTreeWidgetItem*)> show = [&](QTreeWidgetItem* item) {
        bool any = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive);
        const bool own = any;
        for (int i = 0; i < item->childCount(); ++i)
            any = show(item->child(i)) || any;
        // An entry that matches shows all it holds.
        if (own && !text.isEmpty())
            for (int i = 0; i < item->childCount(); ++i)
                item->child(i)->setHidden(false);
        item->setHidden(!any);
        if (!text.isEmpty() && any)
            item->setExpanded(true);
        return any;
    };
    for (int i = 0; i < _tree->topLevelItemCount(); ++i)
        show(_tree->topLevelItem(i));
}

QString MovieExplorer::allText() const
{
    QStringList texts;
    fla::Document* document = _document ? _document() : nullptr;
    if (!document)
        return QString();
    std::vector<fla::Timeline*> timelines(document->timelines.begin(), document->timelines.end());
    if (document->symbolList)
        for (fla::Symbol* symbol : document->symbolList->symbols)
            if (symbol)
                timelines.insert(timelines.end(), symbol->timelines.begin(), symbol->timelines.end());
    std::function<void(fla::Element*)> visit = [&](fla::Element* element) {
        if (auto* text = dynamic_cast<fla::StaticText*>(element))
            texts << textOf(*text);
        if (auto* group = dynamic_cast<fla::Group*>(element))
            for (fla::Element* member : group->members)
                visit(member);
    };
    for (fla::Timeline* timeline : timelines)
        for (fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
            for (fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
                for (fla::Element* element : frame ? frame->elements : std::vector<fla::Element*>())
                    visit(element);
    texts.removeAll(QString());
    return texts.join("\n");
}
