#include "envelope_dialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kFull = 32768;
constexpr int kMaxPoints = 8;
constexpr double kRuler = 22.0;
constexpr double kHandle = 4.0;

} // namespace

EnvelopeEditor::EnvelopeEditor(std::shared_ptr<const PcmBuffer> sound, double frameRate, QWidget* parent)
    : QWidget(parent)
    , _sound(std::move(sound))
    , _frameRate(frameRate > 0.0 ? frameRate : 24.0)
{
    setMinimumSize(400, 220);
    setMouseTracking(true);
    // Fit the sound to the width to begin with.
    _samplesPerPixel = std::max(1.0, static_cast<double>(length()) / 620.0);
}

size_t EnvelopeEditor::length() const
{
    return _sound ? _sound->frames() : 0;
}

void EnvelopeEditor::zoom(double factor)
{
    _samplesPerPixel = std::clamp(_samplesPerPixel / factor, 1.0, std::max(1.0, static_cast<double>(length())));
    update();
}

void EnvelopeEditor::setShowFrames(bool frames)
{
    _showFrames = frames;
    update();
}

QRectF EnvelopeEditor::lane(int channel) const
{
    const double half = (height() - kRuler) / 2.0;
    return QRectF(0.0, channel == 0 ? 0.0 : half + kRuler, width(), half);
}

QRectF EnvelopeEditor::ruler() const
{
    const double half = (height() - kRuler) / 2.0;
    return QRectF(0.0, half, width(), kRuler);
}

double EnvelopeEditor::xOf(size_t sample) const
{
    return static_cast<double>(sample) / _samplesPerPixel;
}

size_t EnvelopeEditor::sampleAt(double x) const
{
    return static_cast<size_t>(std::clamp(x * _samplesPerPixel, 0.0, static_cast<double>(length())));
}

int EnvelopeEditor::levelAt(int channel, size_t sample) const
{
    if (points.empty())
        return kFull;
    const auto value = [channel](const fla::frame_sound::EnvelopePoint& p) { return channel == 0 ? p.left : p.right; };
    if (sample <= points.front().mark)
        return value(points.front());
    for (size_t i = 1; i < points.size(); ++i)
    {
        if (sample > points[i].mark)
            continue;
        const auto& a = points[i - 1];
        const auto& b = points[i];
        const double t = b.mark > a.mark ? static_cast<double>(sample - a.mark) / static_cast<double>(b.mark - a.mark) : 1.0;
        return static_cast<int>(std::lround(value(a) + (value(b) - value(a)) * t));
    }
    return value(points.back());
}

void EnvelopeEditor::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    const QColor wave = palette().color(QPalette::Text);
    const QColor envelope(40, 170, 255);
    for (int channel = 0; channel < 2; ++channel)
    {
        const QRectF area = lane(channel);
        painter.fillRect(area.adjusted(0, 1, 0, -1), palette().color(QPalette::AlternateBase));
        // The waveform: each pixel's peak, about the lane's middle.
        if (_sound)
        {
            painter.setPen(QPen(wave, 1.0));
            const double middle = area.center().y();
            const double reach = area.height() / 2.0 - 2.0;
            for (int x = 0; x < width(); ++x)
            {
                const size_t first = sampleAt(x);
                const size_t last = std::min(length(), sampleAt(x + 1) + 1);
                float peak = 0.0f;
                for (size_t s = first; s < last; s += std::max<size_t>(1, (last - first) / 64))
                    peak = std::max(peak, std::fabs(_sound->samples[s * 2 + static_cast<size_t>(channel)]));
                painter.drawLine(QPointF(x + 0.5, middle - peak * reach), QPointF(x + 0.5, middle + peak * reach));
            }
        }
        // The envelope, full volume at the top of the lane.
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(envelope, 1.5));
        QPainterPath line;
        const auto yOf = [&area](int level) { return area.bottom() - area.height() * level / static_cast<double>(kFull); };
        line.moveTo(0.0, yOf(levelAt(channel, 0)));
        for (const auto& point : points)
            line.lineTo(xOf(point.mark), yOf(channel == 0 ? point.left : point.right));
        line.lineTo(width(), yOf(levelAt(channel, length())));
        painter.drawPath(line);
        painter.setBrush(Qt::white);
        for (const auto& point : points)
            painter.drawRect(QRectF(xOf(point.mark) - kHandle, yOf(channel == 0 ? point.left : point.right) - kHandle,
                kHandle * 2.0, kHandle * 2.0));
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setBrush(Qt::NoBrush);
    }
    // The ruler: seconds or frames, and the trim bars.
    const QRectF bar = ruler();
    painter.fillRect(bar, palette().color(QPalette::Window));
    painter.setPen(palette().color(QPalette::WindowText));
    const double unit = _showFrames ? 44100.0 / _frameRate : 44100.0;
    double step = unit;
    while (step / _samplesPerPixel < 60.0)
        step *= 2.0;
    for (double sample = 0.0; xOf(static_cast<size_t>(sample)) < width(); sample += step)
    {
        const double x = xOf(static_cast<size_t>(sample));
        painter.drawLine(QPointF(x, bar.top()), QPointF(x, bar.top() + 5));
        painter.drawText(QPointF(x + 2, bar.bottom() - 5),
            _showFrames ? QString::number(static_cast<int>(sample / unit) + 1) : QString::number(sample / unit, 'f', 1) + "s");
    }
    const auto trimBar = [&](size_t sample, const QColor& color) {
        const double x = xOf(sample);
        painter.fillRect(QRectF(x - 2.0, bar.top(), 4.0, bar.height()), color);
        painter.setPen(QPen(color, 1.0, Qt::DashLine));
        painter.drawLine(QPointF(x, 0.0), QPointF(x, height()));
    };
    // Trimmed away is shaded.
    const size_t end = out > 0 ? out : length();
    painter.fillRect(QRectF(0.0, 0.0, xOf(in), height()), QColor(0, 0, 0, 70));
    painter.fillRect(QRectF(xOf(end), 0.0, width(), height()), QColor(0, 0, 0, 70));
    trimBar(in, QColor(60, 200, 90));
    trimBar(end, QColor(230, 90, 60));
}

void EnvelopeEditor::mousePressEvent(QMouseEvent* event)
{
    const QPointF at = event->position();
    _point = -1;
    _bar = 0;
    if (ruler().contains(at))
    {
        const size_t end = out > 0 ? out : length();
        _bar = std::fabs(at.x() - xOf(in)) <= std::fabs(at.x() - xOf(end)) ? 1 : 2;
        mouseMoveEvent(event);
        return;
    }
    const int channel = lane(0).contains(at) ? 0 : lane(1).contains(at) ? 1 : -1;
    if (channel < 0)
        return;
    const QRectF area = lane(channel);
    // A handle under the press, else a new point there.
    for (size_t i = 0; i < points.size(); ++i)
    {
        const int level = channel == 0 ? points[i].left : points[i].right;
        const QPointF handle(xOf(points[i].mark), area.bottom() - area.height() * level / static_cast<double>(kFull));
        if (std::fabs(handle.x() - at.x()) <= kHandle + 2 && std::fabs(handle.y() - at.y()) <= kHandle + 2)
        {
            _point = static_cast<int>(i);
            _channel = channel;
            return;
        }
    }
    if (points.size() >= kMaxPoints)
        return;
    fla::frame_sound::EnvelopePoint point;
    point.mark = sampleAt(at.x());
    point.left = levelAt(0, point.mark);
    point.right = levelAt(1, point.mark);
    const auto place = std::upper_bound(points.begin(), points.end(), point.mark,
        [](size_t mark, const fla::frame_sound::EnvelopePoint& p) { return mark < p.mark; });
    _point = static_cast<int>(place - points.begin());
    points.insert(place, point);
    _channel = channel;
    mouseMoveEvent(event);
}

void EnvelopeEditor::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;
    const QPointF at = event->position();
    if (_bar != 0)
    {
        const size_t sample = sampleAt(at.x());
        if (_bar == 1)
            in = std::min(sample, (out > 0 ? out : length()) > 0 ? (out > 0 ? out : length()) - 1 : 0);
        else
            out = sample >= length() ? 0 : std::max(sample, in + 1);
        update();
        emit changed();
        return;
    }
    if (_point < 0 || _point >= static_cast<int>(points.size()))
        return;
    auto& point = points[static_cast<size_t>(_point)];
    // Along, between its neighbors; up and down, its channel's level.
    const size_t low = _point > 0 ? points[static_cast<size_t>(_point - 1)].mark : 0;
    const size_t high = _point + 1 < static_cast<int>(points.size()) ? points[static_cast<size_t>(_point + 1)].mark : length();
    point.mark = std::clamp(sampleAt(at.x()), low, high);
    const QRectF area = lane(_channel);
    const int level = static_cast<int>(std::lround(std::clamp((area.bottom() - at.y()) / area.height(), 0.0, 1.0) * kFull));
    (_channel == 0 ? point.left : point.right) = level;
    update();
    emit changed();
}

void EnvelopeEditor::mouseReleaseEvent(QMouseEvent* event)
{
    // Dragged well out of its lane, a point goes.
    if (_point >= 0 && _point < static_cast<int>(points.size()))
    {
        const QRectF area = lane(_channel).adjusted(0, -30, 0, 30);
        if (!area.contains(event->position()))
        {
            points.erase(points.begin() + _point);
            update();
            emit changed();
        }
    }
    _point = -1;
    _bar = 0;
}

EnvelopeDialog::EnvelopeDialog(std::shared_ptr<const PcmBuffer> sound, double frameRate,
    const std::vector<fla::frame_sound::EnvelopePoint>& points, size_t in, size_t out, QWidget* parent)
    : QDialog(parent)
    , _sound(sound)
{
    setWindowTitle("Edit Envelope");
    QVBoxLayout* layout = new QVBoxLayout(this);
    _editor = new EnvelopeEditor(sound, frameRate, this);
    _editor->points = points;
    _editor->in = in;
    _editor->out = out;
    layout->addWidget(_editor, 1);
    QLabel* hint = new QLabel("Click a channel to add a point; drag a point out of its channel to remove it. "
                              "Drag the bars on the ruler to trim the start and end.", this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    QHBoxLayout* controls = new QHBoxLayout();
    QPushButton* play = new QPushButton("Play", this);
    QPushButton* stop = new QPushButton("Stop", this);
    QPushButton* zoomIn = new QPushButton("Zoom In", this);
    QPushButton* zoomOut = new QPushButton("Zoom Out", this);
    QPushButton* units = new QPushButton("Frames", this);
    units->setCheckable(true);
    units->setToolTip("Measure the ruler in frames instead of seconds");
    QPushButton* clear = new QPushButton("Clear", this);
    clear->setToolTip("Full volume throughout, untrimmed");
    for (QPushButton* button : {play, stop, zoomIn, zoomOut, units, clear})
        controls->addWidget(button);
    controls->addStretch(1);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    controls->addWidget(buttons);
    layout->addLayout(controls);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(zoomIn, &QPushButton::clicked, this, [this]() { _editor->zoom(2.0); });
    connect(zoomOut, &QPushButton::clicked, this, [this]() { _editor->zoom(0.5); });
    connect(units, &QPushButton::toggled, _editor, &EnvelopeEditor::setShowFrames);
    connect(clear, &QPushButton::clicked, this, [this]() {
        _editor->points.clear();
        _editor->in = 0;
        _editor->out = 0;
        _editor->update();
    });
    connect(play, &QPushButton::clicked, this, [this]() {
        if (!_sound)
            return;
        if (_voice >= 0)
            AudioEngine::instance().stop(_voice);
        // As the frame will play it: trimmed, through the envelope.
        auto envelope = std::make_shared<AudioEnvelope>();
        for (const auto& point : _editor->points)
            envelope->points.push_back({point.mark, point.left / 32768.0f, point.right / 32768.0f});
        const size_t end = _editor->out > 0 ? _editor->out : _sound->frames();
        _voice = AudioEngine::instance().play(_sound, _editor->in, end, 1, 1.0f,
            envelope->points.empty() ? nullptr : std::shared_ptr<const AudioEnvelope>(envelope));
    });
    connect(stop, &QPushButton::clicked, this, [this]() {
        if (_voice >= 0)
            AudioEngine::instance().stop(_voice);
        _voice = -1;
    });
    resize(760, 420);
}

EnvelopeDialog::~EnvelopeDialog()
{
    if (_voice >= 0)
        AudioEngine::instance().stop(_voice);
}
