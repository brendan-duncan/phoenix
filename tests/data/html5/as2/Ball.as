// A ball that rolls to the right and starts again; linked to the Ball
// symbol (identifier "ball").
class Ball extends MovieClip {
    var speed:Number = 3;

    function Ball() {
        trace("Ball() " + _name + " speed " + speed);
    }

    function onEnterFrame() {
        _x += speed;
        if (_x > 180) {
            _x = 60;
        }
    }

    function onRelease() {
        trace("ball released " + _name);
    }
}
