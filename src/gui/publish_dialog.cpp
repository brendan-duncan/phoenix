#include "publish_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
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

PublishSettingsDialog::PublishSettingsDialog(const fla::publish::Settings& settings, const QString& documentName,
    int stageWidth, int stageHeight, QWidget* parent)
    : QDialog(parent)
    , _settings(settings)
    , _name(documentName)
{
    setWindowTitle("Publish Settings");
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QString("Profile: <b>%1</b>").arg(QString::fromStdString(settings.profile).toHtmlEscaped()), this));

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
    layout->addWidget(formats);
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
    layout->addWidget(size);

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
    const auto fillQuality = [this](int bitRate, int bits) {
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
    };
    fillQuality(settings.soundBitRate, settings.soundAdpcmBits);
    connect(_soundCompression, &QComboBox::currentIndexChanged, this, [this, fillQuality]() {
        fillQuality(_settings.soundBitRate, _settings.soundAdpcmBits);
    });
    QHBoxLayout* soundRow = new QHBoxLayout;
    soundRow->addWidget(_soundCompression);
    soundRow->addWidget(_soundQuality);
    soundRow->addWidget(_soundRate);
    soundRow->addWidget(_soundStereo);
    soundRow->addStretch(1);
    form->addRow("SWF sound", soundRow);
    layout->addWidget(options);
    QLabel* note = new QLabel("Still images show the frame labeled #Static, or the first frame. The settings "
                              "are kept in the document, so the file publishes the same wherever it goes.",
        this);
    note->setWordWrap(true);
    layout->addWidget(note);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
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
    out.soundCompression = _soundCompression->currentData().toString().toStdString();
    if (out.soundCompression == "mp3" && _soundQuality->count() > 0)
        out.soundBitRate = _soundQuality->currentData().toInt();
    else if (out.soundCompression == "adpcm" && _soundQuality->count() > 0)
        out.soundAdpcmBits = _soundQuality->currentData().toInt();
    out.soundRate = _soundRate->currentData().toInt();
    out.soundStereo = _soundStereo->isChecked();
    return out;
}
