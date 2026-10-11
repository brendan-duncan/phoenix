#pragma once

#include "folder.h"
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
    std::string currentTimeline;
    std::string xflVersion;
    std::string creatorInfo;
    std::string platform;
    std::string versionInfo;
    int majorVersion = 0;
    int buildNumber = 0;
    int viewAngle3D = 0;
    uint8_t backgroundColor[4] = {255, 255, 255, 255};
    uint8_t gridColor[4] = {0, 0, 0, 255};
    uint8_t guidesColor[4] = {0, 255, 0, 255};
    int gridSpacingX = 18;
    int gridSpacingY = 18;
    int snapAlignBorderSpacing = 18;
    bool objectsSnapTo = false;
    int timelineHeight = 197;
    int timelineLabelWidth = 256;
    int vanishingPoint3DX = 400;
    int vanishingPoint3DY = 275;
    int nextSceneIdentifier = 2;
    bool playOptionsPlayLoop = false;
    bool playOptionsPlayPages = false;
    bool playOptionsPlayFrameActions = false;

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
    std::vector<std::pair<std::string, std::vector<uint8_t>>> extraFiles;

    std::vector<fla::SymbolInstance*> symbolInstances; // List of all symbol instances in the document
    std::map<const fla::Symbol*, std::vector<const fla::SymbolInstance*>> symbolInstancesMap; // Map of symbols to their instances

    Document(DOMElement* parent)
        : DOMElement(parent)
    {}

    ~Document() override;

    std::string domTypeName() const override { return "Document"; }

    DOMType domType() const override { return DOMType::Document; }

    static DOMType staticDomType() { return DOMType::Document; }
};

} // namespace fla
