#pragma once

#include "../edit/publish_settings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

/// File > Publish Settings: which formats File > Publish writes, their
/// file names, and each format's options, kept in the document as Animate
/// keeps them.
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
    QSpinBox* _width = nullptr;
    QSpinBox* _height = nullptr;
    QComboBox* _pngDepth = nullptr;
    QSpinBox* _pngColors = nullptr;
    QSpinBox* _jpegQuality = nullptr;
    QCheckBox* _jpegProgressive = nullptr;
    QCheckBox* _gifAnimated = nullptr;
    QComboBox* _gifLoop = nullptr;
    QSpinBox* _gifLoopCount = nullptr;
    QSpinBox* _gifColors = nullptr;
    QCheckBox* _svgHidden = nullptr;
    QCheckBox* _svgEmbed = nullptr;
};
