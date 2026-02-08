#pragma once

class Transform
{
public:
    Transform()
        : m11(1.0), m12(0.0), m21(0.0), m22(1.0), tx(0.0), ty(0.0)
    {}

    static Transform fromTranslate(double tx, double ty)
    {
        Transform t;
        t.tx = tx;
        t.ty = ty;
        return t;
    }

    double m11;  // Scale X
    double m12;  // Shear Y
    double m21;  // Shear X
    double m22;  // Scale Y
    double tx; // Translate X
    double ty; // Translate Y
};
