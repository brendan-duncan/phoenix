#include "three_d.h"

#include "../data/document.h"
#include "../data/symbol_instance.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fla {

namespace threed {

namespace {

constexpr double kPi = 3.14159265358979323846;

const char* const kRotationX = "phoenixRotationX";
const char* const kRotationY = "phoenixRotationY";
const char* const kZ = "phoenixZ";

std::string numberText(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.10g", value);
    return buffer;
}

} // namespace

Pose of(const SymbolInstance& instance)
{
    Pose pose;
    for (const auto& attribute : instance.extraAttributes)
    {
        if (attribute.first == kRotationX)
            pose.rotationX = std::atof(attribute.second.c_str());
        else if (attribute.first == kRotationY)
            pose.rotationY = std::atof(attribute.second.c_str());
        else if (attribute.first == kZ)
            pose.z = std::atof(attribute.second.c_str());
    }
    return pose;
}

std::vector<std::pair<std::string, std::string>> withPose(const SymbolInstance& instance, const Pose& pose)
{
    std::vector<std::pair<std::string, std::string>> attributes;
    for (const auto& attribute : instance.extraAttributes)
    {
        if (attribute.first != kRotationX && attribute.first != kRotationY && attribute.first != kZ)
            attributes.push_back(attribute);
    }
    if (pose.rotationX != 0.0)
        attributes.emplace_back(kRotationX, numberText(pose.rotationX));
    if (pose.rotationY != 0.0)
        attributes.emplace_back(kRotationY, numberText(pose.rotationY));
    if (pose.z != 0.0)
        attributes.emplace_back(kZ, numberText(pose.z));
    return attributes;
}

Pose blend(const Pose& a, const Pose& b, double t)
{
    Pose pose;
    pose.rotationX = a.rotationX + (b.rotationX - a.rotationX) * t;
    pose.rotationY = a.rotationY + (b.rotationY - a.rotationY) * t;
    pose.z = a.z + (b.z - a.z) * t;
    return pose;
}

namespace {

const std::string* attributeOf(const Document& document, const char* name)
{
    for (const auto& attribute : document.extraAttributes)
        if (attribute.first == name)
            return &attribute.second;
    return nullptr;
}

std::vector<std::pair<std::string, std::string>> withAttribute(const Document& document, const char* name,
    const std::string* value)
{
    std::vector<std::pair<std::string, std::string>> attributes;
    for (const auto& attribute : document.extraAttributes)
        if (attribute.first != name)
            attributes.push_back(attribute);
    if (value)
        attributes.emplace_back(name, *value);
    return attributes;
}

} // namespace

namespace {

/// One light as phoenixLight keeps it: "azimuth,elevation,ambient,intensity,#RRGGBB".
Light lightFrom(const std::string& text)
{
    Light light;
    unsigned int rgb = 0xFFFFFF;
    const int read = std::sscanf(text.c_str(), "%lf,%lf,%lf,%lf,#%6x", &light.azimuth, &light.elevation, &light.ambient,
        &light.intensity, &rgb);
    light.on = read >= 2;
    light.elevation = std::clamp(light.elevation, -90.0, 90.0);
    light.ambient = std::clamp(light.ambient, 0.0, 1.0);
    light.intensity = std::clamp(light.intensity, 0.0, 10.0);
    light.color[0] = static_cast<unsigned char>((rgb >> 16) & 0xFF);
    light.color[1] = static_cast<unsigned char>((rgb >> 8) & 0xFF);
    light.color[2] = static_cast<unsigned char>(rgb & 0xFF);
    return light;
}

std::string textOf(const Light& light)
{
    char text[128];
    std::snprintf(text, sizeof(text), "%g,%g,%g,%g,#%02X%02X%02X", light.azimuth, light.elevation, light.ambient,
        light.intensity, light.color[0], light.color[1], light.color[2]);
    return text;
}

/// Toward \a light, as a direction with z into the stage.
void towardLight(const Light& light, double& lx, double& ly, double& lz)
{
    const double az = light.azimuth * kPi / 180.0;
    const double el = light.elevation * kPi / 180.0;
    lx = std::cos(el) * std::sin(az);
    ly = -std::cos(el) * std::cos(az);
    lz = -std::sin(el);
}

/// How squarely a clip turned by \a pose faces \a light: 0 edge on to 1.
double facing(const Pose& pose, const Light& light)
{
    // The card's face, turned: it starts facing the viewer.
    const double a = pose.rotationX * kPi / 180.0;
    const double b = pose.rotationY * kPi / 180.0;
    const double nx = -std::cos(a) * std::sin(b);
    const double ny = std::sin(a);
    const double nz = -std::cos(a) * std::cos(b);
    double lx, ly, lz;
    towardLight(light, lx, ly, lz);
    return std::min(1.0, std::fabs(nx * lx + ny * ly + nz * lz));
}

double lit(const Light& light, double faces)
{
    return std::clamp(light.ambient + (1.0 - light.ambient) * light.intensity * faces, 0.0, 1.0);
}

} // namespace

Light lightOf(const Document& document)
{
    const std::vector<Light> lights = lightsOf(document);
    return lights.empty() ? Light() : lights.front();
}

std::vector<std::pair<std::string, std::string>> withLight(const Document& document, const Light& light)
{
    return withLights(document, light.on ? std::vector<Light>{light} : std::vector<Light>());
}

std::vector<Light> lightsOf(const Document& document)
{
    std::vector<Light> lights;
    const std::string* text = attributeOf(document, "phoenixLight");
    for (size_t start = 0; text && start <= text->size();)
    {
        const size_t end = std::min(text->find(';', start), text->size());
        const Light light = lightFrom(text->substr(start, end - start));
        if (light.on)
            lights.push_back(light);
        start = end + 1;
    }
    return lights;
}

std::vector<std::pair<std::string, std::string>> withLights(const Document& document, const std::vector<Light>& lights)
{
    std::string value;
    for (const Light& light : lights)
        if (light.on)
            value += (value.empty() ? "" : ";") + textOf(light);
    return withAttribute(document, "phoenixLight", value.empty() ? nullptr : &value);
}

double shading(const Pose& pose, const Light& light)
{
    if (!light.on)
        return 1.0;
    return lit(light, facing(pose, light));
}

void shade(const Pose& pose, const std::vector<Light>& lights, double rgb[3], bool sides)
{
    rgb[0] = rgb[1] = rgb[2] = lights.empty() ? 1.0 : 0.0;
    for (const Light& light : lights)
    {
        if (!light.on)
            continue;
        const double faces = facing(pose, light);
        // The edges stand across the face: as much of the light as it does
        // not catch.
        const double amount = lit(light, sides ? std::sqrt(std::max(0.0, 1.0 - faces * faces)) : faces);
        for (int c = 0; c < 3; ++c)
            rgb[c] += amount * light.color[c] / 255.0;
    }
    for (int c = 0; c < 3; ++c)
        rgb[c] = std::min(rgb[c], 1.0);
}

Shadows shadowsOf(const Document& document)
{
    Shadows shadows;
    if (const std::string* text = attributeOf(document, "phoenixShadows"))
        shadows.on = std::sscanf(text->c_str(), "%lf,%lf", &shadows.strength, &shadows.softness) >= 1;
    shadows.strength = std::clamp(shadows.strength, 0.0, 1.0);
    shadows.softness = std::clamp(shadows.softness, 0.0, 1000.0);
    return shadows;
}

std::vector<std::pair<std::string, std::string>> withShadows(const Document& document, const Shadows& shadows)
{
    if (!shadows.on)
        return withAttribute(document, "phoenixShadows", nullptr);
    const std::string value = numberText(shadows.strength) + "," + numberText(shadows.softness);
    return withAttribute(document, "phoenixShadows", &value);
}

bool shadowOffset(const Light& light, double depth, Point& offset)
{
    double lx, ly, lz;
    towardLight(light, lx, ly, lz);
    if (!light.on || lz >= 0.0)
        return false;
    // Along the light's rays, away from it, until the shadow has gone
    // \a depth further in; a grazing light throws it no further than five
    // times the depth.
    const double along = depth / std::max(-lz, 0.2);
    offset = Point(-lx * along, -ly * along);
    return true;
}

double shadowBlur(const Shadows& shadows, double depth)
{
    return std::fabs(depth) * shadows.softness / 100.0;
}

double extrusionOf(const SymbolInstance& instance)
{
    for (const auto& attribute : instance.extraAttributes)
        if (attribute.first == "phoenixExtrude")
            return std::clamp(std::atof(attribute.second.c_str()), 0.0, 10000.0);
    return 0.0;
}

std::vector<std::pair<std::string, std::string>> withExtrusion(const SymbolInstance& instance, double depth)
{
    std::vector<std::pair<std::string, std::string>> attributes;
    for (const auto& attribute : instance.extraAttributes)
        if (attribute.first != "phoenixExtrude")
            attributes.push_back(attribute);
    if (depth > 0.0)
        attributes.emplace_back("phoenixExtrude", numberText(depth));
    return attributes;
}

DepthOfField depthOfFieldOf(const Document& document)
{
    DepthOfField depth;
    if (const std::string* text = attributeOf(document, "phoenixDepthOfField"))
        depth.on = std::sscanf(text->c_str(), "%lf,%lf", &depth.focus, &depth.aperture) >= 1;
    depth.aperture = std::clamp(depth.aperture, 0.0, 1000.0);
    return depth;
}

std::vector<std::pair<std::string, std::string>> withDepthOfField(const Document& document, const DepthOfField& depth)
{
    if (!depth.on)
        return withAttribute(document, "phoenixDepthOfField", nullptr);
    const std::string value = numberText(depth.focus) + "," + numberText(depth.aperture);
    return withAttribute(document, "phoenixDepthOfField", &value);
}

double blurAt(const DepthOfField& depth, double z)
{
    return depth.on ? std::fabs(z - depth.focus) * depth.aperture / 100.0 : 0.0;
}

double perspectiveAngle(const Document& document)
{
    for (const auto& attribute : document.extraAttributes)
    {
        if (attribute.first == "phoenixPerspectiveAngle")
            return std::clamp(std::atof(attribute.second.c_str()), 1.0, 179.0);
    }
    return 55.0;
}

std::vector<std::pair<std::string, std::string>> withPerspectiveAngle(const Document& document, double angle)
{
    std::vector<std::pair<std::string, std::string>> attributes;
    for (const auto& attribute : document.extraAttributes)
    {
        if (attribute.first != "phoenixPerspectiveAngle")
            attributes.push_back(attribute);
    }
    angle = std::clamp(angle, 1.0, 179.0);
    if (angle != 55.0)
        attributes.emplace_back("phoenixPerspectiveAngle", numberText(angle));
    return attributes;
}

double focalLength(double angle, double width)
{
    return (std::max(width, 1.0) / 2.0) / std::tan(std::clamp(angle, 1.0, 179.0) * kPi / 360.0);
}

std::array<double, 9> projection(const Pose& pose, const Point& center, const Point& vanishing, double focal,
    double depth)
{
    // A point of the clip, d from the center on the flat plane, turned
    // about the x axis and then the y axis:
    //   x' = d.x cos b + d.y sin a sin b
    //   y' = d.y cos a
    //   z' = -d.x sin b + d.y sin a cos b
    // then pushed z further in and seen from focal in front of the
    // vanishing point: (V + (C + (x', y') - V) f / w), w = f + z + z'.
    const double a = pose.rotationX * kPi / 180.0;
    const double b = pose.rotationY * kPi / 180.0;
    const double f = std::max(focal, 1.0);
    // Rows over (d.x, d.y, 1).
    // A depth e behind the face, (0, 0, e) on the flat clip, turns to
    // (e cos a sin b, -e sin a, e cos a cos b).
    const double e = depth;
    const std::array<double, 3> x = {std::cos(b), std::sin(a) * std::sin(b), e * std::cos(a) * std::sin(b)};
    const std::array<double, 3> y = {0.0, std::cos(a), -e * std::sin(a)};
    const std::array<double, 3> w = {-std::sin(b), std::sin(a) * std::cos(b), f + pose.z + e * std::cos(a) * std::cos(b)};
    std::array<double, 3> nx, ny;
    for (int i = 0; i < 3; ++i)
    {
        nx[i] = f * x[i] + vanishing.x * w[i];
        ny[i] = f * y[i] + vanishing.y * w[i];
    }
    nx[2] += f * (center.x - vanishing.x);
    ny[2] += f * (center.y - vanishing.y);
    // Over the point itself: d = p - C.
    const auto shift = [&center](std::array<double, 3> row) {
        row[2] -= row[0] * center.x + row[1] * center.y;
        return row;
    };
    const std::array<double, 3> rx = shift(nx), ry = shift(ny), rw = shift(w);
    // Scaled so a flat clip at z 0 maps with W = 1.
    std::array<double, 9> map = {rx[0] / f, rx[1] / f, rx[2] / f, ry[0] / f, ry[1] / f, ry[2] / f, rw[0] / f, rw[1] / f,
        rw[2] / f};
    return map;
}

double documentFocalLength(const Document& document)
{
    return focalLength(document.viewAngle3D > 0.0 ? document.viewAngle3D : 55.0, document.width);
}

Point vanishingPoint(const Document& document)
{
    if (document.vanishingPoint3DX == 0.0 && document.vanishingPoint3DY == 0.0)
        return Point(document.width / 2.0, document.height / 2.0);
    return Point(document.vanishingPoint3DX, document.vanishingPoint3DY);
}

std::array<double, 9> motionProjection(const Transform& placed, const Point& pivot, double pivotZ, double rotationX,
    double rotationY, double z, const Point& vanishing, double focal)
{
    // The 2D matrix's turn, and what is left of it (scale and skew), which
    // comes first: K = Rz(-turn) L.
    const double turn = std::atan2(placed.m12, placed.m11);
    const double ct = std::cos(turn);
    const double st = std::sin(turn);
    const double k00 = ct * placed.m11 + st * placed.m12;
    const double k10 = -st * placed.m11 + ct * placed.m12;
    const double k01 = ct * placed.m21 + st * placed.m22;
    const double k11 = -st * placed.m21 + ct * placed.m22;
    // Turned about x, then y, then z (the 2D turn), with z into the stage:
    // the top comes toward the viewer for a positive turn about x, the
    // right for one about y.
    const double a = rotationX * kPi / 180.0;
    const double b = rotationY * kPi / 180.0;
    const auto turned = [&](double x, double y, double w, double out[3]) {
        // About x.
        const double y1 = y * std::cos(a) - w * std::sin(a);
        const double z1 = y * std::sin(a) + w * std::cos(a);
        // About y.
        const double x2 = x * std::cos(b) + z1 * std::sin(b);
        const double z2 = -x * std::sin(b) + z1 * std::cos(b);
        // About z.
        out[0] = x2 * ct - y1 * st;
        out[1] = x2 * st + y1 * ct;
        out[2] = z2;
    };
    // Where the pivot is placed, and each axis of the clip and its pivot's
    // offset turned.
    const double cx = placed.m11 * pivot.x + placed.m21 * pivot.y + placed.tx;
    const double cy = placed.m12 * pivot.x + placed.m22 * pivot.y + placed.ty;
    double ux[3], uy[3], origin[3];
    turned(k00, k10, 0.0, ux);
    turned(k01, k11, 0.0, uy);
    turned(-(k00 * pivot.x + k01 * pivot.y), -(k10 * pivot.x + k11 * pivot.y), -pivotZ, origin);
    // q = (cx, cy, z + pivotZ) + origin + ux x + uy y, seen from focal in
    // front of the vanishing point: X = f q.x + V.x q.z, W = f + q.z.
    const double f = std::max(focal, 1.0);
    const double q[3][3] = {{ux[0], uy[0], cx + origin[0]}, {ux[1], uy[1], cy + origin[1]},
        {ux[2], uy[2], z + pivotZ + origin[2]}};
    std::array<double, 9> map;
    for (int i = 0; i < 3; ++i)
    {
        map[i] = (f * q[0][i] + vanishing.x * q[2][i]) / f;
        map[3 + i] = (f * q[1][i] + vanishing.y * q[2][i]) / f;
        map[6 + i] = q[2][i] / f;
    }
    map[8] += 1.0;
    return map;
}

Point project(const std::array<double, 9>& map, const Point& point)
{
    const double X = map[0] * point.x + map[1] * point.y + map[2];
    const double Y = map[3] * point.x + map[4] * point.y + map[5];
    const double W = map[6] * point.x + map[7] * point.y + map[8];
    if (std::fabs(W) < 1.0e-12)
        return point;
    return Point(X / W, Y / W);
}

} // namespace threed

} // namespace fla
