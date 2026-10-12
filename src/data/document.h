#pragma once

#include "folder.h"
#include "lazy_bytes.h"
#include "resource.h"
#include "timeline.h"
#include "printer_settings.h"
#include "publish_history.h"
#include "script_list.h"
#include "swatch_list.h"
#include "symbol.h"
#include "symbol_instance.h"
#include "symbol_list.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <utility>

namespace fla {

class Document : public DOMElement
{
public:
    std::string source;

    std::string filetypeGUID;
    std::string fileGUID;
    int width = 550;
    int height = 400;
    double frameRate = 24.0;
    /// The scene shown and edited, counting from 1, as text.
    std::string currentTimeline;
    std::string xflVersion;
    std::string creatorInfo;
    std::string platform;
    std::string versionInfo;
    int majorVersion = 0;
    int buildNumber = 0;
    double viewAngle3D = 0.0;
    uint8_t backgroundColor[4] = {255, 255, 255, 255};
    uint8_t gridColor[4] = {0, 0, 0, 255};
    uint8_t guidesColor[4] = {0, 255, 0, 255};
    int gridSpacingX = 18;
    int gridSpacingY = 18;
    int snapAlignBorderSpacing = 18;
    /// Snap to Objects and Snap to Guides, which Animate keeps in the
    /// document; both on unless the file says otherwise.
    bool objectsSnapTo = true;
    bool guidesSnapTo = true;
    int timelineHeight = 197;
    int timelineLabelWidth = 256;
    double vanishingPoint3DX = 400.0;
    double vanishingPoint3DY = 275.0;
    int nextSceneIdentifier = 2;
    // Animate's defaults: it writes these only when they are off.
    bool playOptionsPlayLoop = true;
    bool playOptionsPlayPages = true;
    bool playOptionsPlayFrameActions = true;

    SymbolList* symbolList = nullptr;
    ScriptList* scriptList = nullptr;
    PublishHistory* publishHistory = nullptr;
    PrinterSettings* printerSettings = nullptr;
    std::vector<Resource*> resources;
    std::vector<Timeline*> timelines;
    std::vector<Folder*> folders;
    std::vector<SwatchList*> swatchLists;

    /// Parts of the document the reader does not model (sound items in the
    /// library, extended swatch lists, brush definitions, embedded fonts),
    /// kept as XML text so a save writes them back as read. A section
    /// remembers whether it came before the timelines, as Animate orders
    /// them.
    struct RawSection
    {
        std::string xml;
        bool beforeTimelines = false;
    };
    std::vector<RawSection> extraSections;
    /// Library items in <media> other than bitmaps (DOMSoundItem...).
    std::vector<std::string> extraMedia;
    /// Files of the package those refer to (a sound's data under bin, the
    /// brush definitions file), by path, carried through byte for byte.
    std::vector<std::pair<std::string, LazyBytes>> extraFiles;

    /// The library's sounds, as much as playing them needs: the name frames
    /// refer to them by, how the data is laid out, and where under the
    /// package the data is (its bytes are among extraFiles).
    struct SoundInfo
    {
        std::string name;
        std::string format;
        std::string dataPath;
    };
    std::vector<SoundInfo> sounds;

    /// The library's videos, as read (the items themselves are kept in
    /// extraMedia and written back as they came): their size in pixels,
    /// their frame rate and where they were imported from.
    struct VideoInfo
    {
        std::string name;
        int width = 0;
        int height = 0;
        double fps = 0.0;
        std::string source;
        /// Whether the video's frames are in the file (embedded) rather
        /// than linked.
        bool embedded = false;
    };
    std::vector<VideoInfo> videos;

    /// What a .phx file held that this Phoenix does not know: whole chunks,
    /// written back unchanged on save, and the Phoenix-only features the
    /// document uses (docs/PHOENIX_FORMAT.md).
    struct UnknownChunk
    {
        uint32_t type = 0;
        uint32_t flags = 0;
        std::string content;
    };
    std::vector<UnknownChunk> phxUnknownChunks;
    std::vector<std::string> phxFeatures;
    std::vector<std::string> phxRequiredFeatures;

    std::vector<fla::SymbolInstance*> symbolInstances; // List of all symbol instances in the document
    std::map<const fla::Symbol*, std::vector<const fla::SymbolInstance*>> symbolInstancesMap; // Map of symbols to their instances

    Document(DOMElement* parent)
        : DOMElement(parent)
    {}

    /// The index in timelines of the scene shown and edited: the one
    /// currentTimeline names, else the first. Animate shows one scene at a
    /// time; the scenes play one after another.
    int currentSceneIndex() const;

    /// The scene shown and edited; null with no scenes.
    Timeline* currentScene() const;

    /// Makes the scene at \a index the one shown and edited.
    void setCurrentScene(int index);

    ~Document() override;

    std::string domTypeName() const override { return "Document"; }

    DOMType domType() const override { return DOMType::Document; }

    static DOMType staticDomType() { return DOMType::Document; }
};

} // namespace fla
