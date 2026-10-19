#include "publish_dialog.h"
#include "modal.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include "hot_text.h"
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace {

HotSpinBox* spin(int low, int high, int value, QWidget* parent)
{
    HotSpinBox* box = new HotSpinBox(parent);
    box->setRange(low, high);
    box->setValue(value);
    return box;
}

} // namespace

QString publishFolderPath(const fla::publish::Settings& settings, const QString& documentFolder)
{
    const QDir parent(QDir(documentFolder).filePath(QString::fromStdString(settings.parentFolder)));
    const QString own = QString::fromStdString(settings.publishFolder).trimmed();
    return QDir::cleanPath(own.isEmpty() ? parent.absolutePath() : parent.absoluteFilePath(own));
}

PublishSettingsDialog::PublishSettingsDialog(const std::string& xml, const QString& documentName, int stageWidth,
    int stageHeight, const QString& documentFolder, bool saved, QWidget* parent)
    : QDialog(parent)
    , _settings(fla::publish::read(xml, documentName.toStdString()))
    , _xml(xml)
    , _stageWidth(stageWidth)
    , _stageHeight(stageHeight)
    , _name(documentName)
    , _documentFolder(documentFolder)
    , _saved(saved)
{
    const fla::publish::Settings settings = _settings;
    setWindowTitle("Publish Settings");
    QVBoxLayout* layout = new QVBoxLayout(this);

    // The configurations: each with its own formats, options and location.
    QHBoxLayout* profileRow = new QHBoxLayout;
    profileRow->addWidget(new QLabel("Configuration", this));
    _profile = new QComboBox(this);
    _profile->setToolTip("The publish configuration shown; File > Publish lists them all");
    profileRow->addWidget(_profile, 1);
    QPushButton* add = new QPushButton("New...", this);
    add->setToolTip("A new configuration, a copy of this one");
    QPushButton* rename = new QPushButton("Rename...", this);
    _deleteProfile = new QPushButton("Delete", this);
    profileRow->addWidget(add);
    profileRow->addWidget(rename);
    profileRow->addWidget(_deleteProfile);
    layout->addLayout(profileRow);
    // Two columns, so the dialog fits a laptop's screen: where and what on
    // the left, the formats' options on the right.
    QHBoxLayout* columns = new QHBoxLayout;
    QVBoxLayout* left = new QVBoxLayout;
    QVBoxLayout* right = new QVBoxLayout;
    columns->addLayout(left, 1);
    columns->addLayout(right, 1);
    layout->addLayout(columns);
    // Wide enough for the folders' paths, still well within a laptop's
    // screen.
    setMinimumWidth(860);

    // Where the files go: a folder of their own in the parent folder, or
    // the parent folder itself.
    QGroupBox* location = new QGroupBox("Location", this);
    QFormLayout* where = new QFormLayout(location);
    _parentFolder = new QLineEdit(
        QDir::toNativeSeparators(QDir(documentFolder).absoluteFilePath(QString::fromStdString(settings.parentFolder))),
        location);
    _parentFolder->setToolTip(saved ? "The folder the published files go in, or their publish folder is made in; "
                                      "the document's own folder unless you choose another"
                                    : "The folder the published files go in, or their publish folder is made in");
    QPushButton* browse = new QPushButton("Browse...", location);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString chosen = QFileDialog::getExistingDirectory(this, "Parent Folder", parentFolder());
        if (!chosen.isEmpty())
            _parentFolder->setText(QDir::toNativeSeparators(chosen));
    });
    QHBoxLayout* parentRow = new QHBoxLayout;
    parentRow->addWidget(_parentFolder, 1);
    parentRow->addWidget(browse);
    where->addRow("Parent folder", parentRow);
    _publishFolder = new QLineEdit(QString::fromStdString(settings.publishFolder), location);
    _publishFolder->setPlaceholderText("None: the files go in the parent folder");
    _publishFolder->setToolTip("A folder made in the parent folder to hold the published files; leave it empty to "
                               "write them in the parent folder itself");
    where->addRow("Publish folder", _publishFolder);
    _destination = new QLabel(location);
    _destination->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _destination->setWordWrap(true);
    where->addRow("Files go in", _destination);
    connect(_parentFolder, &QLineEdit::textChanged, this, &PublishSettingsDialog::folderChanged);
    connect(_publishFolder, &QLineEdit::textChanged, this, &PublishSettingsDialog::folderChanged);
    left->addWidget(location);

    // The formats, each with its file name.
    QGroupBox* formats = new QGroupBox("Formats", this);
    QGridLayout* grid = new QGridLayout(formats);
    const auto row = [&](int at, const QString& label, bool on, const std::string& file, QCheckBox*& box,
                         QLineEdit*& edit) {
        box = new QCheckBox(label, formats);
        box->setChecked(on);
        edit = new QLineEdit(QString::fromStdString(file), formats);
        grid->addWidget(box, at, 0);
        grid->addWidget(edit, at, 1);
    };
    row(0, "SWF movie", settings.flash, settings.flashFile, _flash, _flashFile);
    row(1, "HTML page", settings.html, settings.htmlFile, _html, _htmlFile);
    row(2, "PNG image", settings.png, settings.pngFile, _png, _pngFile);
    row(3, "JPEG image", settings.jpeg, settings.jpegFile, _jpeg, _jpegFile);
    row(4, "GIF image", settings.gif, settings.gifFile, _gif, _gifFile);
    row(5, "SVG image", settings.svg, settings.svgFile, _svg, _svgFile);
    row(6, "HTML5 page", settings.html5, settings.html5File, _html5, _html5File);
    _html5->setToolTip("A page that plays the movie in the browser, without the SWF: its symbols, timelines, "
                       "sounds and, for an HTML5 Canvas document, its scripts");
    _defaultNames = new QCheckBox("Name each after the document", formats);
    _defaultNames->setChecked(settings.defaultNames);
    grid->addWidget(_defaultNames, 7, 0, 1, 2);
    // The HTML5 page's name moves aside while the HTML page has the plain one.
    connect(_html, &QCheckBox::toggled, this, &PublishSettingsDialog::namesChanged);
    grid->setColumnStretch(1, 1);
    left->addWidget(formats);
    left->addStretch(1);
    connect(_defaultNames, &QCheckBox::toggled, this, &PublishSettingsDialog::namesChanged);
    namesChanged();

    // The images' size.
    QGroupBox* size = new QGroupBox("Image size", this);
    QHBoxLayout* sizeRow = new QHBoxLayout(size);
    _matchMovie = new QCheckBox("Match the stage", size);
    _matchMovie->setChecked(settings.matchMovie);
    _width = spin(1, 16384, settings.width > 0 ? settings.width : stageWidth, size);
    _height = spin(1, 16384, settings.height > 0 ? settings.height : stageHeight, size);
    sizeRow->addWidget(_matchMovie);
    sizeRow->addWidget(_width);
    sizeRow->addWidget(new QLabel("x", size));
    sizeRow->addWidget(_height);
    sizeRow->addStretch(1);
    connect(_matchMovie, &QCheckBox::toggled, this, [this, stageWidth, stageHeight](bool on) {
        _width->setEnabled(!on);
        _height->setEnabled(!on);
        if (on)
        {
            _width->setValue(stageWidth);
            _height->setValue(stageHeight);
        }
    });
    _width->setEnabled(!settings.matchMovie);
    _height->setEnabled(!settings.matchMovie);
    right->addWidget(size);

    // Each format's options.
    QGroupBox* options = new QGroupBox("Options", this);
    QFormLayout* form = new QFormLayout(options);
    _pngDepth = new QComboBox(options);
    _pngDepth->addItems({"24-bit with Alpha", "24-bit", "8-bit"});
    _pngDepth->setCurrentText(QString::fromStdString(settings.pngBitDepth));
    _pngDepth->setToolTip("With alpha the background is clear; 8-bit is a palette of colors");
    _pngColors = spin(2, 256, settings.pngColors, options);
    QHBoxLayout* pngRow = new QHBoxLayout;
    pngRow->addWidget(_pngDepth, 1);
    pngRow->addWidget(new QLabel("Colors", options));
    pngRow->addWidget(_pngColors);
    form->addRow("PNG", pngRow);
    _jpegQuality = spin(0, 100, settings.jpegQuality, options);
    _jpegProgressive = new QCheckBox("Progressive", options);
    _jpegProgressive->setChecked(settings.jpegProgressive);
    QHBoxLayout* jpegRow = new QHBoxLayout;
    jpegRow->addWidget(new QLabel("Quality", options));
    jpegRow->addWidget(_jpegQuality);
    jpegRow->addWidget(_jpegProgressive);
    jpegRow->addStretch(1);
    form->addRow("JPEG", jpegRow);
    _gifAnimated = new QCheckBox("Animated", options);
    _gifAnimated->setChecked(settings.gifAnimated);
    _gifLoop = new QComboBox(options);
    _gifLoop->addItems({"Loop forever", "Play once", "Repeat"});
    _gifLoop->setCurrentIndex(settings.gifLoop ? 0 : settings.gifLoopCount > 0 ? 2 : 1);
    _gifLoopCount = spin(1, 999, std::max(1, settings.gifLoopCount), options);
    _gifColors = spin(2, 256, settings.gifColors, options);
    QHBoxLayout* gifRow = new QHBoxLayout;
    gifRow->addWidget(_gifAnimated);
    gifRow->addWidget(_gifLoop);
    gifRow->addWidget(_gifLoopCount);
    gifRow->addWidget(new QLabel("Colors", options));
    gifRow->addWidget(_gifColors);
    gifRow->addStretch(1);
    form->addRow("GIF", gifRow);
    _svgHidden = new QCheckBox("Include hidden layers", options);
    _svgHidden->setChecked(settings.svgHiddenLayers);
    _svgEmbed = new QCheckBox("Embed images", options);
    _svgEmbed->setChecked(settings.svgEmbed);
    QHBoxLayout* svgRow = new QHBoxLayout;
    svgRow->addWidget(_svgHidden);
    svgRow->addWidget(_svgEmbed);
    svgRow->addStretch(1);
    form->addRow("SVG", svgRow);
    _html5Fit = new QCheckBox("Fit to the window", options);
    _html5Fit->setChecked(settings.html5Fit);
    _html5Fit->setToolTip("Scale the stage to fill the browser window, keeping its shape; otherwise it shows at "
                          "its own size");
    _html5Loop = new QCheckBox("Loop", options);
    _html5Loop->setChecked(settings.html5Loop);
    _html5Loop->setToolTip("The main timeline starts again after its last frame");
    QHBoxLayout* html5Row = new QHBoxLayout;
    html5Row->addWidget(_html5Fit);
    html5Row->addWidget(_html5Loop);
    html5Row->addStretch(1);
    form->addRow("HTML5", html5Row);
    _swfHidden = new QCheckBox("Include hidden layers", options);
    _swfHidden->setChecked(settings.swfHiddenLayers);
    _swfHidden->setToolTip("Hidden layers play in the SWF and in Test Movie");
    form->addRow("SWF", _swfHidden);

    // The SWF's soundtrack: compression, then its bit rate or bits.
    _soundCompression = new QComboBox(options);
    _soundCompression->addItem("MP3", "mp3");
    _soundCompression->addItem("ADPCM", "adpcm");
    _soundCompression->addItem("Raw", "raw");
    _soundCompression->addItem("None", "none");
    _soundCompression->setCurrentIndex(std::max(0, _soundCompression->findData(QString::fromStdString(settings.soundCompression))));
    _soundCompression->setToolTip("How the SWF's sound is squeezed: MP3 smallest, ADPCM quick and plain, Raw "
                                  "uncompressed, None silent");
    _soundQuality = new QComboBox(options);
    _soundRate = new QComboBox(options);
    for (int rate : {5512, 11025, 22050, 44100})
        _soundRate->addItem(QString("%1 kHz").arg(rate / 1000.0, 0, 'f', rate == 5512 ? 1 : 0), rate);
    _soundRate->setCurrentIndex(std::max(0, _soundRate->findData(settings.soundRate)));
    _soundStereo = new QCheckBox("Stereo", options);
    _soundStereo->setChecked(settings.soundStereo);
    fillSoundQuality(settings.soundBitRate, settings.soundAdpcmBits);
    connect(_soundCompression, &QComboBox::currentIndexChanged, this,
        [this]() { fillSoundQuality(_settings.soundBitRate, _settings.soundAdpcmBits); });
    QHBoxLayout* soundRow = new QHBoxLayout;
    soundRow->addWidget(_soundCompression);
    soundRow->addWidget(_soundQuality);
    soundRow->addWidget(_soundRate);
    soundRow->addWidget(_soundStereo);
    soundRow->addStretch(1);
    form->addRow("SWF sound", soundRow);
    right->addWidget(options);
    right->addStretch(1);
    QLabel* note = new QLabel("Still images show the frame labeled #Static, or the first frame. The settings "
                              "are kept in the document, so the file publishes the same wherever it goes.",
        this);
    note->setWordWrap(true);
    layout->addWidget(note);

    // Publish keeps the settings and publishes with them; OK only keeps
    // them.
    // Publish, OK, Cancel, in that order on every platform.
    QHBoxLayout* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    _publishButton = new QPushButton("Publish", this);
    _publishButton->setToolTip("Keep these settings and publish now");
    _okButton = new QPushButton("OK", this);
    _okButton->setDefault(true);
    QPushButton* cancel = new QPushButton("Cancel", this);
    buttons->addWidget(_publishButton);
    buttons->addWidget(_okButton);
    buttons->addWidget(cancel);
    connect(_publishButton, &QPushButton::clicked, this, [this]() { finish(true); });
    connect(_okButton, &QPushButton::clicked, this, [this]() { finish(false); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    layout->addLayout(buttons);
    listProfiles(settings.profile);
    connect(add, &QPushButton::clicked, this, &PublishSettingsDialog::newProfile);
    connect(rename, &QPushButton::clicked, this, &PublishSettingsDialog::renameProfile);
    connect(_deleteProfile, &QPushButton::clicked, this, &PublishSettingsDialog::deleteProfile);
    connect(_profile, &QComboBox::currentIndexChanged, this, &PublishSettingsDialog::profileChosen);
    folderChanged();
}

void PublishSettingsDialog::listProfiles(const std::string& shown)
{
    const QSignalBlocker quiet(_profile);
    _profile->clear();
    const std::vector<std::string> names = fla::publish::profiles(_xml);
    for (const std::string& name : names)
        _profile->addItem(QString::fromStdString(name));
    if (std::find(names.begin(), names.end(), shown) == names.end())
        _profile->addItem(QString::fromStdString(shown));
    _profile->setCurrentText(QString::fromStdString(shown));
    _deleteProfile->setEnabled(_profile->count() > 1);
}

void PublishSettingsDialog::profileChosen()
{
    // What was shown is kept, then the chosen one shown.
    _xml = fla::publish::write(_xml, settings(), _name.toStdString());
    load(fla::publish::read(_xml, _name.toStdString(), _profile->currentText().toStdString()));
}

void PublishSettingsDialog::newProfile()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New Configuration", "Name:", QLineEdit::Normal,
        _profile->currentText() + " copy", &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    if (_profile->findText(name) >= 0)
    {
        modal::information(this, "New Configuration", QString("There is a configuration named %1.").arg(name));
        return;
    }
    _xml = fla::publish::write(_xml, settings(), _name.toStdString());
    _xml = fla::publish::addProfile(_xml, name.toStdString(), _settings.profile, _name.toStdString());
    load(fla::publish::read(_xml, _name.toStdString(), name.toStdString()));
    listProfiles(name.toStdString());
}

void PublishSettingsDialog::renameProfile()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Rename Configuration", "Name:", QLineEdit::Normal,
        _profile->currentText(), &ok).trimmed();
    if (!ok || name.isEmpty() || name == _profile->currentText())
        return;
    if (_profile->findText(name) >= 0)
    {
        modal::information(this, "Rename Configuration", QString("There is a configuration named %1.").arg(name));
        return;
    }
    // A document with no settings yet has its one configuration written
    // first, so there is one to rename.
    _xml = fla::publish::write(_xml, settings(), _name.toStdString());
    _xml = fla::publish::renameProfile(_xml, _settings.profile, name.toStdString());
    _settings.profile = name.toStdString();
    listProfiles(_settings.profile);
}

void PublishSettingsDialog::deleteProfile()
{
    if (_profile->count() < 2)
        return;
    if (QMessageBox::question(this, "Delete Configuration",
            QString("Delete the configuration %1?").arg(_profile->currentText())) != QMessageBox::Yes)
        return;
    _xml = fla::publish::removeProfile(_xml, _settings.profile);
    const std::string shown = fla::publish::currentProfile(_xml);
    load(fla::publish::read(_xml, _name.toStdString(), shown));
    listProfiles(shown);
}

std::string PublishSettingsDialog::xml() const
{
    const fla::publish::Settings shown = settings();
    return fla::publish::setCurrentProfile(fla::publish::write(_xml, shown, _name.toStdString()), shown.profile);
}

void PublishSettingsDialog::load(const fla::publish::Settings& settings)
{
    _settings = settings;
    _parentFolder->setText(
        QDir::toNativeSeparators(QDir(_documentFolder).absoluteFilePath(QString::fromStdString(settings.parentFolder))));
    _publishFolder->setText(QString::fromStdString(settings.publishFolder));
    const std::pair<QCheckBox*, bool> formats[] = {{_flash, settings.flash}, {_html, settings.html},
        {_png, settings.png}, {_jpeg, settings.jpeg}, {_gif, settings.gif}, {_svg, settings.svg},
        {_html5, settings.html5}};
    for (const auto& [box, on] : formats)
    {
        const QSignalBlocker quiet(box);
        box->setChecked(on);
    }
    const std::pair<QLineEdit*, const std::string*> files[] = {{_flashFile, &settings.flashFile},
        {_htmlFile, &settings.htmlFile}, {_pngFile, &settings.pngFile}, {_jpegFile, &settings.jpegFile},
        {_gifFile, &settings.gifFile}, {_svgFile, &settings.svgFile}, {_html5File, &settings.html5File}};
    for (const auto& [edit, file] : files)
        edit->setText(QString::fromStdString(*file));
    {
        const QSignalBlocker quiet(_defaultNames);
        _defaultNames->setChecked(settings.defaultNames);
    }
    namesChanged();
    _matchMovie->setChecked(settings.matchMovie);
    _width->setValue(settings.width > 0 && !settings.matchMovie ? settings.width : _stageWidth);
    _height->setValue(settings.height > 0 && !settings.matchMovie ? settings.height : _stageHeight);
    _width->setEnabled(!settings.matchMovie);
    _height->setEnabled(!settings.matchMovie);
    _pngDepth->setCurrentText(QString::fromStdString(settings.pngBitDepth));
    _pngColors->setValue(settings.pngColors);
    _jpegQuality->setValue(settings.jpegQuality);
    _jpegProgressive->setChecked(settings.jpegProgressive);
    _gifAnimated->setChecked(settings.gifAnimated);
    _gifLoop->setCurrentIndex(settings.gifLoop ? 0 : settings.gifLoopCount > 0 ? 2 : 1);
    _gifLoopCount->setValue(std::max(1, settings.gifLoopCount));
    _gifColors->setValue(settings.gifColors);
    _svgHidden->setChecked(settings.svgHiddenLayers);
    _svgEmbed->setChecked(settings.svgEmbed);
    _swfHidden->setChecked(settings.swfHiddenLayers);
    _html5Fit->setChecked(settings.html5Fit);
    _html5Loop->setChecked(settings.html5Loop);
    _soundRate->setCurrentIndex(std::max(0, _soundRate->findData(settings.soundRate)));
    _soundStereo->setChecked(settings.soundStereo);
    {
        const QSignalBlocker quiet(_soundCompression);
        _soundCompression->setCurrentIndex(
            std::max(0, _soundCompression->findData(QString::fromStdString(settings.soundCompression))));
    }
    fillSoundQuality(settings.soundBitRate, settings.soundAdpcmBits);
    folderChanged();
}

void PublishSettingsDialog::preferPublish()
{
    _okButton->setDefault(false);
    _publishButton->setDefault(true);
    _publishButton->setFocus();
}

QString PublishSettingsDialog::parentFolder() const
{
    const QString typed = QDir::fromNativeSeparators(_parentFolder->text().trimmed());
    return typed.isEmpty() ? QString() : QDir::cleanPath(QDir(_documentFolder).absoluteFilePath(typed));
}

void PublishSettingsDialog::folderChanged()
{
    const QString parent = parentFolder();
    QPushButton* ok = _okButton;
    if (parent.isEmpty())
    {
        _destination->setText("<i>Choose a parent folder.</i>");
        if (ok)
            ok->setEnabled(false);
        _publishButton->setEnabled(false);
        return;
    }
    if (ok)
        ok->setEnabled(true);
    _publishButton->setEnabled(true);
    fla::publish::Settings place;
    place.parentFolder = parent.toStdString();
    place.publishFolder = _publishFolder->text().trimmed().toStdString();
    _destination->setText(QDir::toNativeSeparators(publishFolderPath(place, _documentFolder)).toHtmlEscaped());
}

void PublishSettingsDialog::finish(bool publish)
{
    if (parentFolder().isEmpty())
        return;
    _publish = publish;
    accept();
}

/// The soundtrack's quality choices for its codec: MP3's bit rates or
/// ADPCM's bits, \a bitRate or \a bits chosen.
void PublishSettingsDialog::fillSoundQuality(int bitRate, int bits)
{
    const QString codec = _soundCompression->currentData().toString();
    _soundQuality->clear();
    if (codec == "mp3")
    {
        for (int kbps : {8, 16, 20, 24, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192})
            _soundQuality->addItem(QString("%1 kbps").arg(kbps), kbps);
        _soundQuality->setCurrentIndex(std::max(0, _soundQuality->findData(bitRate)));
    }
    else if (codec == "adpcm")
    {
        for (int b = 2; b <= 5; ++b)
            _soundQuality->addItem(QString("%1-bit").arg(b), b);
        _soundQuality->setCurrentIndex(std::max(0, _soundQuality->findData(bits)));
    }
    _soundQuality->setEnabled(_soundQuality->count() > 0);
    _soundRate->setEnabled(codec != "none");
    _soundStereo->setEnabled(codec != "none");
    // MP3 has no 5.5 kHz.
    if (codec == "mp3" && _soundRate->currentIndex() == 0)
        _soundRate->setCurrentIndex(1);
}

void PublishSettingsDialog::namesChanged()
{
    const bool own = !_defaultNames->isChecked();
    const std::pair<QLineEdit*, const char*> names[] = {{_flashFile, ".swf"}, {_htmlFile, ".html"},
        {_pngFile, ".png"}, {_jpegFile, ".jpg"}, {_gifFile, ".gif"}, {_svgFile, ".svg"}};
    for (const auto& [edit, extension] : names)
    {
        edit->setEnabled(own);
        if (!own)
            edit->setText(_name + extension);
    }
    _html5File->setEnabled(own);
    if (!own)
        _html5File->setText(_name + (_html->isChecked() ? "_html5.html" : ".html"));
}

fla::publish::Settings PublishSettingsDialog::settings() const
{
    fla::publish::Settings out = _settings;
    // The parent folder from the document's own when the document has one,
    // so the setting goes with the file; empty when it is that folder.
    const QString parent = parentFolder();
    if (_saved)
    {
        const QString relative = QDir(_documentFolder).relativeFilePath(parent);
        out.parentFolder = relative == "." ? std::string() : relative.toStdString();
    }
    else
        out.parentFolder = parent.toStdString();
    out.publishFolder = _publishFolder->text().trimmed().toStdString();
    out.flash = _flash->isChecked();
    out.html = _html->isChecked();
    out.png = _png->isChecked();
    out.jpeg = _jpeg->isChecked();
    out.gif = _gif->isChecked();
    out.svg = _svg->isChecked();
    out.html5 = _html5->isChecked();
    out.html5File = _html5File->text().toStdString();
    out.html5Fit = _html5Fit->isChecked();
    out.html5Loop = _html5Loop->isChecked();
    out.defaultNames = _defaultNames->isChecked();
    out.flashFile = _flashFile->text().toStdString();
    out.htmlFile = _htmlFile->text().toStdString();
    out.pngFile = _pngFile->text().toStdString();
    out.jpegFile = _jpegFile->text().toStdString();
    out.gifFile = _gifFile->text().toStdString();
    out.svgFile = _svgFile->text().toStdString();
    out.matchMovie = _matchMovie->isChecked();
    out.width = _width->value();
    out.height = _height->value();
    out.pngBitDepth = _pngDepth->currentText().toStdString();
    out.pngColors = _pngColors->value();
    out.jpegQuality = _jpegQuality->value();
    out.jpegProgressive = _jpegProgressive->isChecked();
    out.gifAnimated = _gifAnimated->isChecked();
    out.gifLoop = _gifLoop->currentIndex() == 0;
    out.gifLoopCount = _gifLoop->currentIndex() == 2 ? _gifLoopCount->value() : 0;
    out.gifColors = _gifColors->value();
    out.svgHiddenLayers = _svgHidden->isChecked();
    out.svgEmbed = _svgEmbed->isChecked();
    out.swfHiddenLayers = _swfHidden->isChecked();
    out.soundCompression = _soundCompression->currentData().toString().toStdString();
    if (out.soundCompression == "mp3" && _soundQuality->count() > 0)
        out.soundBitRate = _soundQuality->currentData().toInt();
    else if (out.soundCompression == "adpcm" && _soundQuality->count() > 0)
        out.soundAdpcmBits = _soundQuality->currentData().toInt();
    out.soundRate = _soundRate->currentData().toInt();
    out.soundStereo = _soundStereo->isChecked();
    return out;
}
