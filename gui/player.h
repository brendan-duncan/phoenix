#pragma once

#include <QObject>

class Player : public QObject
{
    Q_OBJECT
public:
    explicit Player(QObject *parent = nullptr);

    int currentFrame() const { return _currentFrame; }

    void setCurrentFrame(int frame);

signals:
    void currentFrameChanged(int frame);

private:
    int _currentFrame = 0;
};
