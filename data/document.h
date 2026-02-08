#pragma once

#include "timeline.h"
#include "symbol.h"
#include <map>
#include <string>
#include <vector>

class Document : public DOMElement
{
public:
    std::string source;

    double width;
    double height;
    double frameRate;
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
    std::vector<Symbol*> symbols;
    std::vector<Timeline*> timelines;

    std::map<std::string, Symbol*> symbolMap; // For quick lookup of symbols by name

    Document();

    ~Document() override;
};
