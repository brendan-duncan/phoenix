#pragma once

#include "../edit/swf_movie.h"
#include "../util/swf_writer.h"

#include <QByteArray>
#include <QImage>
#include <QTransform>

#include <map>

struct RecordedItem;
class PhoenixView;

/// What Phoenix paints as SWF shapes and bitmaps: each fill or image
/// recorded (RecordedItem) as a shape of one fill, written once however
/// often it is placed.
class SwfArt
{
public:
    explicit SwfArt(swf::Writer& writer);

    /// \a item's shape; 0 when it covers nothing. It is placed with
    /// \a alpha (a solid color or a gradient carries its own, 1 then) and
    /// \a blend.
    int shapeOf(const RecordedItem& item, double& alpha, swf::Writer::Blend& blend);

private:
    swf::Writer& _writer;
    std::map<QByteArray, int> _shapes;
    std::map<QByteArray, int> _bitmaps;

    int bitmapOf(const QImage& image);
};

swf::Matrix swfMatrix(const QTransform& transform);

/// Draws a document's layers for a structured SWF (fla::swfmovie) as the
/// view draws them.
class SwfViewDrawer : public fla::swfmovie::Drawer
{
public:
    SwfViewDrawer(PhoenixView& view, SwfArt& art);

    std::vector<fla::swfmovie::Piece> layerFrame(const fla::Layer& layer, int frame, swf::Writer& writer) override;

private:
    PhoenixView& _view;
    SwfArt& _art;
};
