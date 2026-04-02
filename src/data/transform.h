#pragma once

namespace fla {

class Transform
{
public:
    Transform()
        : m11(1.0), m12(0.0), m21(0.0), m22(1.0), tx(0.0), ty(0.0)
    {}

    Transform(double m11, double m12, double m21, double m22, double tx, double ty)
        : m11(m11), m12(m12), m21(m21), m22(m22), tx(tx), ty(ty)
    {}

    Transform(const Transform& other)
        : m11(other.m11), m12(other.m12), m21(other.m21), m22(other.m22), tx(other.tx), ty(other.ty)
    {}

    void reset()
    {
        m11 = 1.0;
        m12 = 0.0;
        m21 = 0.0;
        m22 = 1.0;
        tx = 0.0;
        ty = 0.0;
    }

    Transform& translate(double dx, double dy)
    {
        tx += dx;
        ty += dy;
        return *this;
    }

    Transform& scale(double sx, double sy)
    {
        m11 *= sx;
        m12 *= sx;
        m21 *= sy;
        m22 *= sy;
        return *this;
    }

    Transform& rotate(double angleDegrees);

    Transform& concatenate(const Transform& other)
    {
        double newM11 = m11 * other.m11 + m21 * other.m12;
        double newM12 = m12 * other.m11 + m22 * other.m12;
        double newM21 = m11 * other.m21 + m21 * other.m22;
        double newM22 = m12 * other.m21 + m22 * other.m22;
        double newTx = m11 * other.tx + m21 * other.ty + tx;
        double newTy = m12 * other.tx + m22 * other.ty + ty;

        m11 = newM11;
        m12 = newM12;
        m21 = newM21;
        m22 = newM22;
        tx = newTx;
        ty = newTy;

        return *this;
    }

    static Transform identity();

    static Transform fromRotation(double angleDegrees);

    static Transform fromScale(double sx, double sy);

    static Transform fromTranslate(double tx, double ty);

    double m11;  // Scale X
    double m12;  // Shear Y
    double m21;  // Shear X
    double m22;  // Scale Y
    double tx; // Translate X
    double ty; // Translate Y
};

} // namespace fla
