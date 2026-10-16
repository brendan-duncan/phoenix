#include "behaviors_panel.h"

#include "behavior_library.h"
#include "hot_text.h"
#include "text_layout.h"
#include "theme.h"

#include "../data/symbol_instance.h"
#include "../edit/command_stack.h"
#include "../edit/property_command.h"
#include "../edit/shape_operators.h"
#include "../edit/text_animators.h"
#include "../edit/adjustments.h"
#include "../data/layer.h"
#include "../data/static_text.h"
#include "../data/shape.h"

#include <QCache>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPainter>
#include <QPainterPath>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QUrl>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace {

using Attributes = std::vector<std::pair<std::string, std::string>>;

/// Folds a run of changes to the same setting into one step.
constexpr int kMergeSetting = 7301;

std::string numberText(double value)
{
    return QString::number(value, 'g', 10).toStdString();
}

int newSeed()
{
    return static_cast<int>(QRandomGenerator::global()->bounded(1, 10000));
}

/// One thing the presets browser offers: a behavior or letter animator on
/// its own, or several ready made together.
struct BrowsedPreset
{
    QString name;
    QString description;
    std::vector<fla::behaviors::Applied> list;
};

/// Behaviors ready made together, with the settings they need.
std::vector<BrowsedPreset> behaviorPresets()
{
    fla::behaviors::Engine& engine = BehaviorLibrary::instance().engine();
    const auto make = [&engine](const char* id, std::map<std::string, std::string> settings) {
        fla::behaviors::Applied applied = engine.defaults(id, 1);
        for (const auto& [name, value] : settings)
            applied.params[name] = value;
        return applied;
    };
    std::vector<BrowsedPreset> list = {
        {"Float", "Drifts gently up and down and about, as if on water.",
            {make("oscillate", {{"motion", "Up and down"}, {"amount", "8"}, {"period", "2.5"}}),
                make("wiggle", {{"frequency", "0.6"}, {"position", "4"}, {"rotation", "4"}, {"scale", "0"}})}},
        {"Heartbeat", "Beats bigger and back, again and again.", {make("pulse", {{"rate", "1.2"}, {"amount", "25"}})}},
        {"Swing", "Swings to and fro like a sign on a hook.", {make("oscillate", {{"motion", "Turn"}, {"amount", "20"}, {"period", "1.5"}})}},
        {"Bob and Turn", "Bobs up and down, tipping as it goes.",
            {make("oscillate", {{"motion", "Up and down"}, {"amount", "12"}, {"period", "1"}}),
                make("oscillate", {{"motion", "Turn"}, {"amount", "10"}, {"period", "1"}, {"phase", "90"}})}},
        {"Spin Forever", "Turns round and round at a steady speed.", {make("spin", {{"speed", "180"}})}},
        {"Flicker", "Blinks on and off at random, like a failing light.",
            {make("blink", {{"rate", "8"}, {"shown", "70"}, {"random", "true"}})}},
        {"Ring of Copies", "Copies of it in a ring, turning together.",
            {make("circle", {{"count", "10"}, {"radius", "90"}}), make("spin", {{"speed", "45"}})}},
        {"Fountain", "Copies of it spray up and fall, fading.", {make("particles", {})}},
        {"Shaky", "Shakes about nervously.", {make("wiggle", {{"frequency", "8"}, {"position", "5"}, {"rotation", "6"}})}},
    };
    return list;
}

/// What a preview shows its preset on.
enum class Sample
{
    /// A star, moved by behaviors.
    Star,
    /// A word, its letters animated.
    Word,
    /// Shapes, their lines and fills changed by shape operators.
    Shapes,
    /// A picture, changed by adjustment effects.
    Picture
};

constexpr int kPreviewFrames = 72;

/// \a list as a preview plays it: a transition keyed from start to end and
/// Trim Paths drawing on over the loop, so they show moving; the rest as
/// set.
std::vector<fla::behaviors::Applied> demonstrated(std::vector<fla::behaviors::Applied> list)
{
    for (fla::behaviors::Applied& applied : list)
    {
        const char* keyed = applied.params.count("completion") ? "completion" : applied.type == "trim" ? "end" : nullptr;
        if (!keyed || applied.keyed(keyed))
            continue;
        applied.params.erase(keyed);
        applied.setKey(keyed, 6, "0");
        applied.setKey(keyed, kPreviewFrames - 12, "100");
    }
    return list;
}

/// A line of the sample as shape operators take it.
fla::shapeops::Line lineOf(const QPolygonF& polygon, bool closed)
{
    fla::shapeops::Line line;
    for (const QPointF& point : polygon)
        line.points.push_back(fla::Point(point.x(), point.y()));
    if (closed && line.points.size() > 2)
        line.points.pop_back();
    line.closed = closed;
    return line;
}

QPainterPath pathOf(const std::vector<fla::shapeops::Line>& lines)
{
    QPainterPath path;
    for (const fla::shapeops::Line& line : lines)
    {
        if (line.points.empty())
            continue;
        path.moveTo(line.points.front().x, line.points.front().y);
        for (size_t i = 1; i < line.points.size(); ++i)
            path.lineTo(line.points[i].x, line.points[i].y);
        if (line.closed)
            path.closeSubpath();
    }
    return path;
}

/// The sample shapes on \a frame of \a list, on a picture of \a size: a
/// circle and a square overlapping, filled (so Merge Paths shows),
/// and a wavy line under them, stroked (so Trim Paths and Taper show).
QImage operatedSample(const std::vector<fla::behaviors::Applied>& list, int frame, const QSize& size)
{
    QImage picture(size, QImage::Format_ARGB32_Premultiplied);
    picture.fill(QColor(0x20, 0x26, 0x30));
    QPainter painter(&picture);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(size.width() / 2.0, size.height() / 2.0);
    const double fit = std::min(size.width() / 330.0, size.height() / 210.0);
    painter.scale(fit, fit);

    QPainterPath circle;
    circle.addEllipse(QPointF(-35.0, -15.0), 50.0, 50.0);
    QPainterPath square;
    square.addRect(QRectF(-5.0, -55.0, 95.0, 85.0));
    std::vector<fla::shapeops::Line> outlines;
    for (const QPainterPath& each : {circle, square})
        for (const QPolygonF& polygon : each.toSubpathPolygons())
            outlines.push_back(lineOf(polygon, true));
    QPolygonF wave;
    for (double x = -120.0; x <= 120.0; x += 3.0)
        wave << QPointF(x, 68.0 + 10.0 * std::sin(x / 20.0));
    const std::vector<fla::shapeops::Line> strokes = {lineOf(wave, false)};

    const std::vector<fla::shapeops::Line> filled = fla::shapeops::apply(outlines, false, list, frame, 24.0);
    QPainterPath fills = pathOf(filled);
    fills.setFillRule(Qt::WindingFill);
    // Merge Paths: the first outline combined with each after it.
    const std::string merge = fla::shapeops::mergeOf(list, frame);
    if (!merge.empty() && filled.size() > 1)
    {
        QPainterPath combined;
        bool first = true;
        for (const fla::shapeops::Line& line : filled)
        {
            if (line.points.size() < 3)
                continue;
            QPainterPath piece = pathOf({line});
            piece.closeSubpath();
            if (first)
                combined = piece;
            else if (merge == "Subtract")
                combined = combined.subtracted(piece);
            else if (merge == "Intersect")
                combined = combined.intersected(piece);
            else if (merge == "Exclude")
                combined = combined.united(piece).subtracted(combined.intersected(piece));
            else
                combined = combined.united(piece);
            first = false;
        }
        fills = combined;
    }
    const QColor accent = theme::color("accent");
    painter.setPen(QPen(accent.darker(170), 2.0));
    painter.setBrush(accent);
    painter.drawPath(fills);

    const std::vector<fla::shapeops::Line> stroked = fla::shapeops::apply(strokes, true, list, frame, 24.0);
    const QColor ink(0xF0, 0xC0, 0x40);
    const fla::shapeops::Taper taper = fla::shapeops::taperOf(list, frame);
    if (taper.on)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        for (const fla::shapeops::Line& line : stroked)
        {
            QPolygonF outline;
            for (const fla::Point& point : fla::shapeops::taperedOutline(line, 8.0, taper, frame / 24.0))
                outline << QPointF(point.x, point.y);
            painter.drawPolygon(outline, Qt::WindingFill);
        }
    }
    else
    {
        painter.setPen(QPen(ink, 6.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(pathOf(stroked));
    }
    return picture;
}

/// The picture adjustment effects are seen on: a sky, a sun, hills, a star
/// and a word, \a size big.
const QImage& effectPicture(const QSize& size)
{
    static QImage picture;
    if (picture.size() == size)
        return picture;
    picture = QImage(size, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&picture);
    painter.setRenderHint(QPainter::Antialiasing);
    const double w = size.width(), h = size.height();
    QLinearGradient sky(0.0, 0.0, 0.0, h);
    sky.setColorAt(0.0, QColor(0x2E, 0x6F, 0xC8));
    sky.setColorAt(1.0, QColor(0xF2, 0xB8, 0x7A));
    painter.fillRect(QRectF(0.0, 0.0, w, h), sky);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xFF, 0xE0, 0x60));
    painter.drawEllipse(QPointF(w * 0.72, h * 0.32), h * 0.16, h * 0.16);
    QPainterPath hills;
    hills.moveTo(0.0, h);
    hills.lineTo(0.0, h * 0.7);
    hills.cubicTo(w * 0.25, h * 0.5, w * 0.45, h * 0.85, w * 0.7, h * 0.65);
    hills.cubicTo(w * 0.85, h * 0.55, w * 0.95, h * 0.62, w, h * 0.6);
    hills.lineTo(w, h);
    hills.closeSubpath();
    painter.setBrush(QColor(0x2F, 0x7A, 0x3A));
    painter.drawPath(hills);
    QPainterPath star;
    for (int i = 0; i < 10; ++i)
    {
        const double angle = -1.5707963267948966 + i * 0.6283185307179586;
        const double r = (i % 2 == 0 ? 0.2 : 0.085) * h;
        const QPointF point(w * 0.25 + std::cos(angle) * r, h * 0.36 + std::sin(angle) * r);
        if (i == 0)
            star.moveTo(point);
        else
            star.lineTo(point);
    }
    star.closeSubpath();
    painter.setBrush(QColor(0xE0, 0x40, 0x50));
    painter.setPen(QPen(Qt::white, std::max(1.0, h / 80.0)));
    painter.drawPath(star);
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(std::max(6, static_cast<int>(h * 0.14)));
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QRectF(0.0, h * 0.72, w, h * 0.26), Qt::AlignCenter, "Phoenix");
    return picture;
}

/// The picture with \a list on it, on \a frame.
QImage adjustedSample(const std::vector<fla::behaviors::Applied>& list, int frame, const QSize& size)
{
    QImage picture = effectPicture(size).copy();
    // Displacement Map pushes it by itself.
    const QImage& source = effectPicture(size);
    const std::vector<uint32_t> map(reinterpret_cast<const uint32_t*>(source.constBits()),
        reinterpret_cast<const uint32_t*>(source.constBits()) + static_cast<size_t>(size.width()) * size.height());
    const fla::compositing::effects::LayerPictures layers = [&map](const std::string&) { return &map; };
    // The picture stands for a stage 560 pixels across.
    fla::compositing::apply(reinterpret_cast<uint32_t*>(picture.bits()), size.width(), size.height(), list, frame,
        size.width() / 560.0, nullptr, 24.0, &layers);
    return picture;
}

/// \a list on its sample on \a frame, \a size big: shapes or a picture.
QImage sampleFrame(Sample sample, const std::vector<fla::behaviors::Applied>& list, int frame, const QSize& size)
{
    return sample == Sample::Picture ? adjustedSample(list, frame, size) : operatedSample(list, frame, size);
}

/// A still of \a list on its sample, for the list of presets.
QIcon sampleIcon(Sample sample, const std::vector<fla::behaviors::Applied>& list)
{
    const QSize size(66, 44);
    return QIcon(QPixmap::fromImage(sampleFrame(sample, demonstrated(list), kPreviewFrames / 2, size)));
}

/// A sample drawn moving as a preset makes it move, round and round: a
/// shape for behaviors, a word for letter animators, shapes for shape
/// operators and a picture for adjustment effects. The frames of the last
/// two are kept once drawn, as they take longer.
class PresetPreview : public QWidget
{
public:
    PresetPreview(Sample sample, QWidget* parent)
        : QWidget(parent)
        , _letters(sample == Sample::Word)
        , _sample(sample)
        , _word(nullptr)
    {
        setMinimumSize(320, 220);
        fla::TextRun run;
        run.text = "Phoenix";
        run.size = 44;
        run.face = "Arial-BoldMT";
        run.fillColor[0] = run.fillColor[1] = run.fillColor[2] = 0xF0;
        _word.runs = {run};
        _word.autoExpand = true;
        _word.textType = fla::StaticText::TextType::Dynamic;
        _timer.setInterval(1000 / 24);
        QObject::connect(&_timer, &QTimer::timeout, this, [this]() {
            _frame = (_frame + 1) % kFrames;
            update();
        });
        _timer.start();
    }
    void show(const std::vector<fla::behaviors::Applied>& list)
    {
        _list = list;
        _shown = demonstrated(list);
        _key = QString::fromStdString(fla::behaviors::encode(list));
        _frame = 0;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(0x20, 0x26, 0x30));
        // Shapes and the picture: each frame drawn once, small, and kept
        // for the next time round.
        if (_sample == Sample::Shapes || _sample == Sample::Picture)
        {
            const QSize size(280, 186);
            const QString key = _key + '#' + QString::number(_frame);
            QImage* picture = _frames.object(key);
            if (!picture)
            {
                picture = new QImage(sampleFrame(_sample, _shown, _frame, size));
                _frames.insert(key, picture, static_cast<int>(picture->sizeInBytes() / 1024));
            }
            const double fit = std::min(width() / static_cast<double>(size.width()), height() / static_cast<double>(size.height()));
            const QSizeF drawn(size.width() * fit, size.height() * fit);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(QRectF(QPointF((width() - drawn.width()) / 2.0, (height() - drawn.height()) / 2.0), drawn), *picture);
        }
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect().adjusted(6, 4, -6, -4), Qt::AlignRight | Qt::AlignBottom,
            QString("%1 / %2").arg(_frame + 1).arg(kFrames));
        if (_sample == Sample::Shapes || _sample == Sample::Picture)
            return;
        if (_letters)
        {
            const std::vector<fla::textanim::Letter> letters = TextLayout::letters(_word);
            const std::vector<fla::textanim::Change> changes = fla::textanim::apply(_list, letters, _frame, 0, 24.0);
            const QRectF box = TextLayout::localBounds(_word);
            painter.translate(width() / 2.0 - box.width() / 2.0, height() / 2.0 - box.height() / 2.0);
            TextLayout::drawAnimated(painter, _word, changes);
            return;
        }
        fla::behaviors::Moment moment;
        moment.frame = _frame;
        moment.frameRate = 24.0;
        moment.start = 0;
        moment.end = kFrames - 1;
        const fla::behaviors::Copies copies = BehaviorLibrary::instance().engine().evaluate(_list, moment);
        painter.translate(width() / 2.0, height() / 2.0);
        painter.scale(0.7, 0.7);
        const QColor accent = theme::color("accent");
        for (const fla::behaviors::Delta& copy : copies)
        {
            painter.save();
            painter.translate(copy.x, copy.y);
            painter.rotate(copy.rotation);
            painter.scale(copy.scaleX, copy.scaleY);
            painter.setOpacity(std::clamp(copy.alpha, 0.0, 1.0));
            QColor fill = accent;
            if (copy.tint > 0.0)
            {
                const QColor tint(copy.tintColor[0], copy.tintColor[1], copy.tintColor[2]);
                fill = QColor::fromRgbF(fill.redF() + (tint.redF() - fill.redF()) * copy.tint,
                    fill.greenF() + (tint.greenF() - fill.greenF()) * copy.tint,
                    fill.blueF() + (tint.blueF() - fill.blueF()) * copy.tint);
            }
            // A star, so turning shows.
            QPainterPath star;
            for (int i = 0; i < 10; ++i)
            {
                const double angle = -1.5707963267948966 + i * 0.6283185307179586;
                const double r = i % 2 == 0 ? 26.0 : 11.0;
                const QPointF point(std::cos(angle) * r, std::sin(angle) * r);
                if (i == 0)
                    star.moveTo(point);
                else
                    star.lineTo(point);
            }
            star.closeSubpath();
            painter.setPen(QPen(fill.darker(160), 2.0));
            painter.setBrush(fill);
            painter.drawPath(star);
            painter.restore();
        }
    }

private:
    static constexpr int kFrames = kPreviewFrames;
    bool _letters;
    Sample _sample;
    fla::StaticText _word;
    std::vector<fla::behaviors::Applied> _list;
    /// The list as played, and as a key to its frames kept (in KB).
    std::vector<fla::behaviors::Applied> _shown;
    QString _key;
    QCache<QString, QImage> _frames{48 * 1024};
    int _frame = 0;
    QTimer _timer;
};

} // namespace

BehaviorsPanel::BehaviorsPanel(BehaviorsLinks links, QWidget* parent)
    : QWidget(parent)
    , _links(std::move(links))
{
    _what = new QLabel(this);
    _what->setWordWrap(true);

    // A selected instance has two lists: behaviors that move it, and
    // effects on its picture.
    _sides = new QWidget(this);
    QHBoxLayout* sides = new QHBoxLayout(_sides);
    sides->setContentsMargins(0, 0, 0, 0);
    sides->setSpacing(0);
    _behaviorsSide = new QToolButton(_sides);
    _behaviorsSide->setObjectName("behaviorsSideBehaviors");
    _behaviorsSide->setText("Behaviors");
    _behaviorsSide->setToolTip("Motion worked out every frame, on top of the instance's keys");
    _effectsSide = new QToolButton(_sides);
    _effectsSide->setObjectName("behaviorsSideEffects");
    _effectsSide->setText("Effects");
    _effectsSide->setToolTip("Effects on the instance's own picture: blurs, glows, outlines, distortions, color and wipes");
    for (QToolButton* side : {_behaviorsSide, _effectsSide})
    {
        side->setCheckable(true);
        side->setAutoRaise(true);
        side->setToolButtonStyle(Qt::ToolButtonTextOnly);
        sides->addWidget(side);
    }
    sides->addStretch();
    _behaviorsSide->setChecked(true);
    connect(_behaviorsSide, &QToolButton::clicked, this, [this]() {
        _effectsShown = false;
        refresh();
    });
    connect(_effectsSide, &QToolButton::clicked, this, [this]() {
        _effectsShown = true;
        refresh();
    });

    _list = new QListWidget(this);
    _list->setMaximumHeight(130);
    _list->setToolTip("The behaviors on the selection, applied top to bottom; the box turns one on or off");

    _add = new QToolButton(this);
    _add->setObjectName("behaviorsAdd");
    _add->setText("Add Behavior");
    _add->setToolButtonStyle(Qt::ToolButtonTextOnly);
    _add->setPopupMode(QToolButton::InstantPopup);
    QMenu* menu = new QMenu(_add);
    connect(menu, &QMenu::aboutToShow, this, [this, menu]() {
        menu->clear();
        for (const fla::behaviors::Definition& definition : available())
        {
            QAction* action = menu->addAction(QString::fromStdString(definition.name));
            action->setToolTip(QString::fromStdString(definition.description));
            action->setStatusTip(QString::fromStdString(definition.description));
            const std::string id = definition.id;
            connect(action, &QAction::triggered, this, [this, id]() { add(id); });
        }
        // Letter animations ready made, of one or more animators.
        if (mode() == Mode::Texts)
        {
            menu->addSeparator();
            QMenu* presets = menu->addMenu("Presets");
            presets->setToolTipsVisible(true);
            for (const fla::textanim::Preset& preset : fla::textanim::presets())
            {
                QAction* action = presets->addAction(QString::fromStdString(preset.name));
                action->setToolTip(QString::fromStdString(preset.description));
                action->setStatusTip(QString::fromStdString(preset.description));
                const std::vector<fla::behaviors::Applied> animators = preset.animators;
                connect(action, &QAction::triggered, this, [this, animators]() {
                    edit("Add Letter Animation", [&animators](std::vector<fla::behaviors::Applied>& list) {
                        list.insert(list.end(), animators.begin(), animators.end());
                    });
                    refresh();
                    _list->setCurrentRow(_list->count() - 1);
                });
            }
        }
        // Every one of them and some made together, each seen moving.
        {
            menu->addSeparator();
            QAction* browse = menu->addAction("Browse Presets...");
            browse->setStatusTip(mode() == Mode::Shapes ? "See each shape operator on sample shapes before you add it"
                    : mode() == Mode::Adjustments      ? "See each effect on a sample picture before you add it"
                                                       : "See each behavior and ready-made animation moving before you add it");
            connect(browse, &QAction::triggered, this, &BehaviorsPanel::browsePresets);
        }
        // Behaviors of one's own: a new one to write, and where they are.
        if (mode() == Mode::Behaviors)
        {
            menu->addSeparator();
            QAction* write = menu->addAction("New Behavior...");
            write->setStatusTip("Write a behavior of your own in JavaScript, starting from an example");
            connect(write, &QAction::triggered, this, &BehaviorsPanel::newBehavior);
            QAction* folder = menu->addAction("Open Behaviors Folder");
            folder->setStatusTip("Show the folder your own behaviors are kept in");
            connect(folder, &QAction::triggered, this, []() {
                QDesktopServices::openUrl(QUrl::fromLocalFile(BehaviorLibrary::folder()));
            });
        }
        menu->setToolTipsVisible(true);
    });
    _add->setMenu(menu);
    _up = new QToolButton(this);
    _up->setArrowType(Qt::UpArrow);
    _up->setToolTip("Apply it earlier");
    _down = new QToolButton(this);
    _down->setArrowType(Qt::DownArrow);
    _down->setToolTip("Apply it later");
    _remove = new QToolButton(this);
    _remove->setText("Remove");
    _remove->setToolTip("Take the behavior off the selection");
    connect(_up, &QToolButton::clicked, this, [this]() { move(-1); });
    connect(_down, &QToolButton::clicked, this, [this]() { move(1); });
    connect(_remove, &QToolButton::clicked, this, &BehaviorsPanel::remove);
    _bake = new QToolButton(this);
    _bake->setObjectName("behaviorsBake");
    _bake->setText("Bake");
    _bake->setToolTip("Bake to Keyframes: leave the selected instances' behaviors as keyframes, a keyframe a frame, "
                      "their copies as instances of their own");
    connect(_bake, &QToolButton::clicked, this, [this]() {
        if (_links.bake)
            _links.bake();
    });

    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->addWidget(_add);
    buttons->addStretch();
    buttons->addWidget(_up);
    buttons->addWidget(_down);
    buttons->addWidget(_remove);
    buttons->addWidget(_bake);

    _description = new QLabel(this);
    _description->setWordWrap(true);
    _description->setStyleSheet("color: palette(placeholder-text);");

    _settings = new QWidget(this);
    _form = new QFormLayout(_settings);
    _form->setContentsMargins(0, 0, 0, 0);
    // A long label puts its controls on the line under it, rather than
    // pushing them out of a narrow panel.
    _form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(_settings);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(_sides);
    layout->addWidget(_what);
    layout->addWidget(_list);
    layout->addLayout(buttons);
    layout->addWidget(_description);
    layout->addWidget(scroll, 1);

    connect(_list, &QListWidget::currentRowChanged, this, [this](int) {
        if (!_refreshing)
            fillSettings();
    });
    connect(_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (_refreshing)
            return;
        const int row = _list->row(item);
        const bool on = item->checkState() == Qt::Checked;
        edit(on ? "Turn Behavior On" : "Turn Behavior Off", [row, on](std::vector<fla::behaviors::Applied>& list) {
            if (row >= 0 && row < static_cast<int>(list.size()))
                list[static_cast<size_t>(row)].enabled = on;
        });
    });
    connect(&BehaviorLibrary::instance(), &BehaviorLibrary::changed, this, &BehaviorsPanel::refresh);
    refresh();
}

BehaviorsPanel::Mode BehaviorsPanel::mode() const
{
    // Instances first, then shapes, then text fields.
    const std::vector<fla::SymbolInstance*> instances = _links.instances ? _links.instances() : std::vector<fla::SymbolInstance*>();
    if (!instances.empty())
        return _effectsShown ? Mode::InstanceEffects : Mode::Behaviors;
    if (_links.shapes && !_links.shapes().empty())
        return Mode::Shapes;
    if (_links.texts && !_links.texts().empty())
        return Mode::Texts;
    if (_links.adjustmentLayer && _links.adjustmentLayer())
        return Mode::Adjustments;
    return Mode::Behaviors;
}

bool BehaviorsPanel::shapeMode() const
{
    return mode() == Mode::Shapes;
}

std::vector<fla::DOMElement*> BehaviorsPanel::targets() const
{
    std::vector<fla::DOMElement*> list;
    switch (mode())
    {
    case Mode::Shapes:
        for (fla::Shape* shape : _links.shapes())
            list.push_back(shape);
        break;
    case Mode::Texts:
        for (fla::StaticText* text : _links.texts())
            list.push_back(text);
        break;
    case Mode::Adjustments:
        list.push_back(_links.adjustmentLayer());
        break;
    case Mode::InstanceEffects:
    default:
        if (_links.instances)
            for (fla::SymbolInstance* instance : _links.instances())
                list.push_back(instance);
    }
    return list;
}

const char* BehaviorsPanel::attribute() const
{
    switch (mode())
    {
    case Mode::Shapes:
        return fla::shapeops::kAttribute;
    case Mode::Texts:
        return fla::textanim::kAttribute;
    case Mode::Adjustments:
        return fla::compositing::kAdjustments;
    case Mode::InstanceEffects:
        return fla::compositing::kInstanceEffects;
    default:
        return "phoenixBehaviors";
    }
}

const fla::behaviors::Definition* BehaviorsPanel::definitionOf(const std::string& id) const
{
    switch (mode())
    {
    case Mode::Shapes:
        return fla::shapeops::find(id);
    case Mode::Texts:
        return fla::textanim::find(id);
    case Mode::Adjustments:
    case Mode::InstanceEffects:
        return fla::compositing::find(id);
    default:
        return BehaviorLibrary::instance().engine().find(id);
    }
}

std::vector<fla::behaviors::Definition> BehaviorsPanel::available() const
{
    switch (mode())
    {
    case Mode::Shapes:
        return fla::shapeops::definitions();
    case Mode::Texts:
        return fla::textanim::definitions();
    case Mode::Adjustments:
    case Mode::InstanceEffects:
        return fla::compositing::definitions();
    default:
        return BehaviorLibrary::instance().engine().definitions();
    }
}

std::vector<fla::behaviors::Applied> BehaviorsPanel::shown() const
{
    const std::vector<fla::DOMElement*> list = targets();
    return list.empty() ? std::vector<fla::behaviors::Applied>() : fla::behaviors::listOf(*list.front(), attribute());
}

void BehaviorsPanel::refresh()
{
    if (_refreshing)
        return;
    _refreshing = true;
    const std::vector<fla::DOMElement*> selected = targets();
    const bool shapes = shapeMode();
    const bool texts = mode() == Mode::Texts;
    const bool adjusting = mode() == Mode::Adjustments;
    const bool effects = mode() == Mode::InstanceEffects;
    const int keep = _list->currentRow();
    _list->clear();
    _add->setText(shapes ? "Add Operator" : texts ? "Add Animator" : adjusting || effects ? "Add Effect" : "Add Behavior");
    // The two sides show while instances are selected.
    const bool instances = mode() == Mode::Behaviors || effects;
    _sides->setVisible(instances && !selected.empty());
    _behaviorsSide->setChecked(!effects);
    _effectsSide->setChecked(effects);
    if (selected.empty())
        _what->setText("Select a symbol instance to give it behaviors (motion worked out every frame, on top of its "
                       "keys) or effects on its picture, a drawn shape to give it operators (Trim Paths, Wiggle Path, Zig-zag), or a text field "
                       "to animate its letters.");
    else if (adjusting)
        _what->setText(QString("Effects of the adjustment layer %1, on everything below it")
                           .arg(QString::fromStdString(static_cast<fla::Layer*>(selected.front())->name)));
    else if (effects)
        _what->setText(selected.size() == 1
                ? QString("Effects of %1, on its own picture")
                      .arg(QString::fromStdString(static_cast<fla::SymbolInstance*>(selected.front())->libraryItemName))
                : QString("Effects of %1 instances (shown: the first one's)").arg(selected.size()));
    else if (texts)
        _what->setText(selected.size() == 1 ? QString("Letter animators of the text")
                                            : QString("Letter animators of %1 text fields (shown: the first one's)").arg(selected.size()));
    else if (shapes)
        _what->setText(selected.size() == 1 ? QString("Operators of the shape")
                                            : QString("Operators of %1 shapes (shown: the first one's)").arg(selected.size()));
    else if (selected.size() == 1)
        _what->setText(QString("Behaviors of %1")
                           .arg(QString::fromStdString(static_cast<fla::SymbolInstance*>(selected.front())->libraryItemName)));
    else
        _what->setText(QString("Behaviors of %1 instances (shown: the first one's)").arg(selected.size()));
    for (const fla::behaviors::Applied& applied : shown())
    {
        const fla::behaviors::Definition* definition = definitionOf(applied.type);
        QListWidgetItem* item = new QListWidgetItem(
            definition ? QString::fromStdString(definition->name) : QString("%1 (missing)").arg(QString::fromStdString(applied.type)),
            _list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(applied.enabled ? Qt::Checked : Qt::Unchecked);
        if (!definition)
            item->setToolTip("No behavior of this name is known: its file may have been taken out of the Behaviors folder");
    }
    _add->setEnabled(!selected.empty());
    if (_list->count() > 0)
        _list->setCurrentRow(std::clamp(keep, 0, _list->count() - 1));
    const bool any = _list->count() > 0;
    _up->setEnabled(any);
    _down->setEnabled(any);
    _remove->setEnabled(any);
    // Effects are drawn live; there is nothing to bake.
    _bake->setVisible(mode() != Mode::Adjustments && !effects);
    _bake->setEnabled(any);
    _refreshing = false;
    fillSettings();
}

void BehaviorsPanel::fillSettings()
{
    while (QLayoutItem* item = _form->count() > 0 ? _form->takeAt(0) : nullptr)
    {
        // Quiet as it goes: a field losing focus on its way out must not
        // set anything.
        if (QWidget* widget = item->widget())
        {
            widget->blockSignals(true);
            for (QObject* inside : widget->findChildren<QObject*>())
                inside->blockSignals(true);
            delete widget;
        }
        delete item;
    }
    const std::vector<fla::behaviors::Applied> list = shown();
    const int row = _list->currentRow();
    if (row < 0 || row >= static_cast<int>(list.size()))
    {
        _description->clear();
        return;
    }
    const fla::behaviors::Applied& applied = list[static_cast<size_t>(row)];
    const fla::behaviors::Definition* definition = definitionOf(applied.type);
    if (!definition)
    {
        _description->setText("This behavior's file is missing; its settings are kept.");
        return;
    }
    _description->setText(QString::fromStdString(definition->description));
    const int now = frame();
    for (const fla::behaviors::ParamSpec& spec : definition->params)
    {
        // Keyed settings show their value on the frame shown.
        const std::string current = applied.valueAt(spec.name, now, spec.text);
        const QString label = QString::fromStdString(spec.label);
        const std::string name = spec.name;
        // The key button: filled with a key on this frame, hollow when keyed
        // on other frames, and plain when not keyed.
        QToolButton* key = new QToolButton(_settings);
        key->setAutoRaise(true);
        bool keyedHere = false;
        for (const auto& each : applied.keys(spec.name))
            keyedHere = keyedHere || each.first == now;
        key->setText(QString(QChar(keyedHere ? 0x25C6 : applied.keyed(spec.name) ? 0x25C7 : 0x00B7)));
        key->setToolTip(keyedHere ? "Keyed on this frame: click to take the key away"
                : applied.keyed(spec.name) ? "Keyed on other frames: click to key it here"
                                           : "Click to key this setting on the frame shown, so it can change over time");
        connect(key, &QToolButton::clicked, this, [this, row, name]() { toggleKey(row, name); });
        const auto addRow = [this, key, &label](QWidget* control) {
            QWidget* holder = new QWidget(_settings);
            QHBoxLayout* line = new QHBoxLayout(holder);
            line->setContentsMargins(0, 0, 0, 0);
            line->addWidget(control, 1);
            line->addWidget(key);
            _form->addRow(label, holder);
        };
        if (spec.type == "seed")
            key->hide();
        if (spec.type == "checkbox")
        {
            QCheckBox* box = new QCheckBox(_settings);
            box->setChecked(current == "true" || current == "1");
            connect(box, &QCheckBox::toggled, this,
                [this, row, name](bool on) { setParam(row, name, on ? "true" : "false", false); });
            addRow(box);
        }
        else if (spec.type == "choice")
        {
            QComboBox* combo = new QComboBox(_settings);
            for (const std::string& choice : spec.choices)
                combo->addItem(QString::fromStdString(choice));
            combo->setCurrentText(QString::fromStdString(current));
            connect(combo, &QComboBox::currentTextChanged, this,
                [this, row, name](const QString& text) { setParam(row, name, text.toStdString(), false); });
            addRow(combo);
        }
        else if (spec.type == "object" || spec.type == "controller" || spec.type == "text")
        {
            // A name: picked from the instances (or controllers) there are,
            // or typed.
            QComboBox* combo = new QComboBox(_settings);
            combo->setEditable(true);
            combo->setInsertPolicy(QComboBox::NoInsert);
            QStringList names;
            if (spec.type == "object" && _links.objectNames)
                names = _links.objectNames();
            else if (spec.type == "controller" && _links.controllerNames)
                names = _links.controllerNames();
            combo->addItems(names);
            combo->setCurrentText(QString::fromStdString(current));
            if (spec.type == "object")
                combo->setToolTip(names.isEmpty() ? "Give the other instance a name in the Properties panel first"
                                                  : "The instance name of another symbol instance on this timeline");
            else if (spec.type == "controller")
                combo->setToolTip(names.isEmpty() ? "Make a controller in the Controllers panel first"
                                                  : "A master controller, from the Controllers panel");
            connect(combo->lineEdit(), &QLineEdit::editingFinished, this, [this, row, name, combo]() {
                setParam(row, name, combo->currentText().toStdString(), false);
            });
            connect(combo, &QComboBox::activated, this, [this, row, name, combo](int) {
                setParam(row, name, combo->currentText().toStdString(), false);
            });
            if (spec.type == "object" && _links.pickObject)
            {
                // Or picked by clicking it on the stage.
                QWidget* holder = new QWidget(_settings);
                QHBoxLayout* line = new QHBoxLayout(holder);
                line->setContentsMargins(0, 0, 0, 0);
                combo->setParent(holder);
                line->addWidget(combo, 1);
                QToolButton* pick = new QToolButton(holder);
                pick->setText("Pick");
                pick->setToolTip("Click this, then the instance on the stage; one with no name is given one");
                line->addWidget(pick);
                connect(pick, &QToolButton::clicked, this, [this, row, name]() {
                    _links.pickObject([this, row, name](const QString& picked) {
                        if (picked.isEmpty())
                            return;
                        setParam(row, name, picked.toStdString(), false);
                        refresh();
                    });
                });
                addRow(holder);
            }
            else
                addRow(combo);
        }
        else if (spec.type == "seed")
        {
            QWidget* holder = new QWidget(_settings);
            QHBoxLayout* line = new QHBoxLayout(holder);
            line->setContentsMargins(0, 0, 0, 0);
            HotSpinBox* seed = new HotSpinBox(holder);
            seed->setRange(0, 9999);
            {
                const QSignalBlocker quiet(seed);
                seed->setValue(static_cast<int>(std::lround(std::atof(current.c_str()))));
            }
            seed->setToolTip("Another seed gives another, equally random, result");
            QPushButton* again = new QPushButton("New", holder);
            again->setToolTip("A new seed");
            line->addWidget(seed);
            line->addWidget(again, 1);
            connect(seed, &HotText::valueChanged, this, [this, row, name](double value, bool live) {
                setParam(row, name, std::to_string(static_cast<int>(std::lround(value))), live);
            });
            connect(again, &QPushButton::clicked, seed, [seed]() { seed->setValue(newSeed()); });
            addRow(holder);
        }
        else
        {
            // A number: a slider over its range, and the box to type in.
            QWidget* holder = new QWidget(_settings);
            QHBoxLayout* line = new QHBoxLayout(holder);
            line->setContentsMargins(0, 0, 0, 0);
            const double step = spec.step > 0.0 ? spec.step : 1.0;
            const double low = std::min(spec.min, spec.max);
            const double high = std::max(spec.min, spec.max);
            const int steps = static_cast<int>(std::lround((high - low) / step));
            QSlider* slider = new QSlider(Qt::Horizontal, holder);
            slider->setRange(0, std::max(1, steps));
            slider->setMinimumWidth(60);
            HotDoubleSpinBox* box = new HotDoubleSpinBox(holder);
            const int decimals = step >= 1.0 ? 0 : step >= 0.1 ? 1 : step >= 0.01 ? 2 : 3;
            box->setDecimals(decimals);
            box->setRange(low, high);
            box->setSingleStep(step);
            box->setStep(step);
            box->setMinimumWidth(56);
            const double value = current.empty() ? spec.value : std::atof(current.c_str());
            {
                const QSignalBlocker quiet(box);
                box->setValue(value);
            }
            slider->setValue(static_cast<int>(std::lround((value - low) / step)));
            line->addWidget(slider, 1);
            line->addWidget(box);
            connect(slider, &QSlider::valueChanged, this, [this, row, name, box, low, step](int position) {
                const double v = low + position * step;
                const QSignalBlocker quiet(box);
                box->setValue(v);
                setParam(row, name, numberText(v), true);
            });
            connect(box, &HotText::valueChanged, this, [this, row, name, slider, low, step](double v, bool) {
                const QSignalBlocker quiet(slider);
                slider->setValue(static_cast<int>(std::lround((v - low) / step)));
                setParam(row, name, numberText(v), true);
            });
            addRow(holder);
        }
    }
}

void BehaviorsPanel::edit(const std::string& name, const std::function<void(std::vector<fla::behaviors::Applied>&)>& change,
    bool merge)
{
    fla::CommandStack* stack = _links.commands ? _links.commands() : nullptr;
    const std::vector<fla::DOMElement*> selected = targets();
    const std::string key = attribute();
    if (!stack || selected.empty())
        return;
    // The panel's own change: what it shows stays as it is while the
    // history tells the window.
    _refreshing = true;
    const bool several = selected.size() > 1;
    if (several)
        stack->beginMacro(name);
    for (fla::DOMElement* element : selected)
    {
        std::vector<fla::behaviors::Applied> list = fla::behaviors::listOf(*element, key);
        change(list);
        const Attributes after = fla::behaviors::withList(*element, key, list);
        if (after == element->extraAttributes)
            continue;
        stack->push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(element->extraAttributes,
            element->extraAttributes, after, name, merge && !several ? kMergeSetting : -1)));
    }
    if (several)
        stack->endMacro();
    _refreshing = false;
    if (_links.changed)
        _links.changed();
}

void BehaviorsPanel::add(const std::string& id)
{
    const Mode now = mode();
    const fla::behaviors::Applied applied = now == Mode::Shapes ? fla::shapeops::defaults(id, newSeed())
        : now == Mode::Texts                                    ? fla::textanim::defaults(id, newSeed())
        : now == Mode::Adjustments || now == Mode::InstanceEffects ? fla::compositing::defaults(id, newSeed())
                                                                : BehaviorLibrary::instance().engine().defaults(id, newSeed());
    edit(now == Mode::Shapes ? "Add Operator" : now == Mode::Texts ? "Add Letter Animator"
            : now == Mode::Adjustments || now == Mode::InstanceEffects ? "Add Effect"
                                                                       : "Add Behavior", [&applied](std::vector<fla::behaviors::Applied>& list) { list.push_back(applied); });
    refresh();
    _list->setCurrentRow(_list->count() - 1);
}

void BehaviorsPanel::browsePresets()
{
    const Mode now = mode();
    const bool letters = now == Mode::Texts;
    const Sample sample = letters ? Sample::Word : now == Mode::Shapes ? Sample::Shapes
        : now == Mode::Adjustments                                     ? Sample::Picture
                                                                       : Sample::Star;
    std::vector<BrowsedPreset> presets;
    if (letters)
        for (const fla::textanim::Preset& preset : fla::textanim::presets())
            presets.push_back({QString::fromStdString(preset.name), QString::fromStdString(preset.description), preset.animators});
    else if (now == Mode::Behaviors)
        presets = behaviorPresets();
    const size_t made = presets.size();
    // Then every one on its own, as it comes, but those that need another
    // instance, a path or a sound to show anything.
    static const std::set<std::string> needMore = {"followpath", "pathrepeat", "lookat", "linkobject", "linkcontrol",
        "linkcontroller", "sound", "recorded", "timeremap", "slidercontrol", "anglecontrol", "pointcontrol", "colorcontrol",
        "checkboxcontrol", "effector", "follow", "jiggle", "path", "sourcetext"};
    for (const fla::behaviors::Definition& definition : available())
        if (!needMore.count(definition.id))
            presets.push_back({QString::fromStdString(definition.name), QString::fromStdString(definition.description),
                {letters                      ? fla::textanim::defaults(definition.id, 1)
                        : now == Mode::Shapes      ? fla::shapeops::defaults(definition.id, 1)
                        : now == Mode::Adjustments ? fla::compositing::defaults(definition.id, 1)
                                                   : BehaviorLibrary::instance().engine().defaults(definition.id, 1)}});

    QDialog dialog(this);
    dialog.setWindowTitle(letters ? "Letter Animation Presets" : now == Mode::Shapes ? "Shape Operator Presets"
            : now == Mode::Adjustments                                             ? "Effect Presets"
                                                                                   : "Behavior Presets");
    auto* layout = new QHBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    list->setMinimumWidth(200);
    const auto heading = [list](const QString& text) {
        auto* item = new QListWidgetItem(text, list);
        item->setFlags(Qt::NoItemFlags);
        QFont font = item->font();
        font.setBold(true);
        item->setFont(font);
    };
    if (made > 0)
        heading("Ready made");
    // Shape operators and effects show a still of each beside its name.
    const bool stills = sample == Sample::Shapes || sample == Sample::Picture;
    if (stills)
        list->setIconSize(QSize(66, 44));
    for (size_t i = 0; i < presets.size(); ++i)
    {
        if (i == made)
            heading(letters ? "Letter animators" : now == Mode::Shapes ? "Shape operators"
                    : now == Mode::Adjustments                         ? "Effects"
                                                                       : "Behaviors");
        auto* item = new QListWidgetItem(presets[i].name, list);
        item->setData(Qt::UserRole, static_cast<int>(i));
        item->setToolTip(presets[i].description);
        if (stills)
            item->setIcon(sampleIcon(sample, presets[i].list));
    }
    layout->addWidget(list);
    auto* side = new QVBoxLayout();
    auto* preview = new PresetPreview(sample, &dialog);
    side->addWidget(preview, 1);
    auto* about = new QLabel(&dialog);
    about->setWordWrap(true);
    about->setMinimumHeight(48);
    about->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    side->addWidget(about);
    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* apply = buttons->addButton(letters ? "Add to Text" : now == Mode::Adjustments ? "Add to Layer" : "Add to Selection",
        QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);
    side->addWidget(buttons);
    layout->addLayout(side, 1);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::currentItemChanged, &dialog, [&presets, preview, about, apply](QListWidgetItem* item) {
        const QVariant index = item ? item->data(Qt::UserRole) : QVariant();
        apply->setEnabled(index.isValid());
        if (!index.isValid())
            return;
        const BrowsedPreset& preset = presets[static_cast<size_t>(index.toInt())];
        preview->show(preset.list);
        about->setText(preset.description);
    });
    // Added as a step of its own, and the dialog stays for another.
    const char* stepName = letters ? "Add Letter Animation" : now == Mode::Shapes ? "Add Operator"
        : now == Mode::Adjustments                                               ? "Add Effect"
                                                                                 : "Add Behavior";
    const auto addChosen = [this, &presets, list, stepName]() {
        const QVariant index = list->currentItem() ? list->currentItem()->data(Qt::UserRole) : QVariant();
        if (!index.isValid() || targets().empty())
            return;
        std::vector<fla::behaviors::Applied> chosen = presets[static_cast<size_t>(index.toInt())].list;
        for (fla::behaviors::Applied& applied : chosen)
            if (applied.params.count("seed"))
                applied.params["seed"] = std::to_string(newSeed());
        edit(stepName, [&chosen](std::vector<fla::behaviors::Applied>& each) {
            each.insert(each.end(), chosen.begin(), chosen.end());
        });
        refresh();
        _list->setCurrentRow(_list->count() - 1);
    };
    connect(apply, &QPushButton::clicked, &dialog, addChosen);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, addChosen);
    list->setCurrentRow(made > 0 ? 1 : 0);
    dialog.resize(640, 360);
    dialog.exec();
}

void BehaviorsPanel::move(int by)
{
    const int row = _list->currentRow();
    const int to = row + by;
    if (row < 0 || to < 0 || to >= _list->count())
        return;
    edit("Reorder Behaviors", [row, to](std::vector<fla::behaviors::Applied>& list) {
        if (row < static_cast<int>(list.size()) && to < static_cast<int>(list.size()))
            std::swap(list[static_cast<size_t>(row)], list[static_cast<size_t>(to)]);
    });
    refresh();
    _list->setCurrentRow(to);
}

void BehaviorsPanel::remove()
{
    const int row = _list->currentRow();
    if (row < 0)
        return;
    edit("Remove Behavior", [row](std::vector<fla::behaviors::Applied>& list) {
        if (row < static_cast<int>(list.size()))
            list.erase(list.begin() + row);
    });
    refresh();
}

void BehaviorsPanel::setParam(int row, const std::string& name, const std::string& value, bool merge)
{
    const std::vector<fla::behaviors::Applied> first = shown();
    if (row < 0 || row >= static_cast<int>(first.size()))
        return;
    const std::string type = first[static_cast<size_t>(row)].type;
    // On each selected instance that has this behavior in this place; a
    // keyed setting is keyed on the frame shown.
    const int now = frame();
    edit("Behavior Settings",
        [row, &type, &name, &value, now](std::vector<fla::behaviors::Applied>& list) {
            if (row >= static_cast<int>(list.size()) || list[static_cast<size_t>(row)].type != type)
                return;
            fla::behaviors::Applied& applied = list[static_cast<size_t>(row)];
            if (applied.keyed(name))
                applied.setKey(name, now, value);
            else
                applied.params[name] = value;
        },
        merge);
    // The key button shows the key now made.
    if (!first[static_cast<size_t>(row)].keyed(name))
        return;
    bool keyedHere = false;
    for (const auto& each : first[static_cast<size_t>(row)].keys(name))
        keyedHere = keyedHere || each.first == now;
    if (!keyedHere)
        QTimer::singleShot(0, this, [this]() { fillSettings(); });
}

void BehaviorsPanel::toggleKey(int row, const std::string& name)
{
    const std::vector<fla::behaviors::Applied> first = shown();
    if (row < 0 || row >= static_cast<int>(first.size()))
        return;
    const std::string type = first[static_cast<size_t>(row)].type;
    const fla::behaviors::Definition* definition = definitionOf(type);
    std::string fallback;
    if (definition)
        for (const fla::behaviors::ParamSpec& spec : definition->params)
            if (spec.name == name)
                fallback = spec.text;
    const int now = frame();
    bool keyedHere = false;
    for (const auto& each : first[static_cast<size_t>(row)].keys(name))
        keyedHere = keyedHere || each.first == now;
    edit(keyedHere ? "Remove Behavior Key" : "Key Behavior Setting",
        [row, &type, &name, now, keyedHere, &fallback](std::vector<fla::behaviors::Applied>& list) {
            if (row >= static_cast<int>(list.size()) || list[static_cast<size_t>(row)].type != type)
                return;
            fla::behaviors::Applied& applied = list[static_cast<size_t>(row)];
            const std::string value = applied.valueAt(name, now, fallback);
            if (keyedHere)
                applied.removeKey(name, now, value);
            else
                applied.setKey(name, now, value);
        });
    fillSettings();
}

int BehaviorsPanel::frame() const
{
    return _links.currentFrame ? _links.currentFrame() : 0;
}

void BehaviorsPanel::newBehavior()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New Behavior", "Name:", QLineEdit::Normal, "My Behavior", &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    // The file is named after it, in letters and digits.
    QString base = name;
    base.remove(QRegularExpression("[^A-Za-z0-9_ -]"));
    base = base.simplified().replace(' ', '-');
    if (base.isEmpty())
        base = "behavior";
    const QString path = QDir(BehaviorLibrary::folder()).filePath(base + ".js");
    if (!QFileInfo::exists(path))
    {
        QString quoted = name;
        quoted.replace("\\", "\\\\").replace("\"", "\\\"");
        const QString source = QString(
            "// A behavior of your own. Phoenix calls evaluate for each frame the\n"
            "// instance is on, and adds what it returns to the keyed motion: x and y in\n"
            "// pixels, rotation in degrees, scale and alpha as factors, frame to run a\n"
            "// graphic's loop on. Save, and the behavior is in Add Behavior at once.\n"
            "behavior({\n"
            "    name: \"%1\",\n"
            "    description: \"Drifts sideways and bobs up and down.\",\n"
            "    params: {\n"
            "        speed: { label: \"Speed (pixels a second)\", value: 40, min: -500, max: 500 },\n"
            "        bob: { label: \"Bob (pixels)\", value: 4, min: 0, max: 50 }\n"
            "    },\n"
            "    evaluate: function (ctx, p) {\n"
            "        return {\n"
            "            x: p.speed * (ctx.frame - ctx.start) / ctx.fps,\n"
            "            y: Math.sin(ctx.time * 2) * p.bob\n"
            "        };\n"
            "    }\n"
            "});\n").arg(quoted);
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            QMessageBox::warning(this, "New Behavior", QString("Could not write %1.").arg(QDir::toNativeSeparators(path)));
            return;
        }
        out.write(source.toUtf8());
        out.close();
        BehaviorLibrary::instance().reload();
        refresh();
    }
    if (_links.openScript)
        _links.openScript(path);
    else
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

