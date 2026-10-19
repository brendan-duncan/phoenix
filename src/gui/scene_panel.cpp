#include "scene_panel.h"
#include "modal.h"

#include "../data/document.h"
#include "../data/timeline.h"
#include "../edit/command_stack.h"
#include "../edit/property_command.h"
#include "../edit/scene_commands.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

/// The list, telling the panel when a drag has put its rows in a new order.
class SceneList : public QListWidget
{
public:
    using QListWidget::QListWidget;
    std::function<void()> moved;

protected:
    void dropEvent(QDropEvent* event) override
    {
        QListWidget::dropEvent(event);
        if (moved)
            moved();
    }
};

} // namespace

ScenePanel::ScenePanel(SceneLinks links, QWidget* parent)
    : QWidget(parent)
    , _links(std::move(links))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    SceneList* list = new SceneList(this);
    _list = list;
    _list->setDragDropMode(QAbstractItemView::InternalMove);
    _list->setDefaultDropAction(Qt::MoveAction);
    _list->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    list->moved = [this]() { reordered(); };
    layout->addWidget(_list, 1);

    QHBoxLayout* buttons = new QHBoxLayout();
    const auto button = [this, buttons](const QString& text, const QString& tip, std::function<void()> act) {
        QToolButton* b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, std::move(act));
        buttons->addWidget(b);
    };
    button("Add", "Add a new scene after this one", [this]() { addScene(false); });
    button("Duplicate", "Copy this scene, with its layers and frames", [this]() { addScene(true); });
    buttons->addStretch();
    button("Delete", "Delete this scene", [this]() { deleteScene(); });
    layout->addLayout(buttons);

    connect(_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (_links.showScene)
            _links.showScene(_list->row(item));
    });
    connect(_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (!_refreshing)
            renamed(item);
    });
    refresh();
}

void ScenePanel::refresh()
{
    _refreshing = true;
    _list->clear();
    const fla::Document* document = _links.document ? _links.document() : nullptr;
    if (document)
    {
        for (const fla::Timeline* scene : document->timelines)
        {
            QListWidgetItem* item = new QListWidgetItem(QString::fromStdString(scene->name), _list);
            item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
            // The pointer, to find each row's scene again after a drag.
            item->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<quintptr>(scene)));
        }
        _list->setCurrentRow(document->currentSceneIndex());
    }
    _refreshing = false;
}

void ScenePanel::push(std::vector<fla::Timeline*> scenes, int current, const char* name)
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
    if (!document || !commands)
        return;
    commands->push(fla::CommandPtr(new fla::SceneListCommand(document, std::move(scenes), current, name)));
    commands->breakMergeChain();
    if (_links.showScene)
        _links.showScene(-1);
    refresh();
}

void ScenePanel::addScene(bool duplicate)
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    if (!document)
        return;
    const int current = document->currentSceneIndex();
    fla::Timeline* added = nullptr;
    if (duplicate && current >= 0)
    {
        const fla::Timeline& original = *document->timelines[static_cast<size_t>(current)];
        added = fla::scenes::duplicate(original, *document, fla::scenes::freeName(*document, original.name + " copy"));
    }
    else
        added = fla::scenes::newScene(*document, fla::scenes::freeName(*document));
    std::vector<fla::Timeline*> scenes = document->timelines;
    scenes.insert(scenes.begin() + (current + 1), added);
    push(std::move(scenes), current + 1, duplicate ? "Duplicate Scene" : "Add Scene");
}

void ScenePanel::deleteScene()
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    if (!document)
        return;
    if (document->timelines.size() <= 1)
    {
        modal::information(this, "Scene", "A document keeps at least one scene.");
        return;
    }
    const int current = document->currentSceneIndex();
    const QString name = QString::fromStdString(document->timelines[static_cast<size_t>(current)]->name);
    if (QMessageBox::question(this, "Delete Scene", QString("Delete %1?").arg(name)) != QMessageBox::Yes)
        return;
    std::vector<fla::Timeline*> scenes = document->timelines;
    scenes.erase(scenes.begin() + current);
    push(std::move(scenes), std::max(0, current - 1), "Delete Scene");
}

void ScenePanel::renamed(QListWidgetItem* item)
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
    const int row = _list->row(item);
    if (!document || !commands || row < 0 || row >= static_cast<int>(document->timelines.size()))
        return;
    fla::Timeline* scene = document->timelines[static_cast<size_t>(row)];
    const std::string name = item->text().trimmed().toStdString();
    // Scene names are unique; an empty or taken one is refused.
    const bool taken = std::any_of(document->timelines.begin(), document->timelines.end(),
        [&](const fla::Timeline* other) { return other != scene && other->name == name; });
    if (name.empty() || taken || name == scene->name)
    {
        refresh();
        return;
    }
    commands->push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(scene->name, scene->name, name, "Rename Scene")));
    commands->breakMergeChain();
    if (_links.showScene)
        _links.showScene(-1);
}

void ScenePanel::reordered()
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    if (!document)
        return;
    std::vector<fla::Timeline*> scenes;
    for (int row = 0; row < _list->count(); ++row)
        scenes.push_back(reinterpret_cast<fla::Timeline*>(_list->item(row)->data(Qt::UserRole).value<quintptr>()));
    if (scenes.size() != document->timelines.size() || scenes == document->timelines)
    {
        refresh();
        return;
    }
    // The scene being edited stays the one being edited.
    const fla::Timeline* current = document->currentScene();
    const int at = static_cast<int>(std::find(scenes.begin(), scenes.end(), current) - scenes.begin());
    push(std::move(scenes), at, "Reorder Scenes");
}
