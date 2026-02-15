#pragma once

#include "resource.h"
#include "timeline.h"
#include "printer_settings.h"
#include "publish_history.h"
#include "scripts.h"
#include "symbol.h"
#include <map>
#include <string>
#include <vector>


/*
xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" 
xmlns="http://ns.adobe.com/xfl/2008/" 
gridColor="#C0C0C0" 
guidesColor="#00FF00" 
width="800" 
height="550" 
frameRate="12" 
currentTimeline="1" 
xflVersion="23.0"
creatorInfo="Adobe Animate" 
platform="Windows" 
versionInfo="Saved by Animate Windows 24.0 build 3" 
majorVersion="24" 
buildNumber="3" 
gridSpacingX="18" 
gridSpacingY="18" 
snapAlignBorderSpacing="18" 
objectsSnapTo="false"
timelineHeight="197"
timelineLabelWidth="256"
vanishingPoint3DX="400" 
vanishingPoint3DY="275" 
nextSceneIdentifier="2" 
playOptionsPlayLoop="false" 
playOptionsPlayPages="false"
playOptionsPlayFrameActions="false" 
filetypeGUID="DD0DDBBF-5BEF-45B2-9F24-A3048D2A676F" 
fileGUID="3B34B1C2CFEFE745B7B9147CA627619E
*/

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

    std::map<std::string, Symbol*> symbolMap; // For quick lookup of symbols by name

    Document() = default;

    ~Document() override;

    std::string domTypeName() const override { return "Document"; }
};

} // namespace fla
