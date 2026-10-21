// flash.filters, and fl.motion's AdjustColor, ColorMatrix and DynamicMatrix
// (the color math the authoring tool's Adjust Color filter uses).

#include "native.h"

#include <algorithm>

namespace fla {
namespace play {
namespace script {

namespace {

using Filter = display::BitmapFilter;

Filter& filterHost(VM& vm, const Val& self)
{
    return hostOf<FilterHost>(vm, self, "BitmapFilter").filter;
}

template <class T>
as3::NativeMethod getNumber(T Filter::*field)
{
    return NATIVE(field) { return number(static_cast<double>(filterHost(vm, self).*field)); };
}

as3::NativeMethod setDouble(double Filter::*field)
{
    return NATIVE(field)
    {
        filterHost(vm, self).*field = vm.toNumber(args[0]);
        return Val();
    };
}

as3::NativeMethod setInt(int Filter::*field)
{
    return NATIVE(field)
    {
        filterHost(vm, self).*field = args[0].toInt32();
        return Val();
    };
}

as3::NativeMethod setColor(uint32_t Filter::*field)
{
    return NATIVE(field)
    {
        filterHost(vm, self).*field = args[0].toUint32() & 0xffffff;
        return Val();
    };
}

as3::NativeMethod getBool(bool Filter::*field)
{
    return NATIVE(field) { return boolean(filterHost(vm, self).*field); };
}

as3::NativeMethod setBool(bool Filter::*field)
{
    return NATIVE(field)
    {
        filterHost(vm, self).*field = args[0].toBoolean();
        return Val();
    };
}

/// The class a filter's type is.
const char* classOf(Filter::Type type)
{
    switch (type)
    {
    case Filter::Type::Blur:
        return "flash.filters.BlurFilter";
    case Filter::Type::DropShadow:
        return "flash.filters.DropShadowFilter";
    case Filter::Type::Glow:
        return "flash.filters.GlowFilter";
    case Filter::Type::Bevel:
        return "flash.filters.BevelFilter";
    case Filter::Type::GradientGlow:
        return "flash.filters.GradientGlowFilter";
    case Filter::Type::GradientBevel:
        return "flash.filters.GradientBevelFilter";
    case Filter::Type::ColorMatrix:
        return "flash.filters.ColorMatrixFilter";
    case Filter::Type::Convolution:
        return "flash.filters.ConvolutionFilter";
    case Filter::Type::DisplacementMap:
        return "flash.filters.DisplacementMapFilter";
    }
    return "flash.filters.BlurFilter";
}

/// A gradient filter's colors, alphas and ratios, from its stops and into
/// them.
std::vector<display::Gradient::Stop> stopsFrom(Bindings& b, const Val& colors, const Val& alphas, const Val& ratios)
{
    const std::vector<double> c = b.numbers(colors);
    const std::vector<double> a = b.numbers(alphas);
    const std::vector<double> r = b.numbers(ratios);
    std::vector<display::Gradient::Stop> stops;
    for (size_t i = 0; i < c.size(); ++i)
    {
        display::Gradient::Stop stop;
        stop.color = as3::toUint32(c[i]) & 0xffffff;
        stop.alpha = i < a.size() ? std::clamp(a[i], 0.0, 1.0) : 1.0;
        stop.ratio = i < r.size() ? static_cast<int>(std::clamp(r[i], 0.0, 255.0)) : 0;
        stops.push_back(stop);
    }
    return stops;
}

/// A matrix of fl.motion: rows of columns.
struct MatrixValues : as3::HostObject
{
    int width = 0;
    int height = 0;
    std::vector<std::vector<double>> values;

    MatrixValues(int w, int h)
        : width(w)
        , height(h)
        , values(static_cast<size_t>(std::max(0, h)), std::vector<double>(static_cast<size_t>(std::max(0, w)), 0.0))
    {}
    void identity()
    {
        for (int i = 0; i < height; ++i)
            for (int j = 0; j < width; ++j)
                values[i][j] = i == j ? 1.0 : 0.0;
    }
    double at(int row, int col) const
    {
        return row >= 0 && col >= 0 && row < height && col < width ? values[row][col] : 0.0;
    }
    /// DynamicMatrix.Multiply: \a in on the left (prepend, the default) or
    /// the right (append).
    bool multiply(const MatrixValues& in, bool append)
    {
        if (append)
        {
            if (width != in.height)
                return false;
            MatrixValues out(in.width, height);
            for (int i = 0; i < height; ++i)
                for (int j = 0; j < in.width; ++j)
                {
                    double total = 0.0;
                    for (int k = 0; k < width; ++k)
                        total += in.at(k, j) * values[i][k];
                    out.values[i][j] = total;
                }
            *this = out;
            return true;
        }
        if (height != in.width)
            return false;
        MatrixValues out(width, in.height);
        for (int i = 0; i < in.height; ++i)
            for (int j = 0; j < width; ++j)
            {
                double total = 0.0;
                for (int k = 0; k < height; ++k)
                    total += values[k][j] * in.at(i, k);
                out.values[i][j] = total;
            }
        *this = out;
        return true;
    }
    MatrixValues& operator=(const MatrixValues& other)
    {
        width = other.width;
        height = other.height;
        values = other.values;
        return *this;
    }
    MatrixValues(const MatrixValues& other)
        : as3::HostObject()
        , width(other.width)
        , height(other.height)
        , values(other.values)
    {}
};

constexpr double kLumR = 0.3086;
constexpr double kLumG = 0.6094;
constexpr double kLumB = 0.0820;

void setBrightness(MatrixValues& m, double value)
{
    m.values[0][4] = m.values[1][4] = m.values[2][4] = value;
}

void setContrast(MatrixValues& m, double value)
{
    const double brightness = 0.5 * (127.0 - value);
    value /= 127.0;
    m.values[0][0] = m.values[1][1] = m.values[2][2] = value;
    m.values[0][4] = m.values[1][4] = m.values[2][4] = brightness;
}

void setSaturation(MatrixValues& m, double value)
{
    const double sub = 1.0 - value;
    double mul = sub * kLumR;
    m.values[0][0] = mul + value;
    m.values[1][0] = mul;
    m.values[2][0] = mul;
    mul = sub * kLumG;
    m.values[0][1] = mul;
    m.values[1][1] = mul + value;
    m.values[2][1] = mul;
    mul = sub * kLumB;
    m.values[0][2] = mul;
    m.values[1][2] = mul;
    m.values[2][2] = mul + value;
}

void setHue(MatrixValues& m, double angle)
{
    m.identity();
    const double lumR = 0.213;
    const double lumG = 0.715;
    const double lumB = 0.072;
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double base[3][3] = {{lumR, lumG, lumB}, {lumR, lumG, lumB}, {lumR, lumG, lumB}};
    const double cosBase[3][3] = {{1 - lumR, -lumG, -lumB}, {-lumR, 1 - lumG, -lumB}, {-lumR, -lumG, 1 - lumB}};
    const double sinBase[3][3] = {{-lumR, -lumG, 1 - lumB}, {0.143, 0.140, -0.283}, {-(1 - lumR), lumG, lumB}};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            m.values[i][j] = base[i][j] + cosBase[i][j] * c + sinBase[i][j] * s;
}

/// The contrast table AdjustColor denormalizes by.
constexpr double kDeltaIndex[101] = {0, 0.01, 0.02, 0.04, 0.05, 0.06, 0.07, 0.08, 0.1, 0.11, 0.12, 0.14, 0.15, 0.16,
    0.17, 0.18, 0.20, 0.21, 0.22, 0.24, 0.25, 0.27, 0.28, 0.30, 0.32, 0.34, 0.36, 0.38, 0.40, 0.42, 0.44, 0.46, 0.48,
    0.5, 0.53, 0.56, 0.59, 0.62, 0.65, 0.68, 0.71, 0.74, 0.77, 0.80, 0.83, 0.86, 0.89, 0.92, 0.95, 0.98, 1.0, 1.06,
    1.12, 1.18, 1.24, 1.30, 1.36, 1.42, 1.48, 1.54, 1.60, 1.66, 1.72, 1.78, 1.84, 1.90, 1.96, 2.0, 2.12, 2.25, 2.37,
    2.50, 2.62, 2.75, 2.87, 3.0, 3.2, 3.4, 3.6, 3.8, 4.0, 4.3, 4.7, 4.9, 5.0, 5.5, 6.0, 6.5, 6.8, 7.0, 7.3, 7.5, 7.8,
    8.0, 8.4, 8.7, 9.0, 9.4, 9.6, 9.8, 10.0};

/// fl.motion.AdjustColor: a matrix for each of the four once it is set.
struct AdjustColorHost : as3::HostObject
{
    std::optional<MatrixValues> brightness, contrast, saturation, hue;
    double brightnessValue = 0.0, contrastValue = 0.0, saturationValue = 0.0, hueValue = 0.0;
};

MatrixValues colorMatrix()
{
    MatrixValues m(5, 5);
    m.identity();
    return m;
}

} // namespace

bool Bindings::filterOf(const Val& value, display::BitmapFilter& filter)
{
    const FilterHost* host = value.host<FilterHost>();
    if (!host)
        return false;
    filter = host->filter;
    return true;
}

Val Bindings::newFilter(const display::BitmapFilter& filter)
{
    auto host = std::make_shared<FilterHost>();
    host->filter = filter;
    return instance(classOf(filter.type), host);
}

void Bindings::defineFilters()
{
    vm.defineNativeClass("flash.filters.BitmapFilterQuality")
        .constant("LOW", number(1))
        .constant("MEDIUM", number(2))
        .constant("HIGH", number(3))
        .final()
        .define();
    vm.defineNativeClass("flash.filters.BitmapFilterType")
        .constant("INNER", text("inner"))
        .constant("OUTER", text("outer"))
        .constant("FULL", text("full"))
        .final()
        .define();

    vm.defineNativeClass("flash.filters.BitmapFilter")
        .method("clone", NATIVE(this) { return newFilter(filterHost(vm, self)); })
        .define();

    // The filters, each a BitmapFilter with its own fields set.
    const auto make = [](const Val& self, const Filter& filter) {
        if (self.host<FilterHost>())
            return;
        auto host = std::make_shared<FilterHost>();
        host->filter = filter;
        self->host = host;
    };
    const auto blur = std::make_pair(getNumber(&Filter::blurX), setDouble(&Filter::blurX));
    const auto blurY = std::make_pair(getNumber(&Filter::blurY), setDouble(&Filter::blurY));
    const auto quality = std::make_pair(getNumber(&Filter::quality), setInt(&Filter::quality));
    const auto distance = std::make_pair(getNumber(&Filter::distance), setDouble(&Filter::distance));
    const auto angle = std::make_pair(getNumber(&Filter::angle), setDouble(&Filter::angle));
    const auto color = std::make_pair(getNumber(&Filter::color), setColor(&Filter::color));
    const auto alpha = std::make_pair(getNumber(&Filter::alpha), setDouble(&Filter::alpha));
    const auto strength = std::make_pair(getNumber(&Filter::strength), setDouble(&Filter::strength));
    const auto inner = std::make_pair(getBool(&Filter::inner), setBool(&Filter::inner));
    const auto knockout = std::make_pair(getBool(&Filter::knockout), setBool(&Filter::knockout));
    const auto bevelType = std::make_pair(NATIVE() { return text(filterHost(vm, self).bevelType); }, NATIVE() {
        filterHost(vm, self).bevelType = vm.toString(args[0]);
        return Val();
    });

    vm.defineNativeClass("flash.filters.BlurFilter")
        .extends("flash.filters.BitmapFilter")
        .final()
        .constructor(NATIVE(make) {
            make(self, Filter::blur(numberArg(vm, args, 0, 4), numberArg(vm, args, 1, 4), intArg(args, 2, 1)));
            return Val();
        })
        .property("blurX", blur.first, blur.second)
        .property("blurY", blurY.first, blurY.second)
        .property("quality", quality.first, quality.second)
        .define();

    vm.defineNativeClass("flash.filters.DropShadowFilter")
        .extends("flash.filters.BitmapFilter")
        .final()
        .constructor(NATIVE(make) {
            make(self, Filter::dropShadow(numberArg(vm, args, 0, 4), numberArg(vm, args, 1, 45), uintArg(args, 2, 0),
                           numberArg(vm, args, 3, 1), numberArg(vm, args, 4, 4), numberArg(vm, args, 5, 4),
                           numberArg(vm, args, 6, 1), intArg(args, 7, 1), booleanArg(args, 8), booleanArg(args, 9),
                           booleanArg(args, 10)));
            return Val();
        })
        .property("distance", distance.first, distance.second)
        .property("angle", angle.first, angle.second)
        .property("color", color.first, color.second)
        .property("alpha", alpha.first, alpha.second)
        .property("blurX", blur.first, blur.second)
        .property("blurY", blurY.first, blurY.second)
        .property("strength", strength.first, strength.second)
        .property("quality", quality.first, quality.second)
        .property("inner", inner.first, inner.second)
        .property("knockout", knockout.first, knockout.second)
        .property("hideObject", getBool(&Filter::hideObject), setBool(&Filter::hideObject))
        .define();

    vm.defineNativeClass("flash.filters.GlowFilter")
        .extends("flash.filters.BitmapFilter")
        .final()
        .constructor(NATIVE(make) {
            make(self, Filter::glow(uintArg(args, 0, 0xff0000), numberArg(vm, args, 1, 1), numberArg(vm, args, 2, 6),
                           numberArg(vm, args, 3, 6), numberArg(vm, args, 4, 2), intArg(args, 5, 1), booleanArg(args, 6),
                           booleanArg(args, 7)));
            return Val();
        })
        .property("color", color.first, color.second)
        .property("alpha", alpha.first, alpha.second)
        .property("blurX", blur.first, blur.second)
        .property("blurY", blurY.first, blurY.second)
        .property("strength", strength.first, strength.second)
        .property("quality", quality.first, quality.second)
        .property("inner", inner.first, inner.second)
        .property("knockout", knockout.first, knockout.second)
        .define();

    vm.defineNativeClass("flash.filters.BevelFilter")
        .extends("flash.filters.BitmapFilter")
        .final()
        .constructor(NATIVE(make) {
            make(self, Filter::bevel(numberArg(vm, args, 0, 4), numberArg(vm, args, 1, 45), uintArg(args, 2, 0xffffff),
                           numberArg(vm, args, 3, 1), uintArg(args, 4, 0), numberArg(vm, args, 5, 1),
                           numberArg(vm, args, 6, 4), numberArg(vm, args, 7, 4), numberArg(vm, args, 8, 1),
                           intArg(args, 9, 1), stringArg(vm, args, 10, "inner"), booleanArg(args, 11)));
            return Val();
        })
        .property("distance", distance.first, distance.second)
        .property("angle", angle.first, angle.second)
        .property("highlightColor", getNumber(&Filter::highlightColor), setColor(&Filter::highlightColor))
        .property("highlightAlpha", getNumber(&Filter::highlightAlpha), setDouble(&Filter::highlightAlpha))
        .property("shadowColor", getNumber(&Filter::shadowColor), setColor(&Filter::shadowColor))
        .property("shadowAlpha", getNumber(&Filter::shadowAlpha), setDouble(&Filter::shadowAlpha))
        .property("blurX", blur.first, blur.second)
        .property("blurY", blurY.first, blurY.second)
        .property("strength", strength.first, strength.second)
        .property("quality", quality.first, quality.second)
        .property("type", bevelType.first, bevelType.second)
        .property("knockout", knockout.first, knockout.second)
        .define();

    // The gradient filters: colors, alphas and ratios as arrays.
    const auto gradient = [&](const char* name, Filter::Type type) {
        vm.defineNativeClass(name)
            .extends("flash.filters.BitmapFilter")
            .final()
            .constructor(NATIVE(this, make, type) {
                Filter filter = Filter::bevel(numberArg(vm, args, 0, 4), numberArg(vm, args, 1, 45), 0xffffff, 1.0, 0, 1.0,
                    numberArg(vm, args, 5, 4), numberArg(vm, args, 6, 4), numberArg(vm, args, 7, 1), intArg(args, 8, 1),
                    stringArg(vm, args, 9, "inner"), booleanArg(args, 10));
                filter.type = type;
                filter.stops = stopsFrom(*this, args[2], args[3], args[4]);
                make(self, filter);
                return Val();
            })
            .property("distance", distance.first, distance.second)
            .property("angle", angle.first, angle.second)
            .property("blurX", blur.first, blur.second)
            .property("blurY", blurY.first, blurY.second)
            .property("strength", strength.first, strength.second)
            .property("quality", quality.first, quality.second)
            .property("type", bevelType.first, bevelType.second)
            .property("knockout", knockout.first, knockout.second)
            .property("colors", NATIVE() {
                std::vector<Val> out;
                for (const auto& stop : filterHost(vm, self).stops)
                    out.push_back(number(stop.color));
                return vm.newArray(std::move(out));
            }, NATIVE(this) {
                Filter& f = filterHost(vm, self);
                std::vector<Val> alphas, ratios;
                for (const auto& stop : f.stops)
                {
                    alphas.push_back(number(stop.alpha));
                    ratios.push_back(number(stop.ratio));
                }
                f.stops = stopsFrom(*this, args[0], vm.newArray(alphas), vm.newArray(ratios));
                return Val();
            })
            .property("alphas", NATIVE() {
                std::vector<Val> out;
                for (const auto& stop : filterHost(vm, self).stops)
                    out.push_back(number(stop.alpha));
                return vm.newArray(std::move(out));
            }, NATIVE(this) {
                Filter& f = filterHost(vm, self);
                const std::vector<double> alphas = numbers(args[0]);
                for (size_t i = 0; i < f.stops.size() && i < alphas.size(); ++i)
                    f.stops[i].alpha = std::clamp(alphas[i], 0.0, 1.0);
                return Val();
            })
            .property("ratios", NATIVE() {
                std::vector<Val> out;
                for (const auto& stop : filterHost(vm, self).stops)
                    out.push_back(number(stop.ratio));
                return vm.newArray(std::move(out));
            }, NATIVE(this) {
                Filter& f = filterHost(vm, self);
                const std::vector<double> ratios = numbers(args[0]);
                for (size_t i = 0; i < f.stops.size() && i < ratios.size(); ++i)
                    f.stops[i].ratio = static_cast<int>(std::clamp(ratios[i], 0.0, 255.0));
                return Val();
            })
            .define();
    };
    gradient("flash.filters.GradientGlowFilter", Filter::Type::GradientGlow);
    gradient("flash.filters.GradientBevelFilter", Filter::Type::GradientBevel);

    vm.defineNativeClass("flash.filters.ColorMatrixFilter")
        .extends("flash.filters.BitmapFilter")
        .final()
        .constructor(NATIVE(this, make) {
            make(self, Filter::colorMatrix(args[0].isObject() ? numbers(args[0]) : std::vector<double>()));
            return Val();
        })
        .property("matrix", NATIVE(this) { return numberArray(filterHost(vm, self).matrix); }, NATIVE(this) {
            filterHost(vm, self) = Filter::colorMatrix(args[0].isObject() ? numbers(args[0]) : std::vector<double>());
            return Val();
        })
        .define();

    vm.defineNativeClass("flash.filters.ConvolutionFilter")
        .extends("flash.filters.BitmapFilter")
        .final()
        .constructor(NATIVE(this, make) {
            make(self, Filter::convolution(intArg(args, 0), intArg(args, 1), numbers(args[2]), numberArg(vm, args, 3, 1),
                           numberArg(vm, args, 4), booleanArg(args, 5, true), booleanArg(args, 6, true),
                           uintArg(args, 7), numberArg(vm, args, 8)));
            return Val();
        })
        .property("matrixX", getNumber(&Filter::matrixX), NATIVE() {
            Filter& f = filterHost(vm, self);
            f = Filter::convolution(args[0].toInt32(), f.matrixY, f.matrix, f.divisor, f.bias, f.preserveAlpha, f.clamp,
                f.color, f.alpha);
            return Val();
        })
        .property("matrixY", getNumber(&Filter::matrixY), NATIVE() {
            Filter& f = filterHost(vm, self);
            f = Filter::convolution(f.matrixX, args[0].toInt32(), f.matrix, f.divisor, f.bias, f.preserveAlpha, f.clamp,
                f.color, f.alpha);
            return Val();
        })
        .property("matrix", NATIVE(this) { return numberArray(filterHost(vm, self).matrix); }, NATIVE(this) {
            Filter& f = filterHost(vm, self);
            f = Filter::convolution(
                f.matrixX, f.matrixY, numbers(args[0]), f.divisor, f.bias, f.preserveAlpha, f.clamp, f.color, f.alpha);
            return Val();
        })
        .property("divisor", getNumber(&Filter::divisor), setDouble(&Filter::divisor))
        .property("bias", getNumber(&Filter::bias), setDouble(&Filter::bias))
        .property("preserveAlpha", getBool(&Filter::preserveAlpha), setBool(&Filter::preserveAlpha))
        .property("clamp", getBool(&Filter::clamp), setBool(&Filter::clamp))
        .property("color", color.first, color.second)
        .property("alpha", alpha.first, alpha.second)
        .define();

    // DisplacementMapFilter: its map kept as given; its point in whole
    // pixels, its scales as 32-bit floats.
    vm.defineNativeClass("flash.filters.DisplacementMapFilterMode")
        .constant("WRAP", text("wrap"))
        .constant("CLAMP", text("clamp"))
        .constant("IGNORE", text("ignore"))
        .constant("COLOR", text("color"))
        .final()
        .define();
    const auto host = [](VM& vm, const Val& self) -> FilterHost& { return hostOf<FilterHost>(vm, self, "BitmapFilter"); };
    const auto f32 = [](double value) { return static_cast<double>(static_cast<float>(value)); };
    const auto whole = [](double value) { return std::isnan(value) ? 0.0 : std::trunc(value); };
    const auto checkedMode = [](VM& vm, const std::string& mode) {
        if (mode != "wrap" && mode != "clamp" && mode != "ignore" && mode != "color")
            vm.throwError("ArgumentError", "Error #2008: Parameter mode must be one of the accepted values.", 2008);
        return mode;
    };
    vm.defineNativeClass("flash.filters.DisplacementMapFilter")
        .extends("flash.filters.BitmapFilter")
        .final()
        .constructor(NATIVE(this, make, f32, whole, checkedMode) {
            if (self.host<FilterHost>())
                return Val();
            const display::Point point = args[1].isObject() ? pointOf(args[1]) : display::Point();
            make(self, Filter::displacementMap(bitmapDataOf(args[0]), display::Point(whole(point.x), whole(point.y)),
                           uintArg(args, 2), uintArg(args, 3), f32(numberArg(vm, args, 4)), f32(numberArg(vm, args, 5)),
                           given(args, 6) ? checkedMode(vm, vm.toString(args[6])) : std::string("wrap"),
                           uintArg(args, 7), numberArg(vm, args, 8)));
            self.host<FilterHost>()->map = args[0].isObject() ? args[0] : Val::null();
            return Val();
        })
        .property("mapBitmap", NATIVE(host) {
            const Val& map = host(vm, self).map;
            return map.isObject() ? map : Val::null();
        }, NATIVE(this, host) {
            host(vm, self).map = args[0].isObject() ? args[0] : Val::null();
            host(vm, self).filter.mapBitmap = bitmapDataOf(args[0]);
            return Val();
        })
        .property("mapPoint", NATIVE(this, host) {
            const display::Point& p = host(vm, self).filter.mapPoint;
            return newPoint(p.x, p.y);
        }, NATIVE(this, host, whole) {
            const display::Point p = args[0].isObject() ? pointOf(args[0]) : display::Point();
            host(vm, self).filter.mapPoint = display::Point(whole(p.x), whole(p.y));
            return Val();
        })
        .property("componentX", NATIVE(host) { return number(host(vm, self).filter.componentX); }, NATIVE(host) {
            host(vm, self).filter.componentX = args[0].toUint32();
            return Val();
        })
        .property("componentY", NATIVE(host) { return number(host(vm, self).filter.componentY); }, NATIVE(host) {
            host(vm, self).filter.componentY = args[0].toUint32();
            return Val();
        })
        .property("scaleX", NATIVE(host) { return number(host(vm, self).filter.scaleX); }, NATIVE(host, f32) {
            host(vm, self).filter.scaleX = f32(vm.toNumber(args[0]));
            return Val();
        })
        .property("scaleY", NATIVE(host) { return number(host(vm, self).filter.scaleY); }, NATIVE(host, f32) {
            host(vm, self).filter.scaleY = f32(vm.toNumber(args[0]));
            return Val();
        })
        .property("mode", NATIVE(host) { return text(host(vm, self).filter.mode); }, NATIVE(host, checkedMode) {
            host(vm, self).filter.mode = checkedMode(vm, vm.toString(args[0]));
            return Val();
        })
        .property("color", NATIVE(host) { return number(host(vm, self).filter.color); }, NATIVE(host) {
            host(vm, self).filter.color = args[0].toUint32() & 0xffffff;
            return Val();
        })
        .property("alpha", NATIVE(host) { return number(host(vm, self).filter.alpha); }, NATIVE(host) {
            host(vm, self).filter.alpha = std::clamp(vm.toNumber(args[0]), 0.0, 1.0);
            return Val();
        })
        .define();

    // fl.motion.DynamicMatrix and ColorMatrix.
    const auto values = [](VM& vm, const Val& self) -> MatrixValues& {
        return hostOf<MatrixValues>(vm, self, "DynamicMatrix");
    };
    vm.defineNativeClass("fl.motion.DynamicMatrix")
        .constant("MATRIX_ORDER_PREPEND", number(0))
        .constant("MATRIX_ORDER_APPEND", number(1))
        .constructor(NATIVE() {
            if (!self.host<MatrixValues>())
                self->host = std::make_shared<MatrixValues>(intArg(args, 0), intArg(args, 1));
            return Val();
        })
        .method("GetWidth", NATIVE(values) { return number(values(vm, self).width); })
        .method("GetHeight", NATIVE(values) { return number(values(vm, self).height); })
        .method("GetValue", NATIVE(values) { return number(values(vm, self).at(intArg(args, 0), intArg(args, 1))); }, 2)
        .method("SetValue", NATIVE(values) {
            MatrixValues& m = values(vm, self);
            const int row = intArg(args, 0);
            const int col = intArg(args, 1);
            if (row >= 0 && col >= 0 && row < m.height && col < m.width)
                m.values[row][col] = numberArg(vm, args, 2);
            return Val();
        }, 3)
        .method("LoadIdentity", NATIVE(values) {
            values(vm, self).identity();
            return Val();
        })
        .method("LoadZeros", NATIVE(values) {
            for (auto& row : values(vm, self).values)
                std::fill(row.begin(), row.end(), 0.0);
            return Val();
        })
        .method("Multiply", NATIVE(values) {
            const MatrixValues* in = args[0].host<MatrixValues>();
            return boolean(in && values(vm, self).multiply(*in, intArg(args, 1) == 1));
        }, 1)
        .method("MultiplyNumber", NATIVE(values) {
            const double by = numberArg(vm, args, 0);
            for (auto& row : values(vm, self).values)
                for (double& value : row)
                    value *= by;
            return boolean(true);
        }, 1)
        .method("Add", NATIVE(values) {
            MatrixValues& m = values(vm, self);
            const MatrixValues* in = args[0].host<MatrixValues>();
            if (!in || in->width != m.width || in->height != m.height)
                return boolean(false);
            for (int i = 0; i < m.height; ++i)
                for (int j = 0; j < m.width; ++j)
                    m.values[i][j] += in->values[i][j];
            return boolean(true);
        }, 1)
        .define();
    vm.defineNativeClass("fl.motion.ColorMatrix")
        .extends("fl.motion.DynamicMatrix")
        .constructor(NATIVE() {
            if (!self.host<MatrixValues>())
                self->host = std::make_shared<MatrixValues>(colorMatrix());
            return Val();
        })
        .method("SetBrightnessMatrix", NATIVE(values) {
            setBrightness(values(vm, self), numberArg(vm, args, 0));
            return Val();
        }, 1)
        .method("SetContrastMatrix", NATIVE(values) {
            setContrast(values(vm, self), numberArg(vm, args, 0));
            return Val();
        }, 1)
        .method("SetSaturationMatrix", NATIVE(values) {
            setSaturation(values(vm, self), numberArg(vm, args, 0));
            return Val();
        }, 1)
        .method("SetHueMatrix", NATIVE(values) {
            setHue(values(vm, self), numberArg(vm, args, 0));
            return Val();
        }, 1)
        .method("GetFlatArray", NATIVE(this, values) {
            const MatrixValues& m = values(vm, self);
            std::vector<double> flat;
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 5; ++j)
                    flat.push_back(m.at(i, j));
            return numberArray(flat);
        })
        .define();

    // fl.motion.AdjustColor: brightness, contrast, saturation and hue
    // (-100 to 100, hue -180 to 180) as a ColorMatrixFilter's matrix, made
    // as the component library makes it.
    const auto adjust = [](VM& vm, const Val& self) -> AdjustColorHost& {
        return hostOf<AdjustColorHost>(vm, self, "AdjustColor");
    };
    vm.defineNativeClass("fl.motion.AdjustColor")
        .constructor(NATIVE() {
            if (!self.host<AdjustColorHost>())
                self->host = std::make_shared<AdjustColorHost>();
            return Val();
        })
        .property("brightness", NATIVE(adjust) { return number(adjust(vm, self).brightnessValue); }, NATIVE(adjust) {
            AdjustColorHost& a = adjust(vm, self);
            const double value = vm.toNumber(args[0]);
            a.brightnessValue = value;
            if (!a.brightness)
                a.brightness = colorMatrix();
            if (value != 0.0)
                setBrightness(*a.brightness, value);
            return Val();
        })
        .property("contrast", NATIVE(adjust) { return number(adjust(vm, self).contrastValue); }, NATIVE(adjust) {
            AdjustColorHost& a = adjust(vm, self);
            const double value = vm.toNumber(args[0]);
            a.contrastValue = value;
            double denormal = 127.0;
            if (value > 0.0)
                denormal = kDeltaIndex[std::clamp(static_cast<int>(value), 0, 100)] * 127.0 + 127.0;
            else if (value < 0.0)
                denormal = value / 100.0 * 127.0 + 127.0;
            if (!a.contrast)
                a.contrast = colorMatrix();
            setContrast(*a.contrast, denormal);
            return Val();
        })
        .property("saturation", NATIVE(adjust) { return number(adjust(vm, self).saturationValue); }, NATIVE(adjust) {
            AdjustColorHost& a = adjust(vm, self);
            const double value = vm.toNumber(args[0]);
            a.saturationValue = value;
            double denormal = 1.0;
            if (value > 0.0)
                denormal = 1.0 + 3.0 * value / 100.0;
            else if (value < 0.0)
                denormal = value / 100.0 + 1.0;
            if (!a.saturation)
                a.saturation = colorMatrix();
            setSaturation(*a.saturation, denormal);
            return Val();
        })
        .property("hue", NATIVE(adjust) { return number(adjust(vm, self).hueValue); }, NATIVE(adjust) {
            AdjustColorHost& a = adjust(vm, self);
            const double value = vm.toNumber(args[0]);
            a.hueValue = value;
            if (!a.hue)
                a.hue = colorMatrix();
            if (value != 0.0)
                setHue(*a.hue, value * display::kPi / 180.0);
            return Val();
        })
        .method("AllValuesAreSet", NATIVE(adjust) {
            const AdjustColorHost& a = adjust(vm, self);
            return boolean(a.brightness && a.contrast && a.saturation && a.hue);
        })
        .method("CalculateFinalFlatArray", NATIVE(this, adjust) {
            const AdjustColorHost& a = adjust(vm, self);
            if (!a.brightness || !a.contrast || !a.saturation || !a.hue)
                return Val::null();
            MatrixValues final = colorMatrix();
            final.multiply(*a.brightness, false);
            final.multiply(*a.contrast, false);
            final.multiply(*a.saturation, false);
            final.multiply(*a.hue, false);
            std::vector<double> flat;
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 5; ++j)
                    flat.push_back(final.at(i, j));
            return numberArray(flat);
        })
        .define();
}

} // namespace script
} // namespace play
} // namespace fla
