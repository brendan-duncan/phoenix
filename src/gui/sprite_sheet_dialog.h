#pragma once

#include "../util/sprite_sheet.h"

#include <QDialog>
#include <QImage>
#include <QString>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

/// File > Export > Export Sprite Sheet, and Generate Sprite Sheet on a
/// library symbol: frames laid out on one image, with a JSON description
/// beside it for game engines.
class SpriteSheetDialog : public QDialog
{
    Q_OBJECT

public:
    /// Draws frame \a frame at \a scale on a clear image, or the stage
    /// color when \a background; every frame of a source the same size.
    using Renderer = std::function<QImage(int frame, double scale, bool background)>;

    SpriteSheetDialog(const QString& source, int frameCount, double frameRate, Renderer render,
        const QString& suggestedPath, QWidget* parent);

    /// Writes the sheet and its description; false with \a error when they
    /// could not be written.
    bool write(QString& error);

private:
    void renderFrames();
    void relayout();
    void browse();
    sprites::Options options() const;

    QString _source;
    double _frameRate = 24.0;
    Renderer _render;
    QLineEdit* _path = nullptr;
    QSpinBox* _first = nullptr;
    QSpinBox* _last = nullptr;
    QSpinBox* _scale = nullptr;
    QComboBox* _layout = nullptr;
    QSpinBox* _columns = nullptr;
    QSpinBox* _maxWidth = nullptr;
    QSpinBox* _padding = nullptr;
    QSpinBox* _border = nullptr;
    QCheckBox* _trim = nullptr;
    QCheckBox* _dedupe = nullptr;
    QCheckBox* _powerOfTwo = nullptr;
    QCheckBox* _background = nullptr;
    QComboBox* _data = nullptr;
    QLabel* _preview = nullptr;
    QLabel* _summary = nullptr;

    std::vector<QImage> _images;
    std::vector<sprites::Frame> _frames;
    sprites::Sheet _sheet;
    QImage _sheetImage;
};
