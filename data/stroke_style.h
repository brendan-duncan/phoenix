#pragma once

#include "dom_element.h"
#include "solid_color.h"
#include "fill_style.h"
#include <string>

namespace fla {

class Stroke : public DOMElement
{
public:
    double weight;
    std::string scaleMode;
    std::string style;

    FillStyle* fill;

    Stroke();

    ~Stroke();
};

class SolidStroke : public Stroke
{
public:
    SolidStroke();

    std::string domTypeName() const override { return "SolidStroke"; }
};

class DashedStroke : public Stroke
{
public:
    DashedStroke();

    std::string domTypeName() const override { return "DashedStroke"; }
};

class RaggedStroke : public Stroke
{
public:
    RaggedStroke();

    std::string domTypeName() const override { return "RaggedStroke"; }
};

class StippleStroke : public Stroke
{
public:
    StippleStroke();

    std::string domTypeName() const override { return "StippleStroke"; }
};

class DottedStroke : public Stroke
{
public:
    DottedStroke();

    std::string domTypeName() const override { return "DottedStroke"; }
};

class StrokeStyle : public DOMElement
{
public:
    int index;

    Stroke* stroke;

    StrokeStyle();

    ~StrokeStyle() override;

    std::string domTypeName() const override { return "StrokeStyle"; }
};

} // namespace fla
