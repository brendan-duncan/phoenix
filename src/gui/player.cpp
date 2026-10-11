#include "player.h"

Player::Player(QObject *parent)
    : QObject(parent)
{
}

void Player::setPlaying(bool playing)
{
    if (_playing == playing)
        return;
    _playing = playing;
    emit playingChanged(playing);
}

void Player::setCurrentFrame(int frame)
{
    if (_currentFrame != frame && frame >= 0)
    {
        _currentFrame = frame;
        emit currentFrameChanged(frame);
    }
}

