#include "lottie_export.h"

#include "html5_publish.h"

#include "../data/action_script.h"
#include "../data/document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/symbol.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <functional>
#include <list>
#include <map>
#include <set>
#include <unordered_map>

namespace LottieExport {

namespace {

constexpr double kPi = 3.14159265358979323846;

double rounded(double value, double unit = 1000.0)
{
    const double r = std::round(value * unit) / unit;
    return r == 0.0 ? 0.0 : r;
}

QJsonArray point(const QPointF& p)
{
    return QJsonArray{rounded(p.x()), rounded(p.y())};
}

/// A value that does not change.
QJsonObject still(const QJsonValue& value)
{
    return QJsonObject{{"a", 0}, {"k", value}};
}

QJsonArray color(int rgb)
{
    return QJsonArray{rounded(((rgb >> 16) & 0xff) / 255.0, 10000.0), rounded(((rgb >> 8) & 0xff) / 255.0, 10000.0),
        rounded((rgb & 0xff) / 255.0, 10000.0), 1};
}

QTransform matrixOf(const QJsonValue& value)
{
    const QJsonArray m = value.toArray();
    if (m.size() != 6)
        return QTransform();
    return QTransform(m[0].toDouble(), m[1].toDouble(), m[2].toDouble(), m[3].toDouble(), m[4].toDouble(), m[5].toDouble());
}

// ---------------------------------------------------------------------------
// Paths: the content's SVG path data (M, L, C and Z, absolute) as Lottie's
// shapes, a vertex list each with its in and out handles.

struct Vertex
{
    QPointF at;
    QPointF in;
    QPointF out;
};

struct Outline
{
    std::vector<Vertex> points;
    bool closed = false;
};

std::vector<Outline> outlinesOf(const QString& data)
{
    std::vector<Outline> outlines;
    std::vector<double> numbers;
    QChar command;
    const auto flush = [&]() {
        if (command.isNull())
            return;
        const auto pt = [&numbers](size_t i) { return QPointF(numbers[i], numbers[i + 1]); };
        if (command == 'M' && numbers.size() >= 2)
        {
            outlines.push_back({});
            outlines.back().points.push_back({pt(0), {}, {}});
            // More pairs after a move are lines.
            for (size_t i = 2; i + 1 < numbers.size(); i += 2)
                outlines.back().points.push_back({pt(i), {}, {}});
        }
        else if (command == 'L' && !outlines.empty())
        {
            for (size_t i = 0; i + 1 < numbers.size(); i += 2)
                outlines.back().points.push_back({pt(i), {}, {}});
        }
        else if (command == 'C' && !outlines.empty())
        {
            for (size_t i = 0; i + 5 < numbers.size(); i += 6)
            {
                Vertex& from = outlines.back().points.back();
                from.out = pt(i) - from.at;
                const QPointF end = pt(i + 4);
                outlines.back().points.push_back({end, pt(i + 2) - end, {}});
            }
        }
        else if (command == 'Z' && !outlines.empty())
        {
            Outline& outline = outlines.back();
            outline.closed = true;
            // Back where it began: the last point is the first again.
            if (outline.points.size() > 1)
            {
                const Vertex& last = outline.points.back();
                Vertex& first = outline.points.front();
                if (std::abs(last.at.x() - first.at.x()) < 1e-6 && std::abs(last.at.y() - first.at.y()) < 1e-6)
                {
                    first.in = last.in;
                    outline.points.pop_back();
                }
            }
        }
        numbers.clear();
    };
    const int n = static_cast<int>(data.size());
    int i = 0;
    while (i < n)
    {
        const QChar c = data[i];
        if (c.isLetter() && c != 'e' && c != 'E')
        {
            flush();
            command = c.toUpper();
            ++i;
            continue;
        }
        if (c.isDigit() || c == '-' || c == '+' || c == '.')
        {
            int j = i + 1;
            while (j < n)
            {
                const QChar d = data[j];
                if (d.isDigit() || d == '.')
                    ++j;
                else if ((d == 'e' || d == 'E') && j + 1 < n)
                    j += (data[j + 1] == '-' || data[j + 1] == '+') ? 2 : 1;
                else
                    break;
            }
            numbers.push_back(data.mid(i, j - i).toDouble());
            i = j;
            continue;
        }
        ++i;
    }
    flush();
    outlines.erase(std::remove_if(outlines.begin(), outlines.end(), [](const Outline& o) { return o.points.size() < 2; }),
        outlines.end());
    return outlines;
}

QJsonObject shapeData(const Outline& outline, const QTransform& by = QTransform())
{
    QJsonArray v, in, out;
    for (const Vertex& p : outline.points)
    {
        const QPointF at = by.map(p.at);
        v.append(point(at));
        in.append(point(by.map(p.at + p.in) - at));
        out.append(point(by.map(p.at + p.out) - at));
    }
    return QJsonObject{{"c", outline.closed}, {"v", v}, {"i", in}, {"o", out}};
}

QJsonArray pathShapes(const QString& data)
{
    QJsonArray shapes;
    for (const Outline& outline : outlinesOf(data))
        shapes.append(QJsonObject{{"ty", "sh"}, {"ks", still(shapeData(outline))}});
    return shapes;
}

QJsonObject identityTransform(double opacity = 100.0)
{
    return QJsonObject{{"ty", "tr"}, {"p", still(QJsonArray{0, 0})}, {"a", still(QJsonArray{0, 0})},
        {"s", still(QJsonArray{100, 100})}, {"r", still(0)}, {"o", still(rounded(opacity, 100.0))}, {"sk", still(0)},
        {"sa", still(0)}};
}

// ---------------------------------------------------------------------------
// Values held frame by frame.

/// A property's value on each frame it is given, written as one value when
/// it never changes, else as hold keyframes where it does.
class Held
{
public:
    /// \a value is what it is when it is never given one.
    explicit Held(const QJsonArray& value)
        : _default(value)
    {
    }

    void set(int frame, const QJsonArray& value)
    {
        if (!_keys.empty() && _keys.back().second == value)
            return;
        _keys.emplace_back(frame, value);
    }

    QJsonObject json(bool scalar) const
    {
        if (_keys.empty())
            return still(scalar ? _default.at(0) : QJsonValue(_default));
        if (_keys.size() == 1)
            return still(scalar ? _keys.front().second.at(0) : QJsonValue(_keys.front().second));
        QJsonArray keys;
        for (const auto& [frame, value] : _keys)
            keys.append(QJsonObject{{"t", frame}, {"s", value}, {"h", 1}});
        return QJsonObject{{"a", 1}, {"k", keys}};
    }

private:
    QJsonArray _default;
    std::vector<std::pair<int, QJsonArray>> _keys;
};

/// A place, size, turn and opacity on each frame, as Lottie's transform.
class HeldTransform
{
public:
    void set(int frame, const QTransform& m, double opacity)
    {
        double rotation = 0.0, scaleX = 100.0, scaleY = 100.0, skew = 0.0;
        if (!decompose(m.m11(), m.m12(), m.m21(), m.m22(), rotation, scaleX, scaleY, skew))
        {
            // Flat: nothing shows.
            opacity = 0.0;
            rotation = skew = 0.0;
            scaleX = scaleY = 0.0;
        }
        _position.set(frame, QJsonArray{rounded(m.dx()), rounded(m.dy())});
        _scale.set(frame, QJsonArray{rounded(scaleX), rounded(scaleY)});
        _rotation.set(frame, QJsonArray{rounded(rotation)});
        _skew.set(frame, QJsonArray{rounded(skew)});
        _opacity.set(frame, QJsonArray{rounded(std::clamp(opacity, 0.0, 1.0) * 100.0, 100.0)});
        _last = m;
    }

    /// Nothing shows on \a frame: where it was, see-through.
    void hide(int frame)
    {
        _opacity.set(frame, QJsonArray{0});
    }

    QJsonObject json(bool group) const
    {
        QJsonObject out{{"p", _position.json(false)}, {"a", still(QJsonArray{0, 0})}, {"s", _scale.json(false)},
            {"r", _rotation.json(true)}, {"o", _opacity.json(true)}, {"sk", _skew.json(true)}, {"sa", still(0)}};
        if (group)
            out["ty"] = "tr";
        return out;
    }

private:
    Held _position{QJsonArray{0, 0}}, _scale{QJsonArray{100, 100}}, _rotation{QJsonArray{0}}, _skew{QJsonArray{0}},
        _opacity{QJsonArray{100}};
    QTransform _last;
};

// ---------------------------------------------------------------------------

int blendNumber(const QString& mode, bool& known)
{
    known = true;
    static const QHash<QString, int> modes = {{"normal", 0}, {"multiply", 1}, {"screen", 2}, {"overlay", 3},
        {"darken", 4}, {"lighten", 5}, {"hardlight", 8}, {"difference", 10}};
    const auto found = modes.find(mode.toLower());
    if (found != modes.end())
        return found.value();
    known = mode.isEmpty();
    return 0;
}

/// A part of a shape that becomes a layer: a run of paths drawn with their
/// fills and strokes, or a picture (a bitmap, or a bitmap fill), either
/// perhaps clipped.
struct Part
{
    QJsonArray groups;
    int image = -1;
    QTransform imageMatrix;
    double opacity = 1.0;
    QString clip;
};

/// A color effect: red, green and blue multiplied, then offset (0 to 255).
struct ColorEffect
{
    double multiply[3] = {1.0, 1.0, 1.0};
    double offset[3] = {0.0, 0.0, 0.0};

    bool none() const
    {
        for (int i = 0; i < 3; ++i)
            if (multiply[i] != 1.0 || offset[i] != 0.0)
                return false;
        return true;
    }

    /// This effect, then \a outer over it.
    ColorEffect then(const ColorEffect& outer) const
    {
        ColorEffect both;
        for (int i = 0; i < 3; ++i)
        {
            both.multiply[i] = multiply[i] * outer.multiply[i];
            both.offset[i] = offset[i] * outer.multiply[i] + outer.offset[i];
        }
        return both;
    }

    /// A Lottie color (0 to 1 a channel) through the effect.
    double apply(int channel, double value) const
    {
        return std::clamp((value * 255.0 * multiply[channel] + offset[channel]) / 255.0, 0.0, 1.0);
    }
};

/// Something drawn on a frame, every symbol it is in opened up.
struct Leaf
{
    QString key;
    QChar kind;
    int id = -1;
    QTransform matrix;
    double alpha = 1.0;
    ColorEffect color;
    QString matte;
    QString maskOf;
    QString blend;
    QJsonArray filters;
};

struct TrackFrame
{
    QTransform matrix;
    double alpha = 1.0;
    ColorEffect color;
};

/// One layer of the animation: a part of something drawn, frame by frame.
struct Track
{
    QChar kind;
    int id = -1;
    int part = 0;
    QString matte;
    QString blend;
    QJsonArray filters;
    std::map<int, TrackFrame> frames;
};

class Exporter
{
public:
    Exporter(const QJsonObject& content, const fla::Document& document, const QString& folder, QStringList& notes)
        : _content(content)
        , _folder(folder)
        , _notes(notes)
    {
        _art = content["art"].toArray();
        _texts = content["texts"].toArray();
        _symbols = content["symbols"].toArray();
        _images = content["images"].toArray();
        // Where each symbol's timeline stops (a stop() on a keyframe), so a
        // movie clip holds there as it plays rather than going round.
        static const QRegularExpression stop("\\bstop\\s*\\(\\s*\\)");
        if (document.symbolList)
            for (const fla::Symbol* symbol : document.symbolList->symbols)
            {
                if (!symbol || symbol->timelines.empty() || !symbol->timelines.front())
                    continue;
                for (const fla::Layer* layer : symbol->timelines.front()->layers)
                    for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
                        if (frame && frame->actionScript &&
                            QString::fromStdString(frame->actionScript->code).contains(stop))
                            _stops[QString::fromStdString(symbol->name)].insert(frame->index);
            }
        for (const fla::Timeline* scene : document.timelines)
            for (const fla::Layer* layer : scene ? scene->layers : std::vector<fla::Layer*>())
                for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
                    if (frame && frame->actionScript && !frame->actionScript->code.empty())
                        _rootScripts = true;
    }

    QJsonObject build(const QString& name)
    {
        const QJsonObject root = _content["root"].toObject();
        const int frames = std::max(1, root["n"].toInt(1));
        for (int frame = 0; frame < frames; ++frame)
        {
            std::vector<Leaf> leaves;
            walk(root, frame, Leaf(), frame, leaves, 0);
            std::vector<QString> drawn;
            for (const Leaf& leaf : leaves)
                place(leaf, frame, drawn);
            // Each layer in its place among those already found.
            QString previous;
            for (const QString& key : drawn)
            {
                if (!_where.contains(key))
                {
                    const auto at = previous.isEmpty() ? _order.begin() : std::next(_where.value(previous));
                    _where.insert(key, _order.insert(at, key));
                }
                previous = key;
            }
        }

        QJsonArray layers;
        int index = 1;
        // Lottie lists the top layer first.
        for (auto it = _order.rbegin(); it != _order.rend(); ++it)
        {
            const Track& track = _tracks.at(*it);
            if (track.frames.empty())
                continue;
            const int first = track.frames.begin()->first;
            const int last = track.frames.rbegin()->first;
            QJsonObject layer = layerFor(track, first, last);
            if (layer.isEmpty())
                continue;
            layer["nm"] = *it;
            if (!track.matte.isEmpty())
            {
                QJsonObject matte = matteFor(track.matte, first, last);
                if (!matte.isEmpty())
                {
                    matte["ind"] = index++;
                    matte["td"] = 1;
                    matte["nm"] = "Mask of " + *it;
                    layers.append(matte);
                    layer["tt"] = 1;
                }
            }
            layer["ind"] = index++;
            layers.append(layer);
        }

        QJsonArray assets;
        for (int i = 0; i < _images.size(); ++i)
            if (_usedImages.count(i))
            {
                QJsonObject asset = imageAsset(i);
                if (!asset.isEmpty())
                    assets.append(asset);
            }

        if (_rootScripts)
            note("Frame scripts are not run: the animation plays every frame of the timeline, and a movie clip "
                 "holds where a stop() on its own timeline stops it.");
        const double frameRate = _content["frameRate"].toDouble(24.0);
        return QJsonObject{{"v", "5.7.4"}, {"fr", frameRate}, {"ip", 0}, {"op", frames},
            {"w", _content["width"].toInt(550)}, {"h", _content["height"].toInt(400)}, {"nm", name}, {"ddd", 0},
            {"assets", assets}, {"layers", layers}, {"markers", markers(root)}};
    }

private:
    void note(const QString& text)
    {
        if (!_notes.contains(text))
            _notes << text;
    }

    QJsonArray markers(const QJsonObject& root)
    {
        QJsonArray out;
        for (const QJsonValue& label : root["labels"].toArray())
        {
            const QJsonArray pair = label.toArray();
            if (pair.size() == 2)
                out.append(QJsonObject{{"tm", pair[0].toInt()}, {"cm", pair[1].toString()}, {"dr", 0}});
        }
        return out;
    }

    /// The frame (from 0) a symbol's timeline of \a frames frames shows,
    /// placed as \a entry with key \a key, on the animation's frame \a now.
    int symbolFrame(const QJsonObject& entry, const QJsonObject& symbol, const QString& key, int frames, int now)
    {
        const QString kind = entry.contains("it") ? entry["it"].toString() : symbol["type"].toString();
        if (frames <= 1)
            return 0;
        if (kind == "graphic")
            return std::clamp(entry["gf"].toInt(0), 0, frames - 1);
        if (kind == "button")
            return 0;
        // A movie clip plays on from where it came on, holding at a stop().
        auto known = _clips.find(key);
        if (known == _clips.end())
            known = _clips.emplace(key, std::make_pair(-2, 0)).first;
        auto& [seen, at] = known->second;
        if (seen != now - 1 && seen != now)
            at = 0;
        else if (seen == now - 1)
        {
            const auto stops = _stops.find(symbol["name"].toString());
            const bool stopped = stops != _stops.end() && stops->second.count(at);
            if (!stopped)
                at = (at + 1) % frames;
        }
        seen = now;
        return at;
    }

    /// What \a timeline shows on its \a frame, opened up into leaves under
    /// \a outer (what the symbols it is in give it: place, alpha, color,
    /// mask, blend and filters; its key the path to it).
    void walk(const QJsonObject& timeline, int frame, const Leaf& outer, int now, std::vector<Leaf>& out, int depth)
    {
        if (depth > 32)
            return;
        const QJsonArray map = timeline["map"].toArray();
        const QJsonArray states = timeline["states"].toArray();
        if (map.isEmpty())
            return;
        const int state = map[std::clamp(frame, 0, static_cast<int>(map.size()) - 1)].toInt();
        if (state < 0 || state >= states.size())
            return;
        for (const QJsonValue& value : states[state].toArray())
        {
            const QJsonObject entry = value.toObject();
            if (entry.contains("v") && entry["v"].toInt(1) == 0)
                continue;
            Leaf inner = outer;
            inner.key = outer.key + "/" + entry["k"].toString();
            inner.matrix = matrixOf(entry["m"]) * outer.matrix;
            if (entry.contains("ct"))
            {
                const QJsonArray ct = entry["ct"].toArray();
                if (ct.size() == 8)
                {
                    inner.alpha *= std::clamp(ct[3].toDouble(1.0) + ct[7].toDouble(0.0) / 255.0, 0.0, 1.0);
                    ColorEffect own;
                    for (int i = 0; i < 3; ++i)
                    {
                        own.multiply[i] = ct[i].toDouble(1.0);
                        own.offset[i] = ct[4 + i].toDouble(0.0);
                    }
                    inner.color = own.then(outer.color);
                }
            }
            if (entry.contains("f"))
                for (const QJsonValue& filter : entry["f"].toArray())
                    inner.filters.append(filter);
            if (entry.contains("bm"))
                inner.blend = entry["bm"].toString();
            if (entry.contains("mk"))
                inner.maskOf = outer.key + "#" + QString::number(entry["mk"].toInt());
            if (entry.contains("mb"))
                inner.matte = outer.key + "#" + QString::number(entry["mb"].toInt());
            const QString type = entry["t"].toString();
            const int id = entry["id"].toInt(-1);
            if (type == "s")
            {
                if (id < 0 || id >= _symbols.size())
                    continue;
                const QJsonObject symbol = _symbols[id].toObject();
                const QJsonObject timelineOf = symbol["timeline"].toObject();
                const int frames = std::max(1, timelineOf["n"].toInt(1));
                const int shown = symbolFrame(entry, symbol, inner.key, frames, now);
                walk(timelineOf, shown, inner, now, out, depth + 1);
            }
            else if (type == "a" || type == "b" || type == "x")
            {
                inner.kind = type[0];
                inner.id = id;
                out.push_back(inner);
            }
        }
    }

    /// Records \a leaf on \a frame: a mask's part in its mask, else each of
    /// its parts as a layer, their keys added to \a drawn bottom first.
    void place(const Leaf& leaf, int frame, std::vector<QString>& drawn)
    {
        const QString base = leaf.key + "@" + leaf.kind + QString::number(leaf.id);
        if (!leaf.maskOf.isEmpty())
        {
            if (leaf.kind != 'a' && leaf.kind != 'x')
                return;
            auto& mask = _masks[leaf.maskOf];
            if (!mask.tracks.count(base))
            {
                mask.order.push_back(base);
                mask.tracks[base].kind = leaf.kind;
                mask.tracks[base].id = leaf.id;
            }
            mask.tracks[base].frames[frame] = {leaf.matrix, 1.0};
            return;
        }
        const std::vector<Part>& parts = partsOf(leaf.kind, leaf.id);
        for (size_t i = 0; i < parts.size(); ++i)
        {
            const QString key = parts.size() == 1 ? base : base + "#" + QString::number(i);
            auto found = _tracks.find(key);
            if (found == _tracks.end())
            {
                Track track;
                track.kind = leaf.kind;
                track.id = leaf.id;
                track.part = static_cast<int>(i);
                track.matte = leaf.matte;
                track.blend = leaf.blend;
                track.filters = leaf.filters;
                found = _tracks.emplace(key, track).first;
            }
            found->second.frames[frame] = {leaf.matrix, leaf.alpha, leaf.color};
            drawn.push_back(key);
        }
    }

    // -- What is drawn -------------------------------------------------------

    /// A fill or a stroke for a content Fill; empty when Lottie has none
    /// (a bitmap fill).
    QJsonObject paint(const QJsonObject& fill, const QJsonObject* line, bool evenOdd, double opacity)
    {
        const bool stroke = line != nullptr;
        QJsonObject out;
        if (fill.contains("g"))
        {
            const QTransform m = matrixOf(fill["m"]);
            const bool linear = fill["g"].toString() == "l";
            const QJsonArray stops = fill["s"].toArray();
            QJsonArray values;
            for (const QJsonValue& stop : stops)
            {
                const QJsonArray s = stop.toArray();
                const QJsonArray c = color(s[1].toInt());
                values.append(rounded(s[0].toDouble(), 10000.0));
                values.append(c[0]);
                values.append(c[1]);
                values.append(c[2]);
            }
            for (const QJsonValue& stop : stops)
            {
                const QJsonArray s = stop.toArray();
                values.append(rounded(s[0].toDouble(), 10000.0));
                values.append(rounded(s[2].toDouble(1.0), 10000.0));
            }
            out = QJsonObject{{"ty", stroke ? "gs" : "gf"}, {"t", linear ? 1 : 2},
                {"g", QJsonObject{{"p", stops.size()}, {"k", still(values)}}}, {"o", still(rounded(opacity * 100.0, 100.0))}};
            if (linear)
            {
                out["s"] = still(point(m.map(QPointF(-819.2, 0.0))));
                out["e"] = still(point(m.map(QPointF(819.2, 0.0))));
            }
            else
            {
                out["s"] = still(point(m.map(QPointF(0.0, 0.0))));
                out["e"] = still(point(m.map(QPointF(819.2, 0.0))));
                out["h"] = still(rounded(fill["fp"].toDouble(0.0) * 100.0, 100.0));
                out["a"] = still(0);
                if (std::abs(std::hypot(m.m11(), m.m12()) - std::hypot(m.m21(), m.m22())) > 1e-3 * std::hypot(m.m11(), m.m12()))
                    note("Radial gradients squeezed into ovals are drawn as circles.");
            }
            if (fill.contains("sp"))
                note("Gradients that repeat or reflect past their ends are drawn padded with their end colors.");
        }
        else if (fill.contains("c"))
        {
            out = QJsonObject{{"ty", stroke ? "st" : "fl"}, {"c", still(color(fill["c"].toInt()))},
                {"o", still(rounded(fill["a"].toDouble(1.0) * opacity * 100.0, 100.0))}};
        }
        else
            return QJsonObject();
        if (stroke)
        {
            const QString cap = (*line)["cap"].toString("round");
            const QString join = (*line)["join"].toString("round");
            out["w"] = still(rounded((*line)["w"].toDouble(1.0)));
            out["lc"] = cap == "none" ? 1 : cap == "square" ? 3 : 2;
            out["lj"] = join == "miter" ? 1 : join == "bevel" ? 3 : 2;
            if (join == "miter")
                out["ml"] = rounded((*line)["ml"].toDouble(3.0));
            // Its width kept however it is scaled: worked out for each layer
            // it is drawn on (adjusted).
            if ((*line)["sm"].toString() == "none")
                out["_ns"] = 1;
        }
        else
            out["r"] = evenOdd ? 2 : 1;
        return out;
    }

    /// A group of one op's paths and its paint.
    QJsonObject group(const QString& data, const QJsonObject& paintOf)
    {
        QJsonArray items = pathShapes(data);
        if (items.isEmpty())
            return QJsonObject();
        items.append(paintOf);
        items.append(identityTransform());
        return QJsonObject{{"ty", "gr"}, {"it", items}, {"np", items.size() - 1}};
    }

    /// The parts a shape, bitmap or text field draws as, bottom first.
    const std::vector<Part>& partsOf(QChar kind, int id)
    {
        const QString key = QString(kind) + QString::number(id);
        auto found = _parts.find(key);
        if (found != _parts.end())
            return found->second;
        std::vector<Part> parts;
        if (kind == 'b')
        {
            Part picture;
            picture.image = id;
            parts.push_back(picture);
        }
        else if (kind == 'x')
        {
            Part text;
            text.groups = textGroups(id);
            if (!text.groups.isEmpty())
                parts.push_back(text);
            note("Dynamic and input text is drawn as the outlines of the text it starts with.");
        }
        else if (id >= 0 && id < _art.size())
        {
            Part run;
            const auto flush = [&]() {
                if (!run.groups.isEmpty())
                    parts.push_back(run);
                run = Part();
            };
            for (const QJsonValue& value : _art[id].toObject()["o"].toArray())
            {
                const QJsonObject op = value.toObject();
                const double opacity = op["o"].toDouble(1.0);
                if (op.contains("bm"))
                {
                    bool known = false;
                    blendNumber(op["bm"].toString(), known);
                    if (!known || op["bm"].toString() != "normal")
                        note("Blend modes inside shapes are drawn as normal.");
                }
                if (op.contains("i") || (op.contains("f") && op["f"].toObject().contains("b")))
                {
                    // A picture, or a path filled with one: its own layer,
                    // clipped to the path.
                    flush();
                    Part picture;
                    if (op.contains("i"))
                    {
                        picture.image = op["i"].toInt();
                        picture.imageMatrix = matrixOf(op["m"]);
                        picture.clip = op["clip"].toString();
                    }
                    else
                    {
                        const QJsonObject fill = op["f"].toObject();
                        picture.image = fill["b"].toInt();
                        picture.imageMatrix = matrixOf(fill["m"]);
                        picture.clip = op["p"].toString();
                        if (fill["r"].toInt() == 1)
                            note("Bitmap fills are drawn once, not tiled across their shapes.");
                    }
                    picture.opacity = opacity;
                    parts.push_back(picture);
                    continue;
                }
                QJsonObject made;
                if (op.contains("s"))
                {
                    const QJsonObject line = op["s"].toObject();
                    const QJsonObject p = paint(line["f"].toObject(), &line, false, opacity);
                    if (!p.isEmpty())
                        made = group(op["p"].toString(), p);
                }
                else if (op.contains("f"))
                {
                    const QJsonObject p = paint(op["f"].toObject(), nullptr, op["e"].toInt() == 1, opacity);
                    if (!p.isEmpty())
                        made = group(op["p"].toString(), p);
                }
                if (made.isEmpty())
                    continue;
                if (op.contains("clip"))
                {
                    flush();
                    Part clipped;
                    clipped.groups.append(made);
                    clipped.clip = op["clip"].toString();
                    parts.push_back(clipped);
                    continue;
                }
                run.groups.append(made);
            }
            flush();
        }
        return _parts.emplace(key, parts).first->second;
    }

    /// A text field's text as the outlines of its glyphs, a group a run.
    QJsonArray textGroups(int id)
    {
        QJsonArray groups;
        if (id < 0 || id >= _texts.size())
            return groups;
        const QJsonObject field = _texts[id].toObject();
        const double left = field["x"].toDouble() + 2.0;
        const double width = std::max(0.0, field["w"].toDouble() - 4.0);
        double y = field["y"].toDouble() + 2.0;
        struct Piece
        {
            QString text;
            QFont font;
            int color = 0;
        };
        std::vector<std::vector<Piece>> lines(1);
        std::vector<QString> aligns(1, field["runs"].toArray().isEmpty() ? QString() :
            field["runs"].toArray().first().toObject()["a"].toString());
        std::vector<double> leading(1, 0.0);
        for (const QJsonValue& value : field["runs"].toArray())
        {
            const QJsonObject run = value.toObject();
            QFont font(run["f"].toString());
            font.setPixelSize(std::max(1, static_cast<int>(std::lround(run["s"].toDouble(12.0)))));
            font.setBold(run["b"].toInt() == 1);
            font.setItalic(run["i"].toInt() == 1);
            if (run.contains("ls"))
                font.setLetterSpacing(QFont::AbsoluteSpacing, run["ls"].toDouble());
            const QString text = run["t"].toString();
            QString piece;
            for (int i = 0; i <= text.size(); ++i)
            {
                const bool end = i == text.size();
                const QChar c = end ? QChar() : text[i];
                if (end || c == '\r' || c == '\n')
                {
                    if (!piece.isEmpty())
                        lines.back().push_back({piece, font, run["c"].toInt()});
                    piece.clear();
                    leading.back() = std::max(leading.back(), run["ld"].toDouble(0.0));
                    if (!end)
                    {
                        if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                            ++i;
                        lines.emplace_back();
                        aligns.push_back(run["a"].toString());
                        leading.push_back(0.0);
                    }
                }
                else
                    piece += c;
            }
        }
        for (size_t l = 0; l < lines.size(); ++l)
        {
            double ascent = 0.0, descent = 0.0, lineWidth = 0.0;
            for (const Piece& piece : lines[l])
            {
                const QFontMetricsF metrics(piece.font);
                ascent = std::max(ascent, metrics.ascent());
                descent = std::max(descent, metrics.descent());
                lineWidth += metrics.horizontalAdvance(piece.text);
            }
            if (lines[l].empty())
            {
                const QFontMetricsF metrics(QFont("Arial"));
                ascent = metrics.ascent();
                descent = metrics.descent();
            }
            double x = left;
            if (aligns[l] == "center")
                x += (width - lineWidth) / 2.0;
            else if (aligns[l] == "right")
                x += width - lineWidth;
            const double baseline = y + ascent;
            for (const Piece& piece : lines[l])
            {
                QPainterPath glyphs;
                glyphs.addText(QPointF(x, baseline), piece.font, piece.text);
                x += QFontMetricsF(piece.font).horizontalAdvance(piece.text);
                // The glyphs' curves as they are, not flattened.
                QString data;
                for (int i = 0; i < glyphs.elementCount(); ++i)
                {
                    const QPainterPath::Element& e = glyphs.elementAt(i);
                    if (e.type == QPainterPath::MoveToElement)
                        data += QString("Z M%1 %2").arg(e.x).arg(e.y);
                    else if (e.type == QPainterPath::LineToElement)
                        data += QString("L%1 %2").arg(e.x).arg(e.y);
                    else if (e.type == QPainterPath::CurveToElement && i + 2 < glyphs.elementCount())
                    {
                        const QPainterPath::Element& c2 = glyphs.elementAt(i + 1);
                        const QPainterPath::Element& to = glyphs.elementAt(i + 2);
                        data += QString("C%1 %2 %3 %4 %5 %6").arg(e.x).arg(e.y).arg(c2.x).arg(c2.y).arg(to.x).arg(to.y);
                        i += 2;
                    }
                }
                data += "Z";
                const QJsonObject made = group(data, QJsonObject{{"ty", "fl"}, {"c", still(color(piece.color))},
                                                         {"o", still(100)}, {"r", 1}});
                if (!made.isEmpty())
                    groups.append(made);
            }
            y = baseline + descent + leading[l];
        }
        return groups;
    }

    // -- Layers ----------------------------------------------------------------

    /// A group of a shape as \a track draws it on frames \a first to
    /// \a last: its fill, stroke and gradient colors through the track's
    /// color effect, and the widths of strokes that keep their width
    /// whatever their scale, frame by frame where they change.
    QJsonObject adjusted(QJsonObject group, const Track& track, int first, int last, bool colored)
    {
        QJsonArray items = group["it"].toArray();
        for (int i = 0; i < items.size(); ++i)
        {
            QJsonObject item = items[i].toObject();
            const QString type = item["ty"].toString();
            if (type == "gr")
                item = adjusted(item, track, first, last, colored);
            // The track's frame on each frame: the last one it was there.
            const auto each = [&](const std::function<void(int, const TrackFrame&)>& work) {
                const TrackFrame* at = nullptr;
                for (int frame = first; frame <= last; ++frame)
                {
                    const auto found = track.frames.find(frame);
                    if (found != track.frames.end())
                        at = &found->second;
                    if (at)
                        work(frame, *at);
                }
            };
            if (colored && (type == "fl" || type == "st"))
            {
                const QJsonArray c = item["c"].toObject()["k"].toArray();
                Held held(c);
                each([&](int frame, const TrackFrame& at) {
                    held.set(frame, QJsonArray{rounded(at.color.apply(0, c[0].toDouble()), 10000.0),
                                        rounded(at.color.apply(1, c[1].toDouble()), 10000.0),
                                        rounded(at.color.apply(2, c[2].toDouble()), 10000.0), 1});
                });
                item["c"] = held.json(false);
            }
            else if (colored && (type == "gf" || type == "gs"))
            {
                QJsonObject gradient = item["g"].toObject();
                const int stops = gradient["p"].toInt();
                const QJsonArray values = gradient["k"].toObject()["k"].toArray();
                Held held(values);
                each([&](int frame, const TrackFrame& at) {
                    QJsonArray changed = values;
                    for (int s = 0; s < stops && s * 4 + 3 < values.size(); ++s)
                        for (int channel = 0; channel < 3; ++channel)
                            changed[s * 4 + 1 + channel] =
                                rounded(at.color.apply(channel, values[s * 4 + 1 + channel].toDouble()), 10000.0);
                    held.set(frame, changed);
                });
                gradient["k"] = held.json(false);
                item["g"] = gradient;
            }
            if (item.contains("_ns"))
            {
                item.remove("_ns");
                const double width = item["w"].toObject()["k"].toDouble(1.0);
                Held held(QJsonArray{width});
                each([&](int frame, const TrackFrame& at) {
                    const double scale = std::sqrt(std::abs(at.matrix.determinant()));
                    held.set(frame, QJsonArray{rounded(scale > 1e-9 ? width / scale : width)});
                });
                item["w"] = held.json(true);
            }
            items[i] = item;
        }
        group["it"] = items;
        return group;
    }

    /// Lottie's effects for a track's filters: blur as Gaussian Blur, drop
    /// shadow as Drop Shadow; the rest left out, with a note.
    QJsonArray effectsOf(const QJsonArray& filters)
    {
        QJsonArray effects;
        const auto slider = [](const char* name, int index, double value) {
            return QJsonObject{{"ty", 0}, {"nm", name}, {"ix", index}, {"v", still(rounded(value))}};
        };
        // How far a filter's box blur spreads, as the deviation of the bell
        // curve it comes to after its passes.
        const auto deviation = [](const QJsonObject& filter) {
            const double size = std::max(filter["bx"].toDouble(4.0), filter["by"].toDouble(4.0));
            return size * std::sqrt(std::max(1, filter["q"].toInt(1)) / 12.0);
        };
        for (const QJsonValue& value : filters)
        {
            const QJsonObject filter = value.toObject();
            const QString type = filter["t"].toString();
            if (type == "blur")
            {
                // Lottie's players take a deviation of 0.3 a unit of
                // blurriness.
                effects.append(QJsonObject{{"ty", 29}, {"nm", "Gaussian Blur"}, {"mn", "ADBE Gaussian Blur 2"},
                    {"ix", effects.size() + 1}, {"en", 1}, {"np", 5},
                    {"ef", QJsonArray{slider("Blurriness", 1, deviation(filter) / 0.3),
                               QJsonObject{{"ty", 7}, {"nm", "Blur Dimensions"}, {"ix", 2}, {"v", still(1)}},
                               QJsonObject{{"ty", 7}, {"nm", "Repeat Edge Pixels"}, {"ix", 3}, {"v", still(0)}}}}});
                if (std::abs(filter["bx"].toDouble() - filter["by"].toDouble()) > 0.5)
                    note("Blurs with different widths and heights are blurred evenly.");
            }
            else if (type == "shadow")
            {
                if (filter["in"].toInt() || filter["ko"].toInt() || filter["ho"].toInt())
                    note("Inner, knockout and hide-object drop shadows are drawn as plain drop shadows.");
                QJsonArray shadowColor = color(filter["c"].toInt());
                // A direction measured from straight up, clockwise; the
                // filter's angle is from the right.
                effects.append(QJsonObject{{"ty", 25}, {"nm", "Drop Shadow"}, {"mn", "ADBE Drop Shadow"},
                    {"ix", effects.size() + 1}, {"en", 1}, {"np", 8},
                    {"ef", QJsonArray{QJsonObject{{"ty", 2}, {"nm", "Shadow Color"}, {"ix", 1}, {"v", still(shadowColor)}},
                               slider("Opacity", 2, std::clamp(filter["a"].toDouble(1.0), 0.0, 1.0) * 255.0),
                               QJsonObject{{"ty", 1}, {"nm", "Direction"}, {"ix", 3},
                                   {"v", still(rounded(filter["an"].toDouble(45.0) + 90.0))}},
                               slider("Distance", 4, filter["d"].toDouble(4.0)),
                               slider("Softness", 5, deviation(filter) * 4.0)}}});
            }
            else
                note("Glow, bevel, gradient glow, gradient bevel and color matrix filters are left out.");
        }
        return effects;
    }

    QJsonObject layerFor(const Track& track, int first, int last)
    {
        const std::vector<Part>& parts = partsOf(track.kind, track.id);
        if (track.part < 0 || track.part >= static_cast<int>(parts.size()))
            return QJsonObject();
        const Part& part = parts[static_cast<size_t>(track.part)];
        const bool picture = part.image >= 0;
        if (picture && (part.image >= _images.size() || _images[part.image].toObject().contains("in")))
            return QJsonObject();
        // A picture's own place goes under the object's.
        const QTransform own = picture ? part.imageMatrix : QTransform();
        HeldTransform transform;
        for (int frame = first; frame <= last; ++frame)
        {
            const auto found = track.frames.find(frame);
            if (found == track.frames.end())
                transform.hide(frame);
            else
                transform.set(frame, own * found->second.matrix, found->second.alpha * part.opacity);
        }
        QJsonObject layer{{"ddd", 0}, {"sr", 1}, {"ks", transform.json(false)}, {"ao", 0}, {"ip", first},
            {"op", last + 1}, {"st", 0}};
        bool known = false;
        layer["bm"] = blendNumber(track.blend, known);
        if (!known)
            note("Blend modes Lottie has no match for (add, subtract, invert, alpha, erase) are drawn as normal.");
        bool colored = false;
        for (const auto& [frame, at] : track.frames)
            colored = colored || !at.color.none();
        if (picture)
        {
            layer["ty"] = 2;
            layer["refId"] = QString("image_%1").arg(part.image);
            _usedImages.insert(part.image);
            if (colored)
                note("Color effects on bitmaps (tint, brightness, advanced) are left out.");
        }
        else
        {
            QJsonArray shapes;
            // Lottie draws the first shape on top.
            for (int i = static_cast<int>(part.groups.size()) - 1; i >= 0; --i)
                shapes.append(adjusted(part.groups[i].toObject(), track, first, last, colored));
            layer["ty"] = 4;
            layer["shapes"] = shapes;
        }
        const QJsonArray effects = effectsOf(track.filters);
        if (!effects.isEmpty())
            layer["ef"] = effects;
        if (!part.clip.isEmpty())
        {
            // The clip is in the shape's space; a picture's layer is in the
            // picture's.
            const QTransform toLayer = own.inverted();
            QJsonArray masks;
            for (const Outline& outline : outlinesOf(part.clip))
                masks.append(QJsonObject{{"inv", false}, {"mode", "a"}, {"pt", still(shapeData(outline, toLayer))},
                    {"o", still(100)}, {"x", still(0)}, {"nm", "Clip"}});
            if (!masks.isEmpty())
            {
                layer["masksProperties"] = masks;
                layer["hasMask"] = true;
            }
        }
        return layer;
    }

    /// A track matte for frames \a first to \a last: the mask's shapes,
    /// each where it is on each frame.
    QJsonObject matteFor(const QString& mask, int first, int last)
    {
        const auto found = _masks.find(mask);
        if (found == _masks.end())
            return QJsonObject();
        QJsonArray shapes;
        for (auto it = found->second.order.rbegin(); it != found->second.order.rend(); ++it)
        {
            const Track& track = found->second.tracks.at(*it);
            QJsonArray items;
            for (const Part& part : partsOf(track.kind, track.id))
                for (const QJsonValue& value : part.groups)
                {
                    // Only where it covers counts: a solid fill.
                    QJsonArray paths;
                    for (const QJsonValue& item : value.toObject()["it"].toArray())
                        if (item.toObject()["ty"].toString() == "sh")
                            paths.append(item);
                    for (const QJsonValue& path : paths)
                        items.append(path);
                }
            if (items.isEmpty())
                continue;
            items.append(QJsonObject{{"ty", "fl"}, {"c", still(QJsonArray{1, 1, 1, 1})}, {"o", still(100)}, {"r", 1}});
            HeldTransform transform;
            for (int frame = first; frame <= last; ++frame)
            {
                const auto at = track.frames.find(frame);
                if (at == track.frames.end())
                    transform.hide(frame);
                else
                    transform.set(frame, at->second.matrix, 1.0);
            }
            items.append(transform.json(true));
            shapes.append(QJsonObject{{"ty", "gr"}, {"it", items}, {"np", items.size() - 1}});
        }
        if (shapes.isEmpty())
            return QJsonObject();
        return QJsonObject{{"ddd", 0}, {"ty", 4}, {"sr", 1}, {"ks", HeldTransform().json(false)}, {"ao", 0},
            {"ip", first}, {"op", last + 1}, {"st", 0}, {"bm", 0}, {"shapes", shapes}};
    }

    QJsonObject imageAsset(int index)
    {
        const QJsonObject image = _images[index].toObject();
        const QString source = image["src"].toString();
        if (source.isEmpty())
            return QJsonObject();
        QFile file(QDir(_folder).filePath(source));
        if (!file.open(QIODevice::ReadOnly))
            return QJsonObject();
        const QByteArray bytes = file.readAll();
        const QString suffix = QFileInfo(source).suffix().toLower();
        const QString type = suffix == "jpg" || suffix == "jpeg" ? "image/jpeg" : suffix == "webp" ? "image/webp" : "image/png";
        return QJsonObject{{"id", QString("image_%1").arg(index)}, {"w", image["w"].toInt()}, {"h", image["h"].toInt()},
            {"u", ""}, {"p", "data:" + type + ";base64," + QString::fromLatin1(bytes.toBase64())}, {"e", 1}};
    }

    struct Mask
    {
        std::vector<QString> order;
        std::map<QString, Track> tracks;
    };

    QJsonObject _content;
    QString _folder;
    QStringList& _notes;
    QJsonArray _art, _texts, _symbols, _images;
    std::map<QString, std::set<int>> _stops;
    bool _rootScripts = false;
    std::map<QString, std::pair<int, int>> _clips;
    std::map<QString, std::vector<Part>> _parts;
    std::map<QString, Track> _tracks;
    std::map<QString, Mask> _masks;
    std::list<QString> _order;
    QHash<QString, std::list<QString>::iterator> _where;
    std::set<int> _usedImages;
};

} // namespace

bool decompose(double a, double b, double c, double d, double& rotation, double& scaleX, double& scaleY, double& skew)
{
    const double sx = std::hypot(a, b);
    const double determinant = a * d - b * c;
    if (sx < 1e-12 || std::abs(determinant) < 1e-12)
        return false;
    // M = R(rotation) * Shear(k) * Scale(sx, sy), the shear along x.
    const double sy = determinant / sx;
    const double k = (a * c + b * d) / determinant;
    rotation = std::atan2(b, a) * 180.0 / kPi;
    scaleX = sx * 100.0;
    scaleY = sy * 100.0;
    // Lottie skews x by tan(-skew) of y.
    skew = -std::atan(k) * 180.0 / kPi;
    return true;
}

QByteArray animation(PhoenixView& view, const fla::Document& document, const QString& name, QStringList& notes,
    QString& error)
{
    QTemporaryDir folder;
    if (!folder.isValid())
    {
        error = "There is no room for the pictures while the animation is made.";
        return QByteArray();
    }
    QStringList found;
    const std::string content = Html5Publish::exportContent(view, document, folder.path(), found);
    // Sounds are not written for Lottie at all: one note for them.
    bool sounds = false;
    for (const QString& note : found)
    {
        if (note.startsWith("The sound ") && note.endsWith(" could not be written."))
            sounds = true;
        else if (!notes.contains(note))
            notes << note;
    }
    if (sounds)
        notes << "Sounds are left out: Lottie's players do not play them.";
    QJsonParseError parsed;
    const QJsonDocument json = QJsonDocument::fromJson(QByteArray::fromStdString(content), &parsed);
    if (parsed.error != QJsonParseError::NoError || !json.isObject())
    {
        error = "The movie could not be read for exporting: " + parsed.errorString() + ".";
        return QByteArray();
    }
    Exporter exporter(json.object(), document, folder.path(), notes);
    const QJsonObject out = exporter.build(name);
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

bool write(PhoenixView& view, const fla::Document& document, const QString& path, QStringList& notes, QString& error)
{
    const QByteArray json = animation(view, document, QFileInfo(path).completeBaseName(), notes, error);
    if (json.isEmpty())
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit())
    {
        error = QString("Could not write %1.").arg(QDir::toNativeSeparators(path));
        return false;
    }
    return true;
}

} // namespace LottieExport
