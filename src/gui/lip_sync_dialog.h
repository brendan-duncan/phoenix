#pragma once

#include "../edit/lip_sync.h"

#include <QDialog>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct PcmBuffer;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QRadioButton;
class HotSpinBox;
class QTableWidget;

/// Modify > Lip Sync: where the mouth shapes come from (a library sound or
/// a recording on disk, listened to by Rhubarb Lip Sync or by Phoenix, or a
/// cue file Rhubarb or Papagayo wrote), the mouth chart (which frame of
/// the mouth graphic each shape is drawn on) and the frame the line starts
/// on.
class LipSyncDialog : public QDialog
{
    Q_OBJECT

public:
    /// A library sound's samples, as Phoenix plays it.
    using SoundSource = std::function<std::shared_ptr<const PcmBuffer>(const QString& name)>;

    /// \a labels are the mouth graphic's frame labels, which name the
    /// shapes they show; \a sounds the library's sounds, \a sound the one
    /// to start with (the voice in the scene).
    LipSyncDialog(const QString& mouthName, const std::map<std::string, int>& savedChart,
        const std::map<std::string, int>& labels, const QStringList& sounds, const QString& sound,
        SoundSource soundOf, int startFrame, double frameRate, QWidget* parent = nullptr);

    const std::vector<fla::lipsync::Cue>& cues() const { return _cues; }
    std::map<std::string, int> chart() const;
    /// From 0.
    int startFrame() const;

private:
    /// Reads the cues from the source chosen, and lays out the chart.
    bool read();
    bool runRhubarb(std::string& text);
    /// The library sound chosen; empty when it is a recording on disk.
    QString librarySound() const;
    void fillChart();

    double _frameRate;
    std::map<std::string, int> _savedChart;
    std::map<std::string, int> _labels;
    SoundSource _soundOf;
    std::vector<fla::lipsync::Cue> _cues;
    QComboBox* _sound;
    QRadioButton* _fromAudio;
    QRadioButton* _fromCues;
    QLineEdit* _audio;
    QLineEdit* _rhubarb;
    QPlainTextEdit* _script;
    QLineEdit* _cueFile;
    QTableWidget* _chart;
    HotSpinBox* _start;
    QLabel* _status;
};
