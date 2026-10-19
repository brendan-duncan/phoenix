#pragma once

#include "movie_export.h"

#include "../util/share_formats.h"

#include <QDialog>
#include <QImage>
#include <QSize>
#include <QString>

#include <functional>
#include <vector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;

/// File > Share: the movie as an animated GIF, WebP or PNG, or an MP4 or
/// WebM video, at a size for a social site, ready to post. Big format
/// choices, a few plain settings, a guess at the file's size, and Save.
class ShareDialog : public QDialog
{
    Q_OBJECT

public:
    /// What the dialog needs of the document: a frame drawn at a size
    /// (for the size guess), the movie written as the settings say (false
    /// with an error, or with none when cancelled), and opening a folder
    /// on a file.
    struct Hooks
    {
        std::function<QImage(int frame, const QSize& size, bool transparent)> render;
        std::function<bool(const MovieExportSettings& settings, QString& error)> write;
        std::function<void(const QString& path)> reveal;
    };

    ShareDialog(const QSize& documentSize, double frameRate, int frameCount, const QString& baseName,
        const Hooks& hooks, QWidget* parent);

    /// The settings as chosen, the file not yet named.
    MovieExportSettings settings() const;
    share::Format format() const;

    /// Which formats this Phoenix can write: those Phoenix writes itself,
    /// and those its ffmpeg has the encoder for.
    static bool available(share::Format format);

    /// Writes the movie to \a path as chosen; the dialog shows how it
    /// went. For scripts and checks as well as the Save button.
    bool saveTo(const QString& path);

private:
    void formatChanged();
    void sizeChanged();
    void save();
    void estimate();
    QSize outputSize() const;

    QSize _documentSize;
    double _frameRate;
    int _frameCount;
    QString _baseName;
    Hooks _hooks;
    QButtonGroup* _formats = nullptr;
    QLabel* _blurb = nullptr;
    QComboBox* _size = nullptr;
    QSpinBox* _width = nullptr;
    QSpinBox* _height = nullptr;
    QDoubleSpinBox* _rate = nullptr;
    QComboBox* _plays = nullptr;
    QSlider* _quality = nullptr;
    QCheckBox* _transparent = nullptr;
    QCheckBox* _sound = nullptr;
    QLabel* _estimate = nullptr;
    QLabel* _result = nullptr;
    QPushButton* _save = nullptr;
    QPushButton* _show = nullptr;
    QTimer* _estimateTimer = nullptr;
    QString _saved;
    std::vector<QSize> _sizes;
};
