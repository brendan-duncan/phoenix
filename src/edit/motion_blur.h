#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fla {

class DOMElement;
class Document;
class Layer;

/// Motion blur, Phoenix's own: a layer with it on is drawn several times
/// across the shutter, part way between its frames, and the pictures
/// averaged, so what moves fast smears along its path. The layer keeps
/// phoenixMotionBlur="true"; the document its shutter angle (degrees of a
/// frame, 180 a half frame, centered on it) and how many pictures it
/// averages. Other apps that open the FLA draw the layers sharp. Free of
/// Qt.
namespace motionblur {

extern const char* const kLayer;
extern const char* const kShutter;
extern const char* const kSamples;

bool on(const Layer& layer);
/// \a layer's attributes with motion blur \a on or off.
std::vector<std::pair<std::string, std::string>> withOn(const Layer& layer, bool on);

/// The document's shutter angle (0 to 720, 180 unless set) and samples (2
/// to 64, 16 unless set).
double shutter(const Document& document);
int samples(const Document& document);
/// \a document's attributes with the shutter and samples given.
std::vector<std::pair<std::string, std::string>> withSettings(const Document& document, double shutter, int samples);

/// When, in frames from the frame drawn, each picture is taken: \a samples
/// times spread evenly across the shutter, centered on the frame.
std::vector<double> sampleTimes(double shutter, int samples);

/// A layer's time effects, Phoenix's own: Echo draws it again as it was
/// some frames before, each fainter (a trail of earlier poses), and
/// Posterize Time holds each pose for a number of frames (animation on
/// twos, a jerky stop-motion look). Kept in phoenixEcho
/// ("count,interval,decay") and phoenixPosterize (frames a pose).
struct LayerTime
{
    int echoes = 0;
    int interval = 2;
    /// How much of the one before each echo keeps, 0 to 1.
    double decay = 0.6;
    int posterize = 1;
    bool any() const { return echoes > 0 || posterize > 1; }
};
LayerTime layerTimeOf(const Layer& layer);
std::vector<std::pair<std::string, std::string>> withLayerTime(const Layer& layer, const LayerTime& time);
/// The frame \a frame shows held as \a time holds it.
int posterized(const LayerTime& time, int frame);

} // namespace motionblur

} // namespace fla
