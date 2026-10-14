#pragma once

#include "../edit/lip_sync.h"

#include <QDialog>

#include <map>
#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QRadioButton;
class HotSpinBox;
class QTableWidget;

/// Modify > Lip Sync: where the mouth shapes come from (Rhubarb Lip Sync
/// run on a voice recording, or a cue file Rhubarb or Papagayo wrote), the
/// mouth chart (which frame of the mouth graphic each shape is drawn on)
/// and the frame the line starts on.
class LipSyncDialog : public QDialog
{
    Q_OBJECT

public:
    LipSyncDialog(const QString& mouthName, const std::map<std::string, int>& savedChart, int startFrame,
        double frameRate, QWidget* parent = nullptr);

    const std::vector<fla::lipsync::Cue>& cues() const { return _cues; }
    std::map<std::string, int> chart() const;
    /// From 0.
    int startFrame() const;

private:
    /// Reads the cues from the source chosen, and lays out the chart.
    bool read();
    bool runRhubarb(std::string& text);
    void fillChart();

    double _frameRate;
    std::map<std::string, int> _savedChart;
    std::vector<fla::lipsync::Cue> _cues;
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
