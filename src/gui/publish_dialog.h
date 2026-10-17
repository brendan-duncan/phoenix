#pragma once

#include "../edit/publish_settings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class HotSpinBox;

/// File > Publish Settings: which formats File > Publish writes, their
/// file names, and each format's options, kept in the document as FLA files
/// keep them.
class PublishSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    PublishSettingsDialog(const fla::publish::Settings& settings, const QString& documentName, int stageWidth,
        int stageHeight, QWidget* parent);

    fla::publish::Settings settings() const;

private:
    void namesChanged();

    fla::publish::Settings _settings;
    QString _name;
    QCheckBox* _flash = nullptr;
    QCheckBox* _html = nullptr;
    QCheckBox* _png = nullptr;
    QCheckBox* _jpeg = nullptr;
    QCheckBox* _gif = nullptr;
    QCheckBox* _svg = nullptr;
    QLineEdit* _flashFile = nullptr;
    QLineEdit* _htmlFile = nullptr;
    QLineEdit* _pngFile = nullptr;
    QLineEdit* _jpegFile = nullptr;
    QLineEdit* _gifFile = nullptr;
    QLineEdit* _svgFile = nullptr;
    QCheckBox* _defaultNames = nullptr;
    QCheckBox* _matchMovie = nullptr;
    HotSpinBox* _width = nullptr;
    HotSpinBox* _height = nullptr;
    QComboBox* _pngDepth = nullptr;
    HotSpinBox* _pngColors = nullptr;
    HotSpinBox* _jpegQuality = nullptr;
    QCheckBox* _jpegProgressive = nullptr;
    QCheckBox* _gifAnimated = nullptr;
    QComboBox* _gifLoop = nullptr;
    HotSpinBox* _gifLoopCount = nullptr;
    HotSpinBox* _gifColors = nullptr;
    QCheckBox* _svgHidden = nullptr;
    QCheckBox* _svgEmbed = nullptr;
    QCheckBox* _html5 = nullptr;
    QLineEdit* _html5File = nullptr;
    QCheckBox* _html5Fit = nullptr;
    QCheckBox* _html5Loop = nullptr;
    QComboBox* _soundCompression = nullptr;
    QComboBox* _soundQuality = nullptr;
    QComboBox* _soundRate = nullptr;
    QCheckBox* _soundStereo = nullptr;
};
