#include "cloud_browser.h"

#include "cloud_client.h"
#include "cloud_documents.h"
#include "main_window.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {

const int kKindRole = Qt::UserRole + 1;
const int kIdRole = Qt::UserRole + 2;
const int kDocRole = Qt::UserRole + 3;
const int kPlaceView = Qt::UserRole + 1;
const int kPlaceGroup = Qt::UserRole + 2;
const QSize kCard(168, 105);

/// A card's picture: the document's thumbnail fitted on a dark tile, or a
/// plain tile until it comes.
QIcon cardIcon(const QImage& picture = QImage(), bool folder = false, QStyle* style = nullptr)
{
    QPixmap tile(kCard);
    tile.fill(QColor(32, 32, 36));
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!picture.isNull())
    {
        const QImage fitted = picture.scaled(kCard - QSize(4, 4), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawImage((kCard.width() - fitted.width()) / 2, (kCard.height() - fitted.height()) / 2, fitted);
    }
    else if (folder && style)
    {
        const QPixmap glyph = style->standardIcon(QStyle::SP_DirIcon).pixmap(48, 48);
        painter.drawPixmap((kCard.width() - glyph.width()) / 2, (kCard.height() - glyph.height()) / 2, glyph);
    }
    painter.setPen(QColor(60, 60, 66));
    painter.drawRect(tile.rect().adjusted(0, 0, -1, -1));
    return QIcon(tile);
}

QString encoded(const QString& value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

} // namespace

CloudBrowser::CloudBrowser(MainWindow* window, Mode mode,
    std::function<void(const QString&, const QString&, const QString&)> saveTo, const QString& name)
    : QDialog(window)
    , _window(window)
    , _mode(mode)
    , _saveTo(std::move(saveTo))
{
    setObjectName("cloudBrowser");
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(mode == Mode::Open ? "Open from Phoenix Cloud"
            : mode == Mode::Library  ? "Open a Library from Phoenix Cloud"
                                     : "Save a Copy to Phoenix Cloud");
    resize(900, 600);

    QVBoxLayout* layout = new QVBoxLayout(this);
    QHBoxLayout* top = new QHBoxLayout();
    QLabel* who = new QLabel(QString("Signed in as %1 at %2").arg(CloudClient::instance().userName(),
                                 QUrl(CloudClient::instance().server()).host()),
        this);
    who->setEnabled(false);
    _search = new QLineEdit(this);
    _search->setObjectName("cloudSearch");
    _search->setPlaceholderText("Find a document");
    _search->setClearButtonEnabled(true);
    _search->setMaximumWidth(320);
    top->addWidget(who, 1);
    top->addWidget(_search);
    layout->addLayout(top);

    QHBoxLayout* middle = new QHBoxLayout();
    _places = new QListWidget(this);
    _places->setObjectName("cloudPlaces");
    _places->setMaximumWidth(190);
    const auto addPlace = [this](const QString& label, const QString& view, const QString& group = QString()) {
        QListWidgetItem* item = new QListWidgetItem(label, _places);
        item->setData(kPlaceView, view);
        item->setData(kPlaceGroup, group);
        return item;
    };
    if (mode != Mode::SaveTo)
        addPlace("Recent", "recent");
    addPlace("My Files", "files");
    if (mode != Mode::SaveTo)
    {
        addPlace("Shared with Me", "shared");
        addPlace("Favorites", "favorites");
    }
    middle->addWidget(_places);

    QVBoxLayout* right = new QVBoxLayout();
    QHBoxLayout* path = new QHBoxLayout();
    _up = new QToolButton(this);
    _up->setIcon(style()->standardIcon(QStyle::SP_FileDialogToParent));
    _up->setToolTip("The folder above");
    _where = new QLabel(this);
    path->addWidget(_up);
    path->addWidget(_where, 1);
    right->addLayout(path);
    _items = new QListWidget(this);
    _items->setObjectName("cloudItems");
    _items->setViewMode(QListView::IconMode);
    _items->setIconSize(kCard);
    _items->setGridSize(kCard + QSize(24, 44));
    _items->setResizeMode(QListView::Adjust);
    _items->setMovement(QListView::Static);
    _items->setWordWrap(true);
    _items->setUniformItemSizes(true);
    _empty = new QLabel(this);
    _empty->setAlignment(Qt::AlignCenter);
    _empty->setEnabled(false);
    _empty->hide();
    right->addWidget(_items, 1);
    right->addWidget(_empty, 1);
    middle->addLayout(right, 1);
    layout->addLayout(middle, 1);

    QHBoxLayout* bottom = new QHBoxLayout();
    _new = new QPushButton("New Document...", this);
    _new->setObjectName("cloudNewDocument");
    _upload = new QPushButton("Upload a File...", this);
    _upload->setToolTip("A .phx, FLA, SWF, SVG, EPS or AI file from this computer, made a cloud document");
    bottom->addWidget(_new);
    bottom->addWidget(_upload);
    if (mode == Mode::SaveTo)
    {
        _new->hide();
        _upload->hide();
        bottom->addWidget(new QLabel("Name:", this));
        _name = new QLineEdit(name, this);
        _name->setObjectName("cloudSaveName");
        bottom->addWidget(_name, 1);
    }
    else
        bottom->addStretch(1);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    _ok = buttons->addButton(mode == Mode::SaveTo ? "Save Here" : mode == Mode::Library ? "Open Library" : "Open",
        QDialogButtonBox::AcceptRole);
    _ok->setObjectName("cloudOk");
    _ok->setDefault(true);
    bottom->addWidget(buttons);
    layout->addLayout(bottom);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(_ok, &QPushButton::clicked, this, [this]() {
        if (_mode == Mode::SaveTo)
        {
            // Into the folder shown, or the folder picked in it.
            QString folder = _folder;
            if (QListWidgetItem* item = _items->currentItem(); item && item->data(kKindRole).toString() == "folder")
                folder = item->data(kIdRole).toString();
            const QString name = _name->text().trimmed();
            if (name.isEmpty())
                return;
            if (_saveTo)
                _saveTo(name, folder, _view == "group" ? _group : QString());
            accept();
            return;
        }
        choose(_items->currentItem());
    });
    connect(_items, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) { choose(item); });
    connect(_items, &QListWidget::currentItemChanged, this, [this]() { updateButtons(); });
    connect(_up, &QToolButton::clicked, this, [this]() { up(); });
    connect(_places, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* place) {
        if (!place)
            return;
        _search->blockSignals(true);
        _search->clear();
        _search->blockSignals(false);
        showPlace(place->data(kPlaceView).toString(), place->data(kPlaceGroup).toString());
    });
    QTimer* typing = new QTimer(this);
    typing->setSingleShot(true);
    typing->setInterval(250);
    connect(_search, &QLineEdit::textChanged, typing, qOverload<>(&QTimer::start));
    connect(typing, &QTimer::timeout, this, [this]() {
        const QString text = _search->text().trimmed();
        if (text.isEmpty())
        {
            if (QListWidgetItem* place = _places->currentItem())
                showPlace(place->data(kPlaceView).toString(), place->data(kPlaceGroup).toString());
        }
        else
            search(text);
    });
    connect(_new, &QPushButton::clicked, this, [this]() {
        QPointer<MainWindow> window = _window;
        const QString folder = _view == "files" || _view == "group" ? _folder : QString();
        const QString group = _view == "group" ? _group : QString();
        accept();
        if (window)
            newDocument(window, folder, group);
    });
    connect(_upload, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(this, "Upload a File to Phoenix Cloud", QString(),
            "Documents (*.phx *.fla *.xfl *.swf *.svg *.eps *.ai);;All Files (*)");
        if (file.isEmpty())
            return;
        const QString folder = _view == "files" || _view == "group" ? _folder : QString();
        const QString group = _view == "group" ? _group : QString();
        MainWindow* window = _window;
        accept();
        CloudDocuments::instance().upload(file, QFileInfo(file).fileName(), folder, group, window);
    });

    // Groups, from who is signed in.
    CloudClient::instance().get("/api/me", [this](const CloudClient::Reply& reply) {
        for (const QJsonValue& value : reply.json.value("groups").toArray())
        {
            const QJsonObject group = value.toObject();
            // Saving needs room to add to: an editor of the group at least.
            if (_mode == Mode::SaveTo && group.value("role").toString() == "viewer")
                continue;
            QListWidgetItem* item = new QListWidgetItem(group.value("name").toString(), _places);
            item->setData(kPlaceView, "group");
            item->setData(kPlaceGroup, group.value("id").toString());
        }
    }, this);
    _places->setCurrentRow(0);
}

void CloudBrowser::showPlace(const QString& view, const QString& group, const QString& folder)
{
    _view = view;
    _group = group;
    _folder = folder;
    QString path = "/api/documents?view=" + view;
    if (view == "group")
        path += "&group=" + encoded(group);
    if (!folder.isEmpty())
        path += "&folder=" + encoded(folder);
    const int generation = ++_generation;
    CloudClient::instance().get(path, [this, generation](const CloudClient::Reply& reply) {
        if (generation != _generation)
            return;
        if (!reply.ok())
        {
            _items->clear();
            _empty->setText(reply.message());
            _items->hide();
            _empty->show();
            return;
        }
        fill(reply.json);
    }, this);
}

void CloudBrowser::search(const QString& text)
{
    _view = "search";
    _folder.clear();
    const int generation = ++_generation;
    CloudClient::instance().get("/api/documents?q=" + encoded(text), [this, generation](const CloudClient::Reply& reply) {
        if (generation == _generation)
            fill(reply.json);
    }, this);
}

void CloudBrowser::fill(const QJsonObject& listing)
{
    _items->clear();
    const QJsonArray path = listing.value("path").toArray();
    QStringList names;
    for (const QJsonValue& folder : path)
        names << folder.toObject().value("name").toString();
    const QString place = _places->currentItem() ? _places->currentItem()->text() : QString();
    _where->setText(_view == "search" ? QString("Found") : (QStringList{place} + names).join(QString::fromUtf8(" \xE2\x80\xBA ")));
    _parent = path.size() >= 2 ? path.at(path.size() - 2).toObject().value("id").toString() : QString();
    _up->setEnabled(!_folder.isEmpty());
    _canAdd = listing.value("canEdit").toBool(_view == "files");
    for (const QJsonValue& value : listing.value("folders").toArray())
    {
        const QJsonObject folder = value.toObject();
        QListWidgetItem* item = new QListWidgetItem(cardIcon(QImage(), true, style()), folder.value("name").toString(), _items);
        item->setData(kKindRole, "folder");
        item->setData(kIdRole, folder.value("id").toString());
    }
    if (_mode != Mode::SaveTo)
        for (const QJsonValue& value : listing.value("documents").toArray())
        {
            const QJsonObject doc = value.toObject();
            const bool usable = doc.value("revision").toInt() > 0 &&
                (_mode == Mode::Library ? doc.value("canDownload").toBool() : true);
            QString label = doc.value("name").toString();
            const QJsonObject editing = doc.value("editing").toObject();
            QString tip = doc.value("where").toString();
            if (!editing.isEmpty())
                tip += (tip.isEmpty() ? "" : "\n") + QString("%1 is editing it").arg(editing.value("name").toString());
            QListWidgetItem* item = new QListWidgetItem(cardIcon(), label, _items);
            item->setData(kKindRole, "document");
            item->setData(kIdRole, doc.value("id").toString());
            item->setData(kDocRole, QJsonDocument(doc).toJson(QJsonDocument::Compact));
            item->setToolTip(tip.isEmpty() ? label : label + "\n" + tip);
            if (!usable)
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            const QString thumbnail = doc.value("thumbnailUrl").toString();
            if (!thumbnail.isEmpty())
                loadThumbnail(item, thumbnail);
        }
    const bool none = _items->count() == 0;
    _items->setVisible(!none);
    _empty->setVisible(none);
    _empty->setText(_view == "search" ? "Nothing found." : _mode == Mode::SaveTo ? "Saved here, at the top." : "Nothing here yet.");
    _new->setEnabled(_canAdd || _view == "recent" || _view == "search" || _view == "favorites" || _view == "shared");
    updateButtons();
}

void CloudBrowser::loadThumbnail(QListWidgetItem* item, const QString& url)
{
    const int generation = _generation;
    QPointer<QListWidget> list = _items;
    const QString id = item->data(kIdRole).toString();
    CloudClient::instance().get(url, [this, list, id, generation](const CloudClient::Reply& reply) {
        if (!list || generation != _generation || !reply.ok())
            return;
        const QImage picture = QImage::fromData(reply.body);
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(kIdRole).toString() == id)
                list->item(i)->setIcon(cardIcon(picture));
    }, this);
}

void CloudBrowser::choose(QListWidgetItem* item)
{
    if (!item || !(item->flags() & Qt::ItemIsEnabled))
        return;
    const QString id = item->data(kIdRole).toString();
    if (item->data(kKindRole).toString() == "folder")
    {
        showPlace(_view == "group" ? "group" : "files", _group, id);
        return;
    }
    MainWindow* window = _window;
    const Mode mode = _mode;
    accept();
    if (mode == Mode::Library)
        CloudDocuments::instance().openLibrary(id, window);
    else
        CloudDocuments::instance().open(id, window);
}

void CloudBrowser::up()
{
    if (_folder.isEmpty())
        return;
    showPlace(_view == "group" ? "group" : "files", _group, _parent);
}

void CloudBrowser::updateButtons()
{
    QListWidgetItem* item = _items->currentItem();
    if (_mode == Mode::SaveTo)
    {
        _ok->setEnabled(_canAdd || _view == "files");
        return;
    }
    _ok->setEnabled(item && (item->flags() & Qt::ItemIsEnabled));
}

// -- New Document ------------------------------------------------------------

void CloudBrowser::newDocument(MainWindow* window, const QString& folderId, const QString& groupId)
{
    QDialog* dialog = new QDialog(window);
    dialog->setObjectName("cloudNewDocumentDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("New Cloud Document");
    dialog->resize(640, 460);
    QVBoxLayout* layout = new QVBoxLayout(dialog);
    QFormLayout* naming = new QFormLayout();
    QLineEdit* name = new QLineEdit("Untitled", dialog);
    naming->addRow("Name:", name);
    layout->addLayout(naming);
    QTabWidget* tabs = new QTabWidget(dialog);
    layout->addWidget(tabs, 1);

    // Blank.
    QWidget* blank = new QWidget(tabs);
    QFormLayout* form = new QFormLayout(blank);
    QSpinBox* width = new QSpinBox(blank);
    width->setRange(1, 8192);
    width->setValue(1920);
    width->setSuffix(" px");
    QSpinBox* height = new QSpinBox(blank);
    height->setRange(1, 8192);
    height->setValue(1080);
    height->setSuffix(" px");
    QSpinBox* fps = new QSpinBox(blank);
    fps->setRange(1, 120);
    fps->setValue(24);
    fps->setSuffix(" fps");
    QPushButton* background = new QPushButton(blank);
    auto color = std::make_shared<QColor>(Qt::white);
    const auto showColor = [background, color]() {
        background->setText(color->name());
        background->setStyleSheet(QString("background: %1; color: %2;").arg(color->name(), color->lightness() > 128 ? "black" : "white"));
    };
    showColor();
    QObject::connect(background, &QPushButton::clicked, dialog, [dialog, color, showColor]() {
        const QColor picked = QColorDialog::getColor(*color, dialog, "Background");
        if (picked.isValid())
        {
            *color = picked;
            showColor();
        }
    });
    form->addRow("Width:", width);
    form->addRow("Height:", height);
    form->addRow("Frame rate:", fps);
    form->addRow("Background:", background);
    tabs->addTab(blank, "Blank");

    // Templates and lessons, from the cloud.
    const auto starterList = [dialog]() {
        QListWidget* list = new QListWidget(dialog);
        list->setViewMode(QListView::IconMode);
        list->setIconSize(kCard);
        list->setGridSize(kCard + QSize(24, 44));
        list->setResizeMode(QListView::Adjust);
        list->setMovement(QListView::Static);
        list->setWordWrap(true);
        return list;
    };
    QListWidget* templates = starterList();
    QListWidget* tutorials = starterList();
    tabs->addTab(templates, "Templates");
    tabs->addTab(tutorials, "Lessons");
    QPointer<QDialog> guard = dialog;
    CloudClient::instance().get("/api/starters", [guard, templates, tutorials](const CloudClient::Reply& reply) {
        if (!guard)
            return;
        const auto add = [](QListWidget* list, const QJsonArray& starters) {
            for (const QJsonValue& value : starters)
            {
                const QJsonObject starter = value.toObject();
                QListWidgetItem* item = new QListWidgetItem(cardIcon(), starter.value("title").toString(), list);
                item->setData(kIdRole, starter.value("id").toString());
                item->setToolTip(starter.value("summary").toString());
                const QString thumbnail = starter.value("thumbnailUrl").toString();
                if (thumbnail.isEmpty())
                    continue;
                QPointer<QListWidget> guarded = list;
                const QString id = starter.value("id").toString();
                CloudClient::instance().get(thumbnail, [guarded, id](const CloudClient::Reply& picture) {
                    if (!guarded || !picture.ok())
                        return;
                    for (int i = 0; i < guarded->count(); ++i)
                        if (guarded->item(i)->data(kIdRole).toString() == id)
                            guarded->item(i)->setIcon(cardIcon(QImage::fromData(picture.body)));
                }, guarded);
            }
        };
        add(templates, reply.json.value("templates").toArray());
        add(tutorials, reply.json.value("tutorials").toArray());
    }, dialog);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, dialog);
    QPushButton* make = buttons->addButton("Create", QDialogButtonBox::AcceptRole);
    make->setDefault(true);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QPointer<MainWindow> target = window;
    const auto create = [=]() {
        QJsonObject body{{"name", name->text().trimmed()}};
        if (!folderId.isEmpty())
            body["folderId"] = folderId;
        if (!groupId.isEmpty())
            body["groupId"] = groupId;
        if (tabs->currentWidget() == templates || tabs->currentWidget() == tutorials)
        {
            QListWidget* list = static_cast<QListWidget*>(tabs->currentWidget());
            if (!list->currentItem())
                return;
            body[tabs->currentWidget() == templates ? "template" : "tutorial"] = list->currentItem()->data(kIdRole).toString();
        }
        else
        {
            body["width"] = width->value();
            body["height"] = height->value();
            body["fps"] = fps->value();
            body["background"] = color->name();
        }
        dialog->accept();
        if (target)
            CloudDocuments::instance().create(body, target);
    };
    QObject::connect(make, &QPushButton::clicked, dialog, create);
    QObject::connect(templates, &QListWidget::itemActivated, dialog, create);
    QObject::connect(tutorials, &QListWidget::itemActivated, dialog, create);
    // A template or lesson picked: its name for the document, unless typed.
    const auto named = [name](QListWidgetItem* item) {
        if (item && (name->text() == "Untitled" || name->property("fromStarter").toBool()))
        {
            name->setText(item->text());
            name->setProperty("fromStarter", true);
        }
    };
    QObject::connect(templates, &QListWidget::currentItemChanged, dialog, named);
    QObject::connect(tutorials, &QListWidget::currentItemChanged, dialog, named);
    dialog->show();
}
