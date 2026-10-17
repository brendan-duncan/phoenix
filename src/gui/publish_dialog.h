#pragma once

#include "../edit/publish_settings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QLineEdit;
class HotSpinBox;

/// The folder \a settings publish into: their publish folder in their
/// parent folder, the parent alone when there is no publish folder. An
/// empty or relative parent is from \a documentFolder.
QString publishFolderPath(const fla::publish::Settings& settings, const QString& documentFolder);

/// File > Publish Settings: the document's publish configurations, and
/// for each where File > Publish writes, which formats, their file names,
/// and each format's options, kept in the document as FLA files keep them
/// (a configuration is one of the file's publish profiles). New copies the
/// configuration shown. Publish closes it and publishes the configuration
/// shown at once.
class PublishSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    /// \a documentFolder is the folder an empty parent folder stands for:
    /// the document's own, or for a document never saved (\a saved false)
    /// the parent folder last chosen.
    PublishSettingsDialog(const std::string& xml, const QString& documentName, int stageWidth, int stageHeight,
        const QString& documentFolder, bool saved, QWidget* parent);

    /// The configuration shown, as its controls have it.
    fla::publish::Settings settings() const;
    /// The publish settings as edited: every configuration, the one shown
    /// current.
    std::string xml() const;
    /// Closed with Publish rather than OK.
    bool publishRequested() const { return _publish; }
    /// Publish as the default button, for when Publish opened the dialog.
    void preferPublish();
    /// The parent folder as chosen, in full.
    QString parentFolder() const;

private:
    void namesChanged();
    void folderChanged();
    void finish(bool publish);
    /// The controls set to \a settings.
    void load(const fla::publish::Settings& settings);
    void fillSoundQuality(int bitRate, int bits);
    /// The configurations listed, \a shown chosen.
    void listProfiles(const std::string& shown);
    void profileChosen();
    void newProfile();
    void renameProfile();
    void deleteProfile();

    fla::publish::Settings _settings;
    /// Every configuration, as last stored from the controls.
    std::string _xml;
    QComboBox* _profile = nullptr;
    QPushButton* _deleteProfile = nullptr;
    int _stageWidth = 0;
    int _stageHeight = 0;
    QString _name;
    QString _documentFolder;
    bool _saved = false;
    bool _publish = false;
    QLineEdit* _parentFolder = nullptr;
    QLineEdit* _publishFolder = nullptr;
    QLabel* _destination = nullptr;
    QPushButton* _publishButton = nullptr;
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
