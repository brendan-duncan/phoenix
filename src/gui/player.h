#pragma once

#include <QObject>

class Player : public QObject
{
    Q_OBJECT
public:
    explicit Player(QObject *parent = nullptr);

    int currentFrame() const { return _currentFrame; }

    void setCurrentFrame(int frame);

    /// Whether the timeline is playing, which is what sound follows.
    bool isPlaying() const { return _playing; }
    void setPlaying(bool playing);

signals:
    void currentFrameChanged(int frame);
    void playingChanged(bool playing);

private:
    int _currentFrame = 0;
    bool _playing = false;
};
