#pragma once

#include "audio_engine.h"

#include "../edit/frame_sound.h"

#include <QDialog>
#include <QWidget>

#include <memory>
#include <vector>

class QPushButton;
class QLabel;

/// The waveform of a sound in two lanes, left channel over right, each with
/// its volume envelope as a line through square handles, and the in and out
/// points as bars on the ruler between them. Click a lane to add a point,
/// drag a handle to move it, drag it out of its lane to take it away; drag
/// a bar to trim the sound.
class EnvelopeEditor : public QWidget
{
    Q_OBJECT

public:
    EnvelopeEditor(std::shared_ptr<const PcmBuffer> sound, double frameRate, QWidget* parent = nullptr);

    std::vector<fla::frame_sound::EnvelopePoint> points;
    size_t in = 0;
    /// 0 for the sound's end.
    size_t out = 0;

    void zoom(double factor);
    void setShowFrames(bool frames);
    size_t length() const;

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    QSize sizeHint() const override { return QSize(640, 300); }

private:
    QRectF lane(int channel) const;
    QRectF ruler() const;
    double xOf(size_t sample) const;
    size_t sampleAt(double x) const;
    int levelAt(int channel, size_t sample) const;

    std::shared_ptr<const PcmBuffer> _sound;
    double _frameRate;
    double _samplesPerPixel = 1.0;
    bool _showFrames = false;
    /// What is being dragged: a point (index, channel), or a trim bar.
    int _point = -1;
    int _channel = 0;
    int _bar = 0;
};

/// Edit Envelope: the editor with zoom, seconds or frames, and play.
class EnvelopeDialog : public QDialog
{
    Q_OBJECT

public:
    EnvelopeDialog(std::shared_ptr<const PcmBuffer> sound, double frameRate,
        const std::vector<fla::frame_sound::EnvelopePoint>& points, size_t in, size_t out, QWidget* parent = nullptr);
    ~EnvelopeDialog() override;

    EnvelopeEditor* editor() const { return _editor; }

private:
    EnvelopeEditor* _editor;
    std::shared_ptr<const PcmBuffer> _sound;
    int _voice = -1;
};
