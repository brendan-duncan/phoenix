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
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

QSpinBox* spin(int low, int high, int value, QWidget* parent)
{
    QSpinBox* box = new QSpinBox(parent);
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
    _defaultNames = new QCheckBox("Name each after the document", formats);
    _defaultNames->setChecked(settings.defaultNames);
    grid->addWidget(_defaultNames, 6, 0, 1, 2);
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
    layout->addWidget(options);
    layout->addWidget(new QLabel("Still images show the frame labeled #Static, or the first frame. The settings "
                                 "are kept in the document, where Animate reads them too.",
        this));

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
    return out;
}
