#include "notes_panel.h"

#include "text_layout.h"

#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/static_text.h"
#include "../data/timeline.h"
#include "../edit/command_stack.h"
#include "../edit/element_commands.h"
#include "../edit/layer_commands.h"
#include "../edit/property_command.h"
#include "../edit/review_notes.h"
#include "../edit/timeline_edit.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int NoteRole = Qt::UserRole;

QString defaultAuthor()
{
    const QString saved = QSettings().value("review/author").toString();
    if (!saved.isEmpty())
        return saved;
    const QString user = qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER"));
    return user;
}

} // namespace

NotesPanel::NotesPanel(NotesLinks links, QWidget* parent)
    : QWidget(parent)
    , _links(std::move(links))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    _list = new QTreeWidget(this);
    _list->setHeaderLabels({"Frame", "By", "Note"});
    _list->setRootIsDecorated(false);
    _list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _list->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    _list->setWordWrap(true);
    layout->addWidget(_list, 1);

    QHBoxLayout* buttons = new QHBoxLayout();
    QPushButton* add = new QPushButton("Add Note...", this);
    add->setToolTip("Write a note on the frame shown");
    QPushButton* draw = new QPushButton("Draw", this);
    draw->setToolTip("Draw on the notes layer: marks over the art that never export");
    _done = new QPushButton("Done", this);
    _done->setCheckable(true);
    _done->setToolTip("Tick the note off, or bring it back");
    _remove = new QPushButton("Delete", this);
    buttons->addWidget(add);
    buttons->addWidget(draw);
    buttons->addStretch(1);
    buttons->addWidget(_done);
    buttons->addWidget(_remove);
    layout->addLayout(buttons);

    connect(add, &QPushButton::clicked, this, &NotesPanel::addNote);
    connect(draw, &QPushButton::clicked, this, &NotesPanel::drawOnNotes);
    connect(_done, &QPushButton::clicked, this, [this](bool on) { setDone(on); });
    connect(_remove, &QPushButton::clicked, this, &NotesPanel::removeNote);
    connect(_list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        _done->setEnabled(item);
        _remove->setEnabled(item);
        if (!item)
            return;
        auto* text = static_cast<fla::StaticText*>(item->data(0, NoteRole).value<void*>());
        _done->setChecked(text && fla::review::isDone(*text));
        if (_links.setCurrentFrame)
            _links.setCurrentFrame(item->text(0).toInt() - 1);
    });
    refresh();
}

void NotesPanel::refresh()
{
    fla::Timeline* timeline = _links.timeline ? _links.timeline() : nullptr;
    const auto* current = _list->currentItem();
    const void* keep = current ? current->data(0, NoteRole).value<void*>() : nullptr;
    const QSignalBlocker blocker(_list);
    _list->clear();
    if (timeline)
    {
        for (const fla::review::Note& note : fla::review::notes(*timeline))
        {
            QTreeWidgetItem* item = new QTreeWidgetItem(_list);
            item->setText(0, QString::number(note.index + 1));
            item->setText(1, QString::fromStdString(note.author));
            QString body = QString::fromStdString(note.body);
            const QString prefix = QString::fromStdString(note.author) + ": ";
            if (!note.author.empty() && body.startsWith(prefix))
                body = body.mid(prefix.size());
            item->setText(2, body);
            item->setToolTip(2, body);
            item->setData(0, NoteRole, QVariant::fromValue(static_cast<void*>(note.text)));
            if (note.done)
            {
                QFont font = item->font(2);
                font.setStrikeOut(true);
                for (int column = 0; column < 3; ++column)
                {
                    item->setFont(column, font);
                    item->setForeground(column, palette().color(QPalette::Disabled, QPalette::Text));
                }
            }
            if (note.text == keep)
                _list->setCurrentItem(item);
        }
    }
    _done->setEnabled(_list->currentItem());
    _remove->setEnabled(_list->currentItem());
}

fla::Layer* NotesPanel::ensureLayer(fla::Timeline& timeline)
{
    if (fla::Layer* layer = fla::review::notesLayer(timeline))
        return layer;
    fla::Layer* layer = fla::review::newNotesLayer(timeline);
    _links.commands()->push(fla::CommandPtr(new fla::AddLayerCommand(&timeline, 0, layer, "Add Note")));
    return layer;
}

void NotesPanel::addNote()
{
    fla::Timeline* timeline = _links.timeline ? _links.timeline() : nullptr;
    fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
    if (!timeline || !commands)
        return;
    bool ok = false;
    const QString author = defaultAuthor();
    const int frameIndex = _links.currentFrame ? _links.currentFrame() : 0;
    const QString body = QInputDialog::getMultiLineText(this, "Add Note",
        QString("Note on frame %1%2:").arg(frameIndex + 1).arg(author.isEmpty() ? QString() : " by " + author), QString(), &ok)
                             .trimmed();
    if (!ok || body.isEmpty())
        return;

    commands->beginMacro("Add Note");
    fla::Layer* layer = ensureLayer(*timeline);
    // A keyframe of its own on the frame shown, so the note shows from
    // there to the next note.
    bool keyed = false;
    for (const fla::Frame* frame : layer->frames)
        keyed = keyed || (frame && frame->index == frameIndex);
    if (!keyed)
        commands->push(fla::CommandPtr(new fla::EditFramesCommand({layer}, "Add Note",
            [layer, frameIndex](std::vector<fla::Frame*>&) { fla::timeline::insertKeyframe(*layer, frameIndex, true); })));
    fla::Frame* target = nullptr;
    for (fla::Frame* frame : layer->frames)
        if (frame && frame->index == frameIndex)
            target = frame;
    if (target)
    {
        // Notes on the same frame stack down the stage's corner.
        const QPointF corner = _links.corner ? _links.corner() : QPointF(20.0, 20.0);
        const double y = corner.y() + 70.0 * static_cast<double>(target->elements.size());
        fla::StaticText* note = fla::review::makeNote(author.toStdString(), body.toStdString(), corner.x(), y);
        // Measured as the Text tool measures what is typed, so the stage
        // knows where it is.
        const QRectF bounds = TextLayout::localBounds(*note);
        note->height = bounds.height();
        note->localBounds = fla::Rect(fla::Point(bounds.left(), bounds.top()), fla::Point(bounds.right(), bounds.bottom()));
        note->bounds = note->localBounds;
        commands->push(fla::CommandPtr(new fla::AddElementCommand(target, note, "Add Note",
            _links.selection ? _links.selection() : nullptr)));
    }
    commands->endMacro();
    if (_links.changed)
        _links.changed();
    refresh();
}

void NotesPanel::setDone(bool done)
{
    QTreeWidgetItem* item = _list->currentItem();
    fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
    auto* text = item ? static_cast<fla::StaticText*>(item->data(0, NoteRole).value<void*>()) : nullptr;
    if (!text || !commands || fla::review::isDone(*text) == done)
        return;
    commands->push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
        text->extraAttributes, text->extraAttributes, fla::review::withDone(*text, done), done ? "Note Done" : "Note Not Done")));
    refresh();
}

void NotesPanel::removeNote()
{
    QTreeWidgetItem* item = _list->currentItem();
    fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
    fla::Timeline* timeline = _links.timeline ? _links.timeline() : nullptr;
    auto* text = item ? static_cast<fla::StaticText*>(item->data(0, NoteRole).value<void*>()) : nullptr;
    if (!text || !commands || !timeline)
        return;
    for (const fla::review::Note& note : fla::review::notes(*timeline))
    {
        if (note.text != text)
            continue;
        commands->push(fla::CommandPtr(new fla::RemoveElementCommand(note.frame, text, "Delete Note",
            _links.selection ? _links.selection() : nullptr)));
        break;
    }
    if (_links.changed)
        _links.changed();
    refresh();
}

void NotesPanel::drawOnNotes()
{
    fla::Timeline* timeline = _links.timeline ? _links.timeline() : nullptr;
    fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
    if (!timeline || !commands)
        return;
    fla::Layer* layer = fla::review::notesLayer(*timeline);
    if (!layer)
    {
        layer = ensureLayer(*timeline);
        if (_links.changed)
            _links.changed();
    }
    if (_links.setCurrentLayer)
        _links.setCurrentLayer(layer);
}
