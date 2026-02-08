#pragma once

#include "dom_element.h"
#include "solid_color.h"
#include "fill_style.h"
#include <string>

class Stroke
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
};

class DashedStroke : public Stroke
{
public:
    DashedStroke();
};

class RaggedStroke : public Stroke
{
public:
    RaggedStroke();
};

class StippleStroke : public Stroke
{
public:
    StippleStroke();
};

class StrokeStyle : public DOMElement
{
public:
    int index;

    Stroke* stroke;

    StrokeStyle();

    ~StrokeStyle() override;
};
