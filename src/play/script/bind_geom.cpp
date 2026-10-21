// flash.geom: Point, Rectangle, Matrix, ColorTransform and Transform.

#include "native.h"

#include "../display/stage.h"

#include <algorithm>

namespace fla {
namespace play {
namespace script {

namespace {

PointHost& point(VM& vm, const Val& self)
{
    return hostOf<PointHost>(vm, self, "Point");
}

display::Rect& rect(VM& vm, const Val& self)
{
    return hostOf<RectangleHost>(vm, self, "Rectangle").rect;
}

display::Matrix& matrix(VM& vm, const Val& self)
{
    return hostOf<MatrixHost>(vm, self, "Matrix").matrix;
}

display::ColorTransform& color(VM& vm, const Val& self)
{
    return hostOf<ColorTransformHost>(vm, self, "ColorTransform").transform;
}

/// Flash Player's Rectangle.intersection: empty (all zero) when either is
/// empty or they do not overlap.
display::Rect intersection(const display::Rect& a, const display::Rect& b)
{
    if (a.isEmpty() || b.isEmpty())
        return display::Rect();
    display::Rect out;
    out.x = std::max(a.x, b.x);
    out.y = std::max(a.y, b.y);
    out.width = std::min(a.right(), b.right()) - out.x;
    out.height = std::min(a.bottom(), b.bottom()) - out.y;
    if (out.width <= 0.0 || out.height <= 0.0)
        return display::Rect();
    return out;
}

display::Rect united(const display::Rect& a, const display::Rect& b)
{
    if (a.isEmpty())
        return b;
    if (b.isEmpty())
        return a;
    display::Rect out;
    out.x = std::min(a.x, b.x);
    out.y = std::min(a.y, b.y);
    out.width = std::max(a.right(), b.right()) - out.x;
    out.height = std::max(a.bottom(), b.bottom()) - out.y;
    return out;
}

/// A double field of a host, as a property.
template <class H>
as3::NativeMethod getField(double H::*field, const char* what)
{
    return NATIVE(field, what) { return number(hostOf<H>(vm, self, what).*field); };
}

template <class H>
as3::NativeMethod setField(double H::*field, const char* what)
{
    return NATIVE(field, what)
    {
        hostOf<H>(vm, self, what).*field = vm.toNumber(args[0]);
        return Val();
    };
}

} // namespace

Val Bindings::newPoint(double x, double y)
{
    auto host = std::make_shared<PointHost>();
    host->x = x;
    host->y = y;
    return instance("flash.geom.Point", host);
}

Val Bindings::newRectangle(const display::Rect& value)
{
    auto host = std::make_shared<RectangleHost>();
    host->rect = value;
    return instance("flash.geom.Rectangle", host);
}

Val Bindings::newMatrix(const display::Matrix& value)
{
    auto host = std::make_shared<MatrixHost>();
    host->matrix = value;
    return instance("flash.geom.Matrix", host);
}

Val Bindings::newColorTransform(const display::ColorTransform& value)
{
    auto host = std::make_shared<ColorTransformHost>();
    host->transform = value;
    return instance("flash.geom.ColorTransform", host);
}

display::Point Bindings::pointOf(const Val& value)
{
    if (const PointHost* host = value.host<PointHost>())
        return {host->x, host->y};
    if (!value.isObject())
        return {};
    return {vm.toNumber(vm.getProperty(value, "x")), vm.toNumber(vm.getProperty(value, "y"))};
}

display::Rect Bindings::rectOf(const Val& value)
{
    if (const RectangleHost* host = value.host<RectangleHost>())
        return host->rect;
    if (!value.isObject())
        return {};
    return display::Rect(vm.toNumber(vm.getProperty(value, "x")), vm.toNumber(vm.getProperty(value, "y")),
        vm.toNumber(vm.getProperty(value, "width")), vm.toNumber(vm.getProperty(value, "height")));
}

display::Matrix Bindings::matrixOf(const Val& value)
{
    if (const MatrixHost* host = value.host<MatrixHost>())
        return host->matrix;
    if (!value.isObject())
        return {};
    const auto get = [&](const char* name, double otherwise) {
        const Val v = vm.getProperty(value, name);
        return v.isUndefined() ? otherwise : vm.toNumber(v);
    };
    return display::Matrix(get("a", 1), get("b", 0), get("c", 0), get("d", 1), get("tx", 0), get("ty", 0));
}

display::ColorTransform Bindings::colorTransformOf(const Val& value)
{
    if (const ColorTransformHost* host = value.host<ColorTransformHost>())
        return host->transform;
    display::ColorTransform out;
    if (!value.isObject())
        return out;
    const auto get = [&](const char* name, double otherwise) {
        const Val v = vm.getProperty(value, name);
        return v.isUndefined() ? otherwise : vm.toNumber(v);
    };
    out.redMultiplier = get("redMultiplier", 1);
    out.greenMultiplier = get("greenMultiplier", 1);
    out.blueMultiplier = get("blueMultiplier", 1);
    out.alphaMultiplier = get("alphaMultiplier", 1);
    out.redOffset = get("redOffset", 0);
    out.greenOffset = get("greenOffset", 0);
    out.blueOffset = get("blueOffset", 0);
    out.alphaOffset = get("alphaOffset", 0);
    return out;
}

void Bindings::defineGeom()
{
    // A number as a 32-bit float holds it, for Matrix3D's arithmetic.
    vm.defineFunction("flash.geom.__f32", NATIVE() {
        return number(static_cast<double>(static_cast<float>(args.empty() ? 0.0 : vm.toNumber(args[0]))));
    }, 1);

    // Point.
    vm.defineNativeClass("flash.geom.Point")
        .constructor(NATIVE() {
            if (!self.host<PointHost>())
            {
                auto host = std::make_shared<PointHost>();
                host->x = numberArg(vm, args, 0);
                host->y = numberArg(vm, args, 1);
                self->host = host;
            }
            return Val();
        })
        .property("x", getField(&PointHost::x, "Point"), setField(&PointHost::x, "Point"))
        .property("y", getField(&PointHost::y, "Point"), setField(&PointHost::y, "Point"))
        .getter("length", NATIVE() {
            const PointHost& p = point(vm, self);
            return number(std::hypot(p.x, p.y));
        })
        .method("add", NATIVE(this) {
            const PointHost& p = point(vm, self);
            const display::Point v = pointOf(args[0]);
            return newPoint(p.x + v.x, p.y + v.y);
        }, 1)
        .method("subtract", NATIVE(this) {
            const PointHost& p = point(vm, self);
            const display::Point v = pointOf(args[0]);
            return newPoint(p.x - v.x, p.y - v.y);
        }, 1)
        .method("offset", NATIVE() {
            PointHost& p = point(vm, self);
            p.x += numberArg(vm, args, 0);
            p.y += numberArg(vm, args, 1);
            return Val();
        }, 2)
        .method("normalize", NATIVE() {
            PointHost& p = point(vm, self);
            const double length = std::hypot(p.x, p.y);
            if (length > 0.0)
            {
                const double scale = numberArg(vm, args, 0) / length;
                p.x *= scale;
                p.y *= scale;
            }
            return Val();
        }, 1)
        .method("clone", NATIVE(this) {
            const PointHost& p = point(vm, self);
            return newPoint(p.x, p.y);
        })
        .method("equals", NATIVE(this) {
            const PointHost& p = point(vm, self);
            if (!args[0].isObject())
                return boolean(false);
            const display::Point v = pointOf(args[0]);
            return boolean(p.x == v.x && p.y == v.y);
        }, 1)
        .method("setTo", NATIVE() {
            PointHost& p = point(vm, self);
            p.x = numberArg(vm, args, 0);
            p.y = numberArg(vm, args, 1);
            return Val();
        }, 2)
        .method("copyFrom", NATIVE(this) {
            PointHost& p = point(vm, self);
            const display::Point v = pointOf(args[0]);
            p.x = v.x;
            p.y = v.y;
            return Val();
        }, 1)
        .method("toString", NATIVE() {
            const PointHost& p = point(vm, self);
            return text("(x=" + numberText(p.x) + ", y=" + numberText(p.y) + ")");
        })
        .staticMethod("distance", NATIVE(this) {
            const display::Point a = pointOf(args[0]);
            const display::Point b = pointOf(args[1]);
            return number(std::hypot(a.x - b.x, a.y - b.y));
        }, 2)
        .staticMethod("interpolate", NATIVE(this) {
            // f 1 is the first point, 0 the second.
            const display::Point a = pointOf(args[0]);
            const display::Point b = pointOf(args[1]);
            const double f = numberArg(vm, args, 2);
            return newPoint(b.x + (a.x - b.x) * f, b.y + (a.y - b.y) * f);
        }, 3)
        .staticMethod("polar", NATIVE(this) {
            const double length = numberArg(vm, args, 0);
            const double angle = numberArg(vm, args, 1);
            return newPoint(length * std::cos(angle), length * std::sin(angle));
        }, 2)
        .define();

    // Rectangle.
    const auto rectNumber = [](double display::Rect::*field) {
        return NATIVE(field) { return number(rect(vm, self).*field); };
    };
    const auto setRectNumber = [](double display::Rect::*field) {
        return NATIVE(field)
        {
            rect(vm, self).*field = vm.toNumber(args[0]);
            return Val();
        };
    };
    vm.defineNativeClass("flash.geom.Rectangle")
        .constructor(NATIVE() {
            if (!self.host<RectangleHost>())
            {
                auto host = std::make_shared<RectangleHost>();
                host->rect = display::Rect(
                    numberArg(vm, args, 0), numberArg(vm, args, 1), numberArg(vm, args, 2), numberArg(vm, args, 3));
                self->host = host;
            }
            return Val();
        })
        .property("x", rectNumber(&display::Rect::x), setRectNumber(&display::Rect::x))
        .property("y", rectNumber(&display::Rect::y), setRectNumber(&display::Rect::y))
        .property("width", rectNumber(&display::Rect::width), setRectNumber(&display::Rect::width))
        .property("height", rectNumber(&display::Rect::height), setRectNumber(&display::Rect::height))
        .property("left", rectNumber(&display::Rect::x), NATIVE() {
            display::Rect& r = rect(vm, self);
            const double left = vm.toNumber(args[0]);
            r.width += r.x - left;
            r.x = left;
            return Val();
        })
        .property("top", rectNumber(&display::Rect::y), NATIVE() {
            display::Rect& r = rect(vm, self);
            const double top = vm.toNumber(args[0]);
            r.height += r.y - top;
            r.y = top;
            return Val();
        })
        .property("right", NATIVE() { return number(rect(vm, self).right()); }, NATIVE() {
            display::Rect& r = rect(vm, self);
            r.width = vm.toNumber(args[0]) - r.x;
            return Val();
        })
        .property("bottom", NATIVE() { return number(rect(vm, self).bottom()); }, NATIVE() {
            display::Rect& r = rect(vm, self);
            r.height = vm.toNumber(args[0]) - r.y;
            return Val();
        })
        .property("topLeft", NATIVE(this) {
            const display::Rect& r = rect(vm, self);
            return newPoint(r.x, r.y);
        }, NATIVE(this) {
            display::Rect& r = rect(vm, self);
            const display::Point p = pointOf(args[0]);
            r.width += r.x - p.x;
            r.height += r.y - p.y;
            r.x = p.x;
            r.y = p.y;
            return Val();
        })
        .property("bottomRight", NATIVE(this) {
            const display::Rect& r = rect(vm, self);
            return newPoint(r.right(), r.bottom());
        }, NATIVE(this) {
            display::Rect& r = rect(vm, self);
            const display::Point p = pointOf(args[0]);
            r.width = p.x - r.x;
            r.height = p.y - r.y;
            return Val();
        })
        .property("size", NATIVE(this) {
            const display::Rect& r = rect(vm, self);
            return newPoint(r.width, r.height);
        }, NATIVE(this) {
            display::Rect& r = rect(vm, self);
            const display::Point p = pointOf(args[0]);
            r.width = p.x;
            r.height = p.y;
            return Val();
        })
        .method("clone", NATIVE(this) { return newRectangle(rect(vm, self)); })
        .method("isEmpty", NATIVE() {
            const display::Rect& r = rect(vm, self);
            return boolean(r.width <= 0.0 || r.height <= 0.0);
        })
        .method("setEmpty", NATIVE() {
            rect(vm, self) = display::Rect();
            return Val();
        })
        .method("setTo", NATIVE() {
            rect(vm, self) =
                display::Rect(numberArg(vm, args, 0), numberArg(vm, args, 1), numberArg(vm, args, 2), numberArg(vm, args, 3));
            return Val();
        }, 4)
        .method("copyFrom", NATIVE(this) {
            rect(vm, self) = rectOf(args[0]);
            return Val();
        }, 1)
        .method("contains", NATIVE() {
            const display::Rect& r = rect(vm, self);
            const double x = numberArg(vm, args, 0);
            const double y = numberArg(vm, args, 1);
            return boolean(x >= r.x && x < r.right() && y >= r.y && y < r.bottom());
        }, 2)
        .method("containsPoint", NATIVE(this) {
            const display::Rect& r = rect(vm, self);
            const display::Point p = pointOf(args[0]);
            return boolean(p.x >= r.x && p.x < r.right() && p.y >= r.y && p.y < r.bottom());
        }, 1)
        .method("containsRect", NATIVE(this) {
            const display::Rect& r = rect(vm, self);
            const display::Rect o = rectOf(args[0]);
            const double r1 = o.right();
            const double b1 = o.bottom();
            return boolean(o.x >= r.x && o.x < r.right() && o.y >= r.y && o.y < r.bottom() && r1 > r.x &&
                r1 <= r.right() && b1 > r.y && b1 <= r.bottom());
        }, 1)
        .method("equals", NATIVE(this) {
            const display::Rect& r = rect(vm, self);
            if (!args[0].isObject())
                return boolean(false);
            const display::Rect o = rectOf(args[0]);
            return boolean(r.x == o.x && r.y == o.y && r.width == o.width && r.height == o.height);
        }, 1)
        .method("inflate", NATIVE() {
            display::Rect& r = rect(vm, self);
            const double dx = numberArg(vm, args, 0);
            const double dy = numberArg(vm, args, 1);
            r.x -= dx;
            r.width += 2.0 * dx;
            r.y -= dy;
            r.height += 2.0 * dy;
            return Val();
        }, 2)
        .method("inflatePoint", NATIVE(this) {
            display::Rect& r = rect(vm, self);
            const display::Point p = pointOf(args[0]);
            r.x -= p.x;
            r.width += 2.0 * p.x;
            r.y -= p.y;
            r.height += 2.0 * p.y;
            return Val();
        }, 1)
        .method("offset", NATIVE() {
            display::Rect& r = rect(vm, self);
            r.x += numberArg(vm, args, 0);
            r.y += numberArg(vm, args, 1);
            return Val();
        }, 2)
        .method("offsetPoint", NATIVE(this) {
            display::Rect& r = rect(vm, self);
            const display::Point p = pointOf(args[0]);
            r.x += p.x;
            r.y += p.y;
            return Val();
        }, 1)
        .method("intersection", NATIVE(this) { return newRectangle(intersection(rect(vm, self), rectOf(args[0]))); }, 1)
        .method("intersects", NATIVE(this) {
            return boolean(!intersection(rect(vm, self), rectOf(args[0])).isEmpty());
        }, 1)
        .method("union", NATIVE(this) { return newRectangle(united(rect(vm, self), rectOf(args[0]))); }, 1)
        .method("toString", NATIVE() {
            const display::Rect& r = rect(vm, self);
            return text("(x=" + numberText(r.x) + ", y=" + numberText(r.y) + ", w=" + numberText(r.width) +
                ", h=" + numberText(r.height) + ")");
        })
        .define();

    // Matrix.
    const auto matrixNumber = [](double display::Matrix::*field) {
        return NATIVE(field) { return number(matrix(vm, self).*field); };
    };
    const auto setMatrixNumber = [](double display::Matrix::*field) {
        return NATIVE(field)
        {
            matrix(vm, self).*field = vm.toNumber(args[0]);
            return Val();
        };
    };
    vm.defineNativeClass("flash.geom.Matrix")
        .constructor(NATIVE() {
            if (!self.host<MatrixHost>())
            {
                auto host = std::make_shared<MatrixHost>();
                host->matrix = display::Matrix(numberArg(vm, args, 0, 1), numberArg(vm, args, 1), numberArg(vm, args, 2),
                    numberArg(vm, args, 3, 1), numberArg(vm, args, 4), numberArg(vm, args, 5));
                self->host = host;
            }
            return Val();
        })
        .property("a", matrixNumber(&display::Matrix::a), setMatrixNumber(&display::Matrix::a))
        .property("b", matrixNumber(&display::Matrix::b), setMatrixNumber(&display::Matrix::b))
        .property("c", matrixNumber(&display::Matrix::c), setMatrixNumber(&display::Matrix::c))
        .property("d", matrixNumber(&display::Matrix::d), setMatrixNumber(&display::Matrix::d))
        .property("tx", matrixNumber(&display::Matrix::tx), setMatrixNumber(&display::Matrix::tx))
        .property("ty", matrixNumber(&display::Matrix::ty), setMatrixNumber(&display::Matrix::ty))
        .method("clone", NATIVE(this) { return newMatrix(matrix(vm, self)); })
        .method("concat", NATIVE(this) {
            display::Matrix& m = matrix(vm, self);
            m = m.then(matrixOf(args[0]));
            return Val();
        }, 1)
        .method("createBox", NATIVE() {
            matrix(vm, self).createBox(numberArg(vm, args, 0), numberArg(vm, args, 1), numberArg(vm, args, 2),
                numberArg(vm, args, 3), numberArg(vm, args, 4));
            return Val();
        }, 2)
        .method("createGradientBox", NATIVE() {
            matrix(vm, self).createGradientBox(numberArg(vm, args, 0), numberArg(vm, args, 1), numberArg(vm, args, 2),
                numberArg(vm, args, 3), numberArg(vm, args, 4));
            return Val();
        }, 2)
        .method("deltaTransformPoint", NATIVE(this) {
            const display::Point p = pointOf(args[0]);
            const display::Point out = matrix(vm, self).mapDelta(p.x, p.y);
            return newPoint(out.x, out.y);
        }, 1)
        .method("transformPoint", NATIVE(this) {
            const display::Point p = pointOf(args[0]);
            const display::Point out = matrix(vm, self).map(p.x, p.y);
            return newPoint(out.x, out.y);
        }, 1)
        .method("identity", NATIVE() {
            matrix(vm, self).identity();
            return Val();
        })
        .method("invert", NATIVE() {
            matrix(vm, self).invert();
            return Val();
        })
        .method("rotate", NATIVE() {
            matrix(vm, self).rotate(numberArg(vm, args, 0));
            return Val();
        }, 1)
        .method("scale", NATIVE() {
            matrix(vm, self).scale(numberArg(vm, args, 0), numberArg(vm, args, 1));
            return Val();
        }, 2)
        .method("translate", NATIVE() {
            matrix(vm, self).translate(numberArg(vm, args, 0), numberArg(vm, args, 1));
            return Val();
        }, 2)
        .method("setTo", NATIVE() {
            matrix(vm, self) = display::Matrix(numberArg(vm, args, 0), numberArg(vm, args, 1), numberArg(vm, args, 2),
                numberArg(vm, args, 3), numberArg(vm, args, 4), numberArg(vm, args, 5));
            return Val();
        }, 6)
        .method("copyFrom", NATIVE(this) {
            matrix(vm, self) = matrixOf(args[0]);
            return Val();
        }, 1)
        // As a 3 by 3: rows (a, c, tx), (b, d, ty), (0, 0, 1); a row or
        // column past the last is the last.
        .method("copyRowTo", NATIVE() {
            const display::Matrix& m = matrix(vm, self);
            const int row = static_cast<int>(numberArg(vm, args, 0));
            const double values[3][3] = {{m.a, m.c, m.tx}, {m.b, m.d, m.ty}, {0, 0, 1}};
            const double* at = values[std::clamp(row, 0, 2)];
            if (args.size() > 1 && !args[1].isNullish())
            {
                vm.setProperty(args[1], "x", number(at[0]));
                vm.setProperty(args[1], "y", number(at[1]));
                vm.setProperty(args[1], "z", number(at[2]));
            }
            return Val();
        }, 2)
        .method("copyColumnTo", NATIVE() {
            const display::Matrix& m = matrix(vm, self);
            const int column = static_cast<int>(numberArg(vm, args, 0));
            const double values[3][3] = {{m.a, m.b, 0}, {m.c, m.d, 0}, {m.tx, m.ty, 1}};
            const double* at = values[std::clamp(column, 0, 2)];
            if (args.size() > 1 && !args[1].isNullish())
            {
                vm.setProperty(args[1], "x", number(at[0]));
                vm.setProperty(args[1], "y", number(at[1]));
                vm.setProperty(args[1], "z", number(at[2]));
            }
            return Val();
        }, 2)
        // A row from x, y and z; the player's copyColumnFrom does the same.
        .method("copyRowFrom", NATIVE() {
            display::Matrix& m = matrix(vm, self);
            const int row = static_cast<int>(numberArg(vm, args, 0));
            if (args.size() < 2 || args[1].isNullish() || row < 0 || row > 1)
                return Val();
            const double x = vm.toNumber(vm.getProperty(args[1], "x"));
            const double y = vm.toNumber(vm.getProperty(args[1], "y"));
            const double z = vm.toNumber(vm.getProperty(args[1], "z"));
            (row == 0 ? m.a : m.b) = x;
            (row == 0 ? m.c : m.d) = y;
            (row == 0 ? m.tx : m.ty) = z;
            return Val();
        }, 2)
        .method("copyColumnFrom", NATIVE() {
            display::Matrix& m = matrix(vm, self);
            const int column = static_cast<int>(numberArg(vm, args, 0));
            if (args.size() < 2 || args[1].isNullish() || column < 0 || column > 1)
                return Val();
            const double x = vm.toNumber(vm.getProperty(args[1], "x"));
            const double y = vm.toNumber(vm.getProperty(args[1], "y"));
            const double z = vm.toNumber(vm.getProperty(args[1], "z"));
            (column == 0 ? m.a : m.b) = x;
            (column == 0 ? m.c : m.d) = y;
            (column == 0 ? m.tx : m.ty) = z;
            return Val();
        }, 2)
        .method("toString", NATIVE() {
            const display::Matrix& m = matrix(vm, self);
            return text("(a=" + numberText(m.a) + ", b=" + numberText(m.b) + ", c=" + numberText(m.c) + ", d=" +
                numberText(m.d) + ", tx=" + numberText(m.tx) + ", ty=" + numberText(m.ty) + ")");
        })
        .define();

    // ColorTransform.
    const auto colorNumber = [](double display::ColorTransform::*field) {
        return NATIVE(field) { return number(color(vm, self).*field); };
    };
    const auto setColorNumber = [](double display::ColorTransform::*field) {
        return NATIVE(field)
        {
            color(vm, self).*field = vm.toNumber(args[0]);
            return Val();
        };
    };
    vm.defineNativeClass("flash.geom.ColorTransform")
        .constructor(NATIVE() {
            if (!self.host<ColorTransformHost>())
            {
                auto host = std::make_shared<ColorTransformHost>();
                display::ColorTransform& c = host->transform;
                c.redMultiplier = numberArg(vm, args, 0, 1);
                c.greenMultiplier = numberArg(vm, args, 1, 1);
                c.blueMultiplier = numberArg(vm, args, 2, 1);
                c.alphaMultiplier = numberArg(vm, args, 3, 1);
                c.redOffset = numberArg(vm, args, 4);
                c.greenOffset = numberArg(vm, args, 5);
                c.blueOffset = numberArg(vm, args, 6);
                c.alphaOffset = numberArg(vm, args, 7);
                self->host = host;
            }
            return Val();
        })
        .property("redMultiplier", colorNumber(&display::ColorTransform::redMultiplier),
            setColorNumber(&display::ColorTransform::redMultiplier))
        .property("greenMultiplier", colorNumber(&display::ColorTransform::greenMultiplier),
            setColorNumber(&display::ColorTransform::greenMultiplier))
        .property("blueMultiplier", colorNumber(&display::ColorTransform::blueMultiplier),
            setColorNumber(&display::ColorTransform::blueMultiplier))
        .property("alphaMultiplier", colorNumber(&display::ColorTransform::alphaMultiplier),
            setColorNumber(&display::ColorTransform::alphaMultiplier))
        .property("redOffset", colorNumber(&display::ColorTransform::redOffset),
            setColorNumber(&display::ColorTransform::redOffset))
        .property("greenOffset", colorNumber(&display::ColorTransform::greenOffset),
            setColorNumber(&display::ColorTransform::greenOffset))
        .property("blueOffset", colorNumber(&display::ColorTransform::blueOffset),
            setColorNumber(&display::ColorTransform::blueOffset))
        .property("alphaOffset", colorNumber(&display::ColorTransform::alphaOffset),
            setColorNumber(&display::ColorTransform::alphaOffset))
        .property("color", NATIVE() { return number(color(vm, self).color()); }, NATIVE() {
            color(vm, self).setColor(args[0].toUint32());
            return Val();
        })
        .method("concat", NATIVE(this) {
            color(vm, self).concat(colorTransformOf(args[0]));
            return Val();
        }, 1)
        .method("toString", NATIVE() {
            const display::ColorTransform& c = color(vm, self);
            return text("(redMultiplier=" + numberText(c.redMultiplier) + ", greenMultiplier=" +
                numberText(c.greenMultiplier) + ", blueMultiplier=" + numberText(c.blueMultiplier) +
                ", alphaMultiplier=" + numberText(c.alphaMultiplier) + ", redOffset=" + numberText(c.redOffset) +
                ", greenOffset=" + numberText(c.greenOffset) + ", blueOffset=" + numberText(c.blueOffset) +
                ", alphaOffset=" + numberText(c.alphaOffset) + ")");
        })
        .define();

    // Transform: a display object's, read and set through it.
    const auto transformed = [](VM& vm, const Val& self) -> display::DisplayObject& {
        TransformHost& host = hostOf<TransformHost>(vm, self, "Transform");
        if (!host.object)
            vm.throwError("TypeError", "Error #1009: Cannot access a property or method of a null object reference.", 1009);
        return *host.object;
    };
    vm.defineNativeClass("flash.geom.Transform")
        .constructor(NATIVE(this) {
            if (!self.host<TransformHost>())
            {
                auto host = std::make_shared<TransformHost>();
                host->object = sharedDisplayOf(args[0]);
                self->host = host;
            }
            return Val();
        })
        .property("matrix", NATIVE(this, transformed) { return newMatrix(transformed(vm, self).matrix()); },
            NATIVE(this, transformed) {
                if (!args[0].isNullish())
                    transformed(vm, self).setMatrix(matrixOf(args[0]));
                return Val();
            })
        .property("colorTransform",
            NATIVE(this, transformed) { return newColorTransform(transformed(vm, self).colorTransform()); },
            NATIVE(this, transformed) {
                if (args[0].isNullish())
                    vm.throwError("TypeError", "Error #2007: Parameter colorTransform must be non-null.", 2007);
                transformed(vm, self).setColorTransform(colorTransformOf(args[0]));
                return Val();
            })
        .getter("concatenatedMatrix",
            NATIVE(this, transformed) { return newMatrix(transformed(vm, self).concatenatedMatrix()); })
        .getter("concatenatedColorTransform", NATIVE(this, transformed) {
            return newColorTransform(transformed(vm, self).concatenatedColorTransform());
        })
        .getter("pixelBounds", NATIVE(this, transformed) {
            display::DisplayObject& object = transformed(vm, self);
            const display::DisplayObject* space = object.stage();
            if (!space)
                space = object.root();
            display::Rect bounds = object.getBounds(space);
            const double left = std::floor(bounds.x);
            const double top = std::floor(bounds.y);
            bounds = display::Rect(left, top, std::ceil(bounds.right()) - left, std::ceil(bounds.bottom()) - top);
            return newRectangle(bounds);
        })
        .property("matrix3D", NATIVE() { return Val::null(); }, NATIVE() { return Val(); })
        // The stage's and the main timeline's are there from the start;
        // another object has one once a script gives it one. Each read is a
        // new object onto the same projection.
        .property("perspectiveProjection",
            NATIVE(this, transformed) {
                display::DisplayObject& object = transformed(vm, self);
                const display::Stage& stage = runtime.stage();
                const bool isStage = object.kind() == display::ObjectKind::Stage;
                const bool isRoot = !isStage && object.parent() &&
                    object.parent()->kind() == display::ObjectKind::Stage && object.root() == &object;
                if (!object.perspective)
                {
                    if (!isStage && !isRoot)
                        return Val::null();
                    object.perspective = std::make_shared<display::DisplayObject::Perspective>();
                    if (isRoot)
                    {
                        object.perspective->centerX = stage.stageWidth() / 2.0;
                        object.perspective->centerY = stage.stageHeight() / 2.0;
                    }
                }
                return newPerspective(object.perspective, isStage ? 500.0 : stage.stageWidth());
            },
            NATIVE(this, transformed) {
                display::DisplayObject& object = transformed(vm, self);
                const auto* given = args.empty() ? nullptr : args[0].host<PerspectiveHost>();
                if (!given)
                {
                    object.perspective.reset();
                    return Val();
                }
                object.perspective = std::make_shared<display::DisplayObject::Perspective>(*given->state);
                return Val();
            })
        .define();

    // PerspectiveProjection: a field of view onto a width, which gives the
    // focal length (a float, as the player keeps it): 500 for one made on
    // its own, the stage's for one an object has.
    const auto perspective = [](VM& vm, const Val& self) -> PerspectiveHost& {
        return hostOf<PerspectiveHost>(vm, self, "PerspectiveProjection");
    };
    // In floats, as the player works it out.
    const auto focalOf = [](const PerspectiveHost& p) {
        const double turned = (display::kPi - p.state->fieldOfView * display::kPi / 180.0) / 2.0;
        return static_cast<double>(static_cast<float>(p.width / 2.0) * static_cast<float>(std::tan(turned)));
    };
    vm.defineNativeClass("flash.geom.PerspectiveProjection")
        .constructor(NATIVE() {
            if (!self.host<PerspectiveHost>())
            {
                auto host = std::make_shared<PerspectiveHost>();
                host->state = std::make_shared<display::DisplayObject::Perspective>();
                self->host = host;
            }
            return Val();
        })
        .property("fieldOfView", NATIVE(perspective) { return number(perspective(vm, self).state->fieldOfView); },
            NATIVE(perspective) {
                const double fieldOfView = vm.toNumber(args[0]);
                if (!(fieldOfView > 0.0 && fieldOfView < 180.0))
                    vm.throwError("ArgumentError",
                        "Error #2182: Invalid fieldOfView value.  The value must be greater than 0 and less than 180.",
                        2182);
                perspective(vm, self).state->fieldOfView = fieldOfView;
                return Val();
            })
        .property("focalLength", NATIVE(perspective, focalOf) { return number(focalOf(perspective(vm, self))); },
            NATIVE(perspective) {
                PerspectiveHost& p = perspective(vm, self);
                const double focal = vm.toNumber(args[0]);
                if (!(focal > 0.0))
                    vm.throwError("ArgumentError", "Error #2186: Invalid focalLength " + vm.toString(args[0]) + ".", 2186);
                p.state->fieldOfView = 2.0 * std::atan(p.width / 2.0 / focal) * 180.0 / display::kPi;
                return Val();
            })
        .property("projectionCenter",
            NATIVE(this, perspective) {
                const PerspectiveHost& p = perspective(vm, self);
                return newPoint(p.state->centerX, p.state->centerY);
            },
            NATIVE(this, perspective) {
                PerspectiveHost& p = perspective(vm, self);
                const display::Point at = pointOf(args[0]);
                p.state->centerX = at.x;
                p.state->centerY = at.y;
                return Val();
            })
        .method("toMatrix3D", NATIVE(this, perspective, focalOf) {
            const double focal = focalOf(perspective(vm, self));
            const Val cls = vm.applyType(vm.classValue(vm.builtins().vector.get()),
                {vm.classValue(vm.builtins().number.get())});
            Val raw = vm.construct(cls);
            for (double value : {focal, 0.0, 0.0, 0.0, 0.0, focal, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0})
                raw->elements.push_back(number(value));
            return vm.construct(vm.lookupQualified("flash.geom.Matrix3D", true), {raw});
        })
        .define();
}

Val Bindings::newPerspective(std::shared_ptr<display::DisplayObject::Perspective> state, double width)
{
    auto host = std::make_shared<PerspectiveHost>();
    host->state = std::move(state);
    host->width = width;
    return instance("flash.geom.PerspectiveProjection", host);
}

} // namespace script
} // namespace play
} // namespace fla
