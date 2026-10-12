#pragma once

#include <QColor>
#include <QDialog>
#include <QImage>
#include <QSize>
#include <QString>

class PhoenixView;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QToolButton;

/// What File > Export > Export Image writes: one frame as a PNG, JPEG or
/// GIF, at a size, on a matte.
struct ImageExportSettings
{
    enum class Format
    {
        Png,
        Jpeg,
        Gif
    };
    enum class Matte
    {
        Stage,
        Transparent,
        Color
    };

    Format format = Format::Png;
    QString path;
    QSize size;
    int frame = 0;
    Matte matte = Matte::Stage;
    QColor matteColor = Qt::white;
    /// JPEG quality, 0 to 100.
    int quality = 90;
    /// PNG: 8-bit, a palette of \a colors, rather than full color.
    bool png8 = false;
    /// For an 8-bit PNG or a GIF: the palette's size, 2 to 256, and whether
    /// its colors are dithered.
    int colors = 256;
    bool dither = false;
};

/// \a picture cut down to a palette of \a colors as an 8-bit image, its
/// clear pixels one transparent entry when \a transparent.
QImage toIndexed(const QImage& picture, bool transparent, int colors, bool dither);

class ImageExportDialog : public QDialog
{
    Q_OBJECT

public:
    ImageExportDialog(const QSize& documentSize, int frameCount, int currentFrame, const QString& suggestedPath,
        QWidget* parent);

    ImageExportSettings settings() const;

    /// The format a file name's extension asks for.
    static ImageExportSettings::Format formatOf(const QString& path);

private:
    void formatChanged();
    void browse();

    QSize _documentSize;
    QLineEdit* _path = nullptr;
    QComboBox* _format = nullptr;
    QSpinBox* _width = nullptr;
    QSpinBox* _height = nullptr;
    QCheckBox* _keepAspect = nullptr;
    QSpinBox* _frame = nullptr;
    QComboBox* _matte = nullptr;
    QToolButton* _matteSwatch = nullptr;
    QColor _matteColor = Qt::white;
    QSpinBox* _quality = nullptr;
    QComboBox* _depth = nullptr;
    QSpinBox* _colors = nullptr;
    QCheckBox* _dither = nullptr;
    bool _syncing = false;
};

namespace ImageExport {

/// Renders the frame and writes the file; false with \a error when it could
/// not be written.
bool run(const ImageExportSettings& settings, PhoenixView& view, QString& error);

} // namespace ImageExport
