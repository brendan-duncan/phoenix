#include "transform.h"
#include <cmath>

namespace fla {

Transform& Transform::rotate(double angleDegrees)
{
    double angleRadians = angleDegrees * 3.14159265358979323846 / 180.0;
    double cosA = cos(angleRadians);
    double sinA = sin(angleRadians);

    double newM11 = m11 * cosA + m21 * sinA;
    double newM12 = m12 * cosA + m22 * sinA;
    double newM21 = -m11 * sinA + m21 * cosA;
    double newM22 = -m12 * sinA + m22 * cosA;

    m11 = newM11;
    m12 = newM12;
    m21 = newM21;
    m22 = newM22;

    return *this;
}

Transform Transform::identity()
{
    return Transform();
}

Transform Transform::fromRotation(double angleDegrees)
{
    Transform t;
    double angleRadians = angleDegrees * 3.14159265358979323846 / 180.0;
    t.m11 = cos(angleRadians);
    t.m12 = sin(angleRadians);
    t.m21 = -sin(angleRadians);
    t.m22 = cos(angleRadians);
    return t;
}

Transform Transform::fromScale(double sx, double sy)
{
    Transform t;
    t.m11 = sx;
    t.m22 = sy;
    return t;
}

Transform Transform::fromTranslate(double tx, double ty)
{
    Transform t;
    t.tx = tx;
    t.ty = ty;
    return t;
}

} // namespace fla
