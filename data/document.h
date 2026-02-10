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

class Document : public DOMElement
{
public:
    std::string source;

    int width = 550;
    int height = 400;
    double frameRate = 24.0;
    std::string currentTimeline;
    std::string xflVersion;
    std::string creatorInfo;
    std::string platform;
    std::string versionInfo;
    std::string majorVersion;
    std::string buildNumber;
    std::string viewAngle3D;
    std::string vanishingPoint3DX;
    std::string vanishingPoint3DY;
    std::string nextSceneIdentifier;
    std::string playOptionsPlayLoop;
    std::string playOptionsPlayPages;
    std::string playOptionsPlayFrameActions;
    std::string filetypeGUID;
    std::string fileGUID;

    // Child elements
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
