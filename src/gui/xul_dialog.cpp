#include "xul_dialog.h"

#include "file_pick.h"
#include "modal.h"
#include "test_movie_window.h"

#include "../data/fla_document.h"
#include "../jsfl/flfile.h"
#include "../jsfl/xul.h"
#include "../parser/fla_parser.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include "hot_text.h"
#include <QVBoxLayout>

#include <memory>

using jsfl::xul::Control;
using jsfl::xul::Kind;
using jsfl::xul::Node;

namespace {

QString qtext(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

std::string text(const QString& value)
{
    return value.toUtf8().toStdString();
}

int number(const Node& node, const char* name, int fallback)
{
    const std::string* value = node.find(name);
    if (!value)
        return fallback;
    bool ok = false;
    const int parsed = qtext(*value).trimmed().remove("px").toInt(&ok);
    return ok ? parsed : fallback;
}

bool horizontal(const Node& node)
{
    if (node.is("hbox") || node.is("row"))
        return true;
    return node.attr("orient") == "horizontal";
}

QIcon swatch(const std::string& color)
{
    QPixmap pixmap(28, 14);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(QColor(128, 128, 128));
    QColor fill(qtext(color.substr(0, 7)));
    painter.setBrush(fill.isValid() ? fill : QColor(Qt::black));
    painter.drawRect(0, 0, pixmap.width() - 1, pixmap.height() - 1);
    return QIcon(pixmap);
}

/// The width a control asks for, as width (pixels) or size (characters).
void applyWidth(QWidget* widget, const Node& node, bool fixed)
{
    int width = number(node, "width", 0);
    if (width <= 0)
    {
        const int size = number(node, "size", 0);
        if (size > 0)
            width = QFontMetrics(widget->font()).averageCharWidth() * size + 12;
    }
    if (width > 0)
    {
        if (fixed)
            widget->setFixedWidth(width);
        else
            widget->setMinimumWidth(width);
    }
    const int height = number(node, "height", 0);
    if (height > 0)
        widget->setMinimumHeight(height);
}

} // namespace

XulDialog::XulDialog(jsfl::xul::Dialog& dialog, QWidget* parent)
    : QDialog(parent)
    , _dialog(dialog)
{
    setWindowTitle(qtext(dialog.title));
    auto* layout = new QVBoxLayout(this);
    fill(dialog.root(), layout);

    // Labels point at the controls they name, so their access keys work.
    for (const auto& entry : _buddies)
    {
        if (Control* control = _dialog.find(entry.second))
        {
            const auto found = _widgets.find(control);
            if (found != _widgets.end())
                static_cast<QLabel*>(entry.first)->setBuddy(found->second);
        }
    }

    if (!dialog.buttons.empty())
    {
        auto* buttons = new QDialogButtonBox(this);
        for (const std::string& name : dialog.buttons)
        {
            const Node& root = dialog.root();
            if (name == "accept")
            {
                QPushButton* ok = buttons->addButton(QDialogButtonBox::Ok);
                if (const std::string* label = root.find("buttonlabelaccept"))
                    ok->setText(qtext(*label));
            }
            else if (name == "cancel")
            {
                QPushButton* cancel = buttons->addButton(QDialogButtonBox::Cancel);
                if (const std::string* label = root.find("buttonlabelcancel"))
                    cancel->setText(qtext(*label));
            }
        }
        connect(buttons, &QDialogButtonBox::accepted, this, &XulDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &XulDialog::reject);
        layout->addWidget(buttons);
    }

    for (const auto& control : _dialog.controls())
        refresh(*control);
    _dialog.changed = [this](Control& control) { refresh(control); };
    _dialog.closedByScript = [this]() {
        if (isVisible())
            QDialog::done(_dialog.dismiss() == "accept" ? QDialog::Accepted : QDialog::Rejected);
    };
}

XulDialog::~XulDialog()
{
    _dialog.changed = nullptr;
    _dialog.closedByScript = nullptr;
    // The SWF panels' players go before the movies they play.
    qDeleteAll(findChildren<TestMovieWindow*>());
}

void XulDialog::accept()
{
    _dialog.close("accept");
    QDialog::accept();
}

void XulDialog::reject()
{
    _dialog.close("cancel");
    QDialog::reject();
}

void XulDialog::fill(const Node& node, QBoxLayout* box)
{
    const bool across = box->direction() == QBoxLayout::LeftToRight;
    for (const Node& child : node.children)
    {
        if (child.is("spacer"))
        {
            const int flex = number(child, "flex", 0);
            if (flex > 0)
                box->addStretch(flex);
            else
                box->addSpacing(6);
            continue;
        }
        if (child.is("separator"))
        {
            auto* line = new QFrame(this);
            line->setFrameShape(across ? QFrame::VLine : QFrame::HLine);
            line->setFrameShadow(QFrame::Sunken);
            box->addWidget(line);
            continue;
        }
        QWidget* widget = make(child);
        if (!widget)
            continue;
        const int flex = number(child, "flex", 0);
        Qt::Alignment align;
        const std::string pack = child.attr("align");
        if (!across && (child.is("button") || child.is("checkbox")) && pack != "stretch")
            align = Qt::AlignLeft;
        if (across)
            align = Qt::AlignVCenter;
        box->addWidget(widget, flex, align);
    }
    // A row packs its controls to the left unless something in it flexes.
    if (across)
    {
        bool flexes = false;
        for (const Node& child : node.children)
            flexes = flexes || number(child, "flex", 0) > 0;
        if (!flexes)
            box->addStretch(1);
    }
}

QWidget* XulDialog::make(const Node& node)
{
    if (node.is("script") || node.is("property") || node.is("columns") || node.is("column") || node.is("caption"))
        return nullptr;
    if (Control* control = _dialog.control(&node))
        return makeControl(*control);
    if (node.is("grid"))
        return makeGrid(node);
    if (node.is("label"))
    {
        auto* label = new QLabel(qtext(node.attr("value", node.text)), this);
        applyWidth(label, node, false);
        if (const std::string* target = node.find("control"))
            _buddies[label] = *target;
        return label;
    }
    if (node.is("description"))
    {
        auto* label = new QLabel(qtext(node.text).trimmed(), this);
        label->setWordWrap(true);
        applyWidth(label, node, false);
        return label;
    }
    if (node.is("flash"))
        return makeMovie(node);
    if (node.is("image") || node.is("iframe") || node.is("html"))
    {
        auto* label = new QLabel("(Embedded content is not shown.)", this);
        label->setEnabled(false);
        return label;
    }
    if (node.children.empty())
        return nullptr;
    // A box: vbox, hbox, groupbox, or anything else that holds elements.
    QWidget* holder = nullptr;
    QBoxLayout* box = new QBoxLayout(horizontal(node) ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    if (node.is("groupbox"))
    {
        auto* group = new QGroupBox(this);
        for (const Node& child : node.children)
        {
            if (child.is("caption"))
                group->setTitle(qtext(child.attr("label", child.text)).trimmed());
        }
        holder = group;
    }
    else
    {
        holder = new QWidget(this);
        box->setContentsMargins(0, 0, 0, 0);
    }
    holder->setLayout(box);
    fill(node, box);
    applyWidth(holder, node, false);
    return holder;
}

QWidget* XulDialog::makeMovie(const jsfl::xul::Node& node)
{
    // An SWF panel: played as Test Movie plays a movie, at the size the
    // XML gives. Its ActionScript runs, but cannot call JSFL (MMExecute).
    const QString source = qtext(node.attr("src"));
    QString path = source;
    if (path.startsWith("file:", Qt::CaseInsensitive))
        path = qtext(jsfl::uri::toPath(text(path)));
    else if (QFileInfo(path).isRelative() && !_dialog.folder.empty())
        path = QDir(qtext(_dialog.folder)).filePath(path);
    std::unique_ptr<fla::FLADocument> movie;
    if (QFileInfo(path).isFile())
    {
        FLAParser parser;
        movie.reset(parser.parse(path.toStdString()));
    }
    if (!movie || !movie->document)
    {
        auto* label = new QLabel(QString("(The SWF panel %1 could not be read.)").arg(QFileInfo(source).fileName()), this);
        label->setEnabled(false);
        return label;
    }
    auto* holder = new QWidget(this);
    auto* layout = new QVBoxLayout(holder);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* player = new TestMovieWindow(*movie, QFileInfo(path).fileName(), [](const QString&) {},
        [](const QString&, const fla::DOMElement*, int) {}, holder, path);
    player->setWindowFlags(Qt::Widget);
    const int width = number(node, "width", 0), height = number(node, "height", 0);
    if (width > 0 && height > 0)
        player->setMinimumSize(width, height);
    layout->addWidget(player, 1);
    auto* note = new QLabel("This panel's scripts cannot call Phoenix's JSFL.", holder);
    note->setEnabled(false);
    layout->addWidget(note);
    _movies.push_back(std::move(movie));
    return holder;
}

QWidget* XulDialog::makeGrid(const Node& node)
{
    auto* holder = new QWidget(this);
    auto* grid = new QGridLayout(holder);
    grid->setContentsMargins(0, 0, 0, 0);
    std::vector<const Node*> rows;
    for (const Node& child : node.children)
    {
        if (child.is("columns"))
        {
            int column = 0;
            for (const Node& each : child.children)
            {
                if (each.is("column"))
                    grid->setColumnStretch(column++, number(each, "flex", 0));
            }
        }
        else if (child.is("rows"))
        {
            for (const Node& row : child.children)
                rows.push_back(&row);
        }
        else if (child.is("row"))
            rows.push_back(&child);
    }
    int at = 0;
    for (const Node* row : rows)
    {
        if (!row->is("row"))
        {
            // Something else between rows takes a row of its own.
            if (QWidget* widget = make(*row))
                grid->addWidget(widget, at++, 0, 1, -1);
            continue;
        }
        int column = 0;
        for (const Node& cell : row->children)
        {
            if (cell.is("spacer") || cell.is("separator"))
            {
                ++column;
                continue;
            }
            if (QWidget* widget = make(cell))
                grid->addWidget(widget, at, column, Qt::AlignVCenter);
            ++column;
        }
        ++at;
    }
    return holder;
}

QWidget* XulDialog::makeControl(Control& control)
{
    jsfl::xul::Dialog& dialog = _dialog;
    const Node& node = *control.node;
    Control* c = &control;
    QWidget* widget = nullptr;
    switch (control.kind)
    {
    case Kind::TextBox:
    {
        // onchange comes once an edit is done, not at each key.
        auto dirty = std::make_shared<bool>(false);
        if (node.flag("multiline"))
        {
            auto* edit = new QPlainTextEdit(this);
            const int rows = number(node, "rows", 4);
            edit->setFixedHeight(QFontMetrics(edit->font()).lineSpacing() * rows + 12);
            connect(edit, &QPlainTextEdit::textChanged, this, [this, c, edit, dirty]() {
                if (_refreshing)
                    return;
                c->text = text(edit->toPlainText());
                *dirty = true;
            });
            widget = edit;
        }
        else
        {
            auto* edit = new QLineEdit(this);
            const int length = number(node, "maxlength", 0);
            if (length > 0)
                edit->setMaxLength(length);
            if (node.attr("type") == "password")
                edit->setEchoMode(QLineEdit::Password);
            connect(edit, &QLineEdit::textEdited, this, [this, c, dirty](const QString& typed) {
                if (_refreshing)
                    return;
                c->text = text(typed);
                *dirty = true;
            });
            connect(edit, &QLineEdit::editingFinished, this, [&dialog, c, dirty]() {
                if (!*dirty)
                    return;
                *dirty = false;
                dialog.handle(*c->node, "onchange");
            });
            widget = edit;
        }
        applyWidth(widget, node, true);
        break;
    }
    case Kind::CheckBox:
    {
        auto* box = new QCheckBox(qtext(node.attr("label")), this);
        connect(box, &QCheckBox::toggled, this, [this, &dialog, c](bool on) {
            if (!_refreshing)
                dialog.toggle(*c, on);
        });
        widget = box;
        break;
    }
    case Kind::RadioGroup:
    {
        auto* holder = new QWidget(this);
        auto* box = new QBoxLayout(horizontal(node) ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom, holder);
        box->setContentsMargins(0, 0, 0, 0);
        auto* group = new QButtonGroup(holder);
        for (size_t i = 0; i < control.items.size(); ++i)
        {
            auto* radio = new QRadioButton(qtext(control.items[i].label), holder);
            group->addButton(radio, static_cast<int>(i));
            box->addWidget(radio);
        }
        connect(group, &QButtonGroup::idClicked, this, [this, &dialog, c](int index) {
            if (!_refreshing)
                dialog.choose(*c, index);
        });
        widget = holder;
        break;
    }
    case Kind::MenuList:
    {
        auto* combo = new QComboBox(this);
        combo->setEditable(node.flag("editable"));
        connect(combo, &QComboBox::currentIndexChanged, this, [this, &dialog, c](int index) {
            if (!_refreshing && index >= 0)
                dialog.choose(*c, index);
        });
        if (combo->isEditable())
        {
            connect(combo, &QComboBox::editTextChanged, this, [this, &dialog, c](const QString& typed) {
                if (!_refreshing)
                    dialog.edit(*c, text(typed));
            });
        }
        applyWidth(combo, node, true);
        widget = combo;
        break;
    }
    case Kind::ListBox:
    {
        auto* list = new QListWidget(this);
        const int rows = number(node, "rows", 0);
        if (rows > 0)
            list->setFixedHeight(QFontMetrics(list->font()).lineSpacing() * rows + 8);
        connect(list, &QListWidget::currentRowChanged, this, [this, &dialog, c](int row) {
            if (!_refreshing)
                dialog.choose(*c, row);
        });
        applyWidth(list, node, false);
        widget = list;
        break;
    }
    case Kind::ColorChip:
    {
        auto* button = new QPushButton(this);
        button->setToolTip("Choose a color");
        connect(button, &QPushButton::clicked, this, [this, &dialog, c]() {
            modal::getColor(QColor(qtext(c->text.substr(0, 7))), this, "Color",
                [this, &dialog, c](const QColor& picked) {
                    dialog.edit(*c, text(picked.name().toUpper()));
                    refresh(*c);
                });
        });
        widget = button;
        break;
    }
    case Kind::Slider:
    {
        auto* spin = new HotSpinBox(this);
        spin->setRange(number(node, "minvalue", 0), number(node, "maxvalue", 100));
        connect(spin, &HotSpinBox::valueChanged, this, [this, &dialog, c](int value) {
            if (!_refreshing)
                dialog.edit(*c, std::to_string(value));
        });
        applyWidth(spin, node, true);
        widget = spin;
        break;
    }
    case Kind::ChooseFile:
    {
        auto* holder = new QWidget(this);
        auto* box = new QHBoxLayout(holder);
        box->setContentsMargins(0, 0, 0, 0);
        auto* edit = new QLineEdit(holder);
        applyWidth(edit, node, false);
        auto* browse = new QPushButton("Browse...", holder);
        box->addWidget(edit, 1);
        box->addWidget(browse);
        connect(edit, &QLineEdit::textEdited, this, [&dialog, c](const QString& typed) { dialog.edit(*c, text(typed)); });
        connect(browse, &QPushButton::clicked, this, [this, &dialog, c, edit]() {
            const bool save = c->node->attr("type") == "save";
            const auto chosenPath = [&dialog, c, edit](const QString& path) {
                if (path.isEmpty())
                    return;
                const std::string chosen = c->node->attr("pathtype") == "absolute" || c->node->flag("literal")
                    ? text(path)
                    : jsfl::uri::fromPath(text(path));
                dialog.edit(*c, chosen);
                edit->setText(qtext(chosen));
            };
            if (save)
                file_pick::save(this, QString(), QString(), QString(), chosenPath);
            else
                file_pick::open(this, QString(), QString(), QString(), chosenPath);
        });
        widget = holder;
        break;
    }
    case Kind::TargetList:
    {
        // The named instances of the timeline being edited, by their
        // paths, to pick from or type over.
        auto* combo = new QComboBox(this);
        combo->setEditable(true);
        combo->lineEdit()->setPlaceholderText("Instance path");
        connect(combo, &QComboBox::editTextChanged, this, [this, &dialog, c](const QString& typed) {
            if (!_refreshing)
                dialog.edit(*c, text(typed));
        });
        applyWidth(combo, node, true);
        widget = combo;
        break;
    }
    case Kind::Button:
    {
        auto* button = new QPushButton(qtext(node.attr("label")), this);
        button->setAutoDefault(false);
        connect(button, &QPushButton::clicked, this, [&dialog, c]() { dialog.click(*c->node); });
        applyWidth(button, node, false);
        widget = button;
        break;
    }
    case Kind::Label:
    {
        auto* label = new QLabel(this);
        label->setWordWrap(node.is("description"));
        if (const std::string* target = node.find("control"))
            _buddies[label] = *target;
        applyWidth(label, node, false);
        widget = label;
        break;
    }
    case Kind::Property:
        return nullptr;
    }
    if (widget)
        _widgets[&control] = widget;
    return widget;
}

void XulDialog::refresh(Control& control)
{
    const auto found = _widgets.find(&control);
    if (found == _widgets.end())
        return;
    QWidget* widget = found->second;
    _refreshing = true;
    switch (control.kind)
    {
    case Kind::TextBox:
        if (auto* line = qobject_cast<QLineEdit*>(widget))
        {
            if (line->text() != qtext(control.text))
                line->setText(qtext(control.text));
        }
        else if (auto* edit = qobject_cast<QPlainTextEdit*>(widget))
        {
            if (edit->toPlainText() != qtext(control.text))
                edit->setPlainText(qtext(control.text));
        }
        break;
    case Kind::CheckBox:
        static_cast<QCheckBox*>(widget)->setChecked(control.checked);
        break;
    case Kind::RadioGroup:
    {
        const QList<QRadioButton*> radios = widget->findChildren<QRadioButton*>();
        // Items a script set replace the buttons.
        bool same = radios.size() == static_cast<int>(control.items.size());
        for (int i = 0; same && i < radios.size(); ++i)
            same = radios[i]->text() == qtext(control.items[static_cast<size_t>(i)].label);
        if (!same)
        {
            auto* group = widget->findChild<QButtonGroup*>();
            for (QRadioButton* radio : radios)
                delete radio;
            for (size_t i = 0; i < control.items.size(); ++i)
            {
                auto* radio = new QRadioButton(qtext(control.items[i].label), widget);
                group->addButton(radio, static_cast<int>(i));
                widget->layout()->addWidget(radio);
            }
        }
        const QList<QRadioButton*> now = widget->findChildren<QRadioButton*>();
        for (int i = 0; i < now.size(); ++i)
            now[i]->setChecked(i == control.selected);
        break;
    }
    case Kind::MenuList:
    {
        auto* combo = static_cast<QComboBox*>(widget);
        combo->clear();
        for (const jsfl::xul::Item& item : control.items)
            combo->addItem(qtext(item.label));
        combo->setCurrentIndex(control.selected);
        if (combo->isEditable())
            combo->setEditText(qtext(control.text));
        break;
    }
    case Kind::ListBox:
    {
        auto* list = static_cast<QListWidget*>(widget);
        list->clear();
        for (const jsfl::xul::Item& item : control.items)
            list->addItem(qtext(item.label));
        list->setCurrentRow(control.selected);
        break;
    }
    case Kind::ColorChip:
    {
        auto* button = static_cast<QPushButton*>(widget);
        button->setIcon(swatch(control.text));
        button->setText(qtext(control.text));
        break;
    }
    case Kind::Slider:
        static_cast<HotSpinBox*>(widget)->setValue(qtext(control.text).toInt());
        break;
    case Kind::ChooseFile:
        if (auto* edit = widget->findChild<QLineEdit*>())
            edit->setText(qtext(control.text));
        break;
    case Kind::TargetList:
    {
        auto* combo = static_cast<QComboBox*>(widget);
        combo->clear();
        for (const jsfl::xul::Item& item : control.items)
            combo->addItem(qtext(item.label));
        combo->setEditText(qtext(control.text));
        break;
    }
    case Kind::Label:
        static_cast<QLabel*>(widget)->setText(qtext(control.text));
        break;
    case Kind::Button:
        if (control.node->find("label"))
            static_cast<QPushButton*>(widget)->setText(qtext(control.node->attr("label")));
        break;
    case Kind::Property:
        break;
    }
    widget->setEnabled(control.enabled);
    widget->setVisible(control.visible);
    _refreshing = false;
}
