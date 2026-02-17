#pragma once

#include "folder.h"
#include "resource.h"
#include "timeline.h"
#include "printer_settings.h"
#include "publish_history.h"
#include "scripts.h"
#include "symbol.h"
#include <map>
#include <string>
#include <vector>

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

    std::vector<Resource*> resources;
    std::vector<Symbol*> symbols;
    std::vector<Timeline*> timelines;
    Scripts* scripts = nullptr;
    PublishHistory* publishHistory = nullptr;
    PrinterSettings* printerSettings = nullptr;
    std::vector<Folder*> folders;

    std::map<std::string, Symbol*> symbolMap; // For quick lookup of symbols by name

    Document() = default;

    ~Document() override;

    std::string domTypeName() const override { return "Document"; }
};

} // namespace fla
