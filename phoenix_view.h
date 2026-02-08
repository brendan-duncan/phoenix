#pragma once

#include "data/fla_document.h"
#include "data/shape.h"
#include <QWidget>
#include <QPainter>



class PhoenixView : public QWidget
{
    Q_OBJECT
public:
    PhoenixView(QWidget *parent = nullptr);

    ~PhoenixView();

    void setDocument(const FLADocument* document);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    const FLADocument* _flaDocument;

    void drawDocument(QPainter& painter, const Document* document);

    void drawTimeline(QPainter& painter, const Timeline* timeline);

    void drawLayer(QPainter& painter, const Layer* layer);

    void drawFrame(QPainter& painter, const Frame* frame);

    void drawElement(QPainter& painter, const Element* element);

    void drawShape(QPainter& painter, const Shape* shape);
};
