#pragma once

#include <cstdint>

/// The field numbers of docs/phoenix_format.proto, message by message. The
/// .proto is the specification; this mirrors it for the codec, and the two
/// change together. A number here is never changed or reused.
namespace phx::schema {

namespace MediaHeader { enum : uint32_t {
    Id = 1, Kind = 2, Format = 3, SourceName = 4, SourceSize = 5, Width = 6, Height = 7, SampleRate = 8,
    Channels = 9, SampleCount = 10, ContentHash = 11, PackagePath = 12,
}; }

enum MediaKind : uint32_t {
    MediaOther = 0, MediaBitmap = 1, MediaSound = 2, MediaVideo = 3, MediaFont = 4, MediaBrushTexture = 5,
    MediaPackageFile = 6,
};

namespace Metadata { enum : uint32_t { Application = 1, Created = 2, Modified = 3, Author = 4, Values = 5 }; }
namespace KeyValue { enum : uint32_t { Key = 1, Value = 2 }; }
namespace Matrix { enum : uint32_t { A = 1, B = 2, C = 3, D = 4, Tx = 5, Ty = 6 }; }
namespace Vector2 { enum : uint32_t { X = 1, Y = 2 }; }
namespace Rectangle { enum : uint32_t { Left = 1, Top = 2, Right = 3, Bottom = 4 }; }
namespace XflDetail { enum : uint32_t { ExtraAttributes = 1, PresentAttributes = 2, ExtraChildren = 3 }; }

namespace Document { enum : uint32_t {
    Width = 1, Height = 2, FrameRate = 3, BackgroundColor = 4, GridColor = 5, GuidesColor = 6, GridSpacingX = 7,
    GridSpacingY = 8, SnapAlignBorderSpacing = 9, ObjectsSnapTo = 10, GuidesSnapTo = 11, TimelineHeight = 12,
    TimelineLabelWidth = 13, VanishingPointX = 14, VanishingPointY = 15, ViewAngle3D = 16, NextSceneIdentifier = 17,
    PlayLoop = 18, PlayPages = 19, PlayFrameActions = 20, CurrentTimeline = 21, HasSymbolList = 22,
    Scenes = 30, Folders = 31, Bitmaps = 32, Sounds = 33, SymbolOrder = 34, SwatchLists = 35, PackageFiles = 36,
    Features = 40, RequiredFeatures = 41, Xfl = 50, Brushes = 60, Palettes = 61, Videos = 62,
}; }

namespace XflDocument { enum : uint32_t {
    FiletypeGuid = 1, FileGuid = 2, XflVersion = 3, CreatorInfo = 4, Platform = 5, VersionInfo = 6, MajorVersion = 7,
    BuildNumber = 8, ScriptListXml = 9, PublishHistoryXml = 10, PrinterSettingsXml = 11, ExtraSections = 12,
    ExtraMedia = 13, Detail = 14, FromBinary = 15,
}; }

namespace RawSection { enum : uint32_t { Xml = 1, BeforeTimelines = 2 }; }
namespace LibraryFolder { enum : uint32_t { Name = 1, ItemId = 2, IsExpanded = 3, Xfl = 15 }; }

namespace BitmapItem { enum : uint32_t {
    Name = 1, ItemId = 2, SourceExternalFilepath = 3, SourceLastImported = 4, ExternalFileCrc32 = 5,
    ExternalFileSize = 6, AllowSmoothing = 7, Quality = 8, Href = 9, BitmapDataHref = 10, FrameRight = 11,
    FrameBottom = 12, IsJpeg = 13, DataMedia = 14, ImageMedia = 15, Xfl = 16,
}; }

namespace SoundItem { enum : uint32_t { Name = 1, Format = 2, DataPath = 3 }; }
namespace PackageFile { enum : uint32_t { Path = 1, Media = 2 }; }
namespace SwatchList { enum : uint32_t { Name = 1, Colors = 2, Xml = 3 }; }

namespace Symbol { enum : uint32_t {
    Name = 1, ItemId = 2, LastModified = 3, Href = 4, SymbolType = 5, OtherSymbolType = 6, IncludeAttributes = 7,
    Scale9 = 8, ScaleGrid = 9, Timelines = 10, Xfl = 15,
}; }

namespace Timeline { enum : uint32_t {
    Name = 1, LayerDepthEnabled = 2, GuidesX = 3, GuidesY = 4, Layers = 5, ExtraChildren = 6, Xfl = 15,
}; }

namespace Layer { enum : uint32_t {
    Name = 1, Color = 2, Current = 3, LayerType = 4, OtherLayerType = 5, ParentLayerIndex = 6, AutoNamed = 7,
    Selected = 8, Locked = 9, Outline = 10, Visible = 11, Frames = 12, ExtraChildren = 13, Xfl = 15,
}; }

namespace Frame { enum : uint32_t {
    Index = 1, Duration = 2, KeyMode = 3, Name = 4, LabelType = 5, TweenType = 6, MotionTweenSnap = 7,
    MotionTweenSync = 8, Acceleration = 9, HasCustomEase = 10, CustomEase = 11, CustomEaseName = 12, HasEase = 13,
    EaseMethod = 14, OtherEases = 15, MotionTweenRotate = 16, MotionTweenRotateTimes = 17, OtherTweenType = 18,
    ExtraChildren = 19, ActionScript = 20, MorphShape = 21, MorphSignature = 22, Elements = 23, Visible = 24,
    Xfl = 25, ActionScriptXfl = 26,
}; }

namespace Element { enum : uint32_t {
    Matrix = 1, TransformationPoint = 2, IsLocked = 3, IsSelected = 4, IsFloating = 5, KeepMatrix = 6,
    KeepTransformationPoint = 7, Visible = 8, ExtraChildren = 9, LocalBounds = 10, Xfl = 15,
    Shape = 20, Group = 21, SymbolInstance = 22, BitmapInstance = 23, Text = 24, Rectangle = 25, Oval = 26, Raw = 27,
    VideoInstance = 28,
}; }

namespace Shape { enum : uint32_t {
    Fills = 1, Strokes = 2, Edges = 3, IsDrawingObject = 4, CubicEdges = 5, ReadEdgeCount = 6,
}; }
namespace IndexedStyle { enum : uint32_t { Index = 1, Style = 2 }; }
namespace CubicRecord { enum : uint32_t { BeforeEdge = 1, Xml = 2 }; }
namespace Edge { enum : uint32_t { FillStyle0 = 1, FillStyle1 = 2, StrokeStyle = 3, Paths = 4, Xfl = 15 }; }
namespace Path { enum : uint32_t {
    StyleIndex = 1, LineStyleIndex = 2, FillStyleIndex = 3, Segments = 4, Coordinates = 5,
}; }
namespace Group { enum : uint32_t { Members = 1 }; }

namespace SymbolInstance { enum : uint32_t {
    LibraryItemName = 1, Name = 2, FirstFrame = 3, SymbolType = 4, LoopType = 5, ColorTransform = 6, BlendMode = 7,
    OtherSymbolType = 8, KeepColor = 9, ExtraFilters = 10, Filters = 11, LastFrame = 12,
}; }

namespace ColorTransform { enum : uint32_t {
    TintMultiplier = 1, TintColor = 2, Brightness = 3, RedMultiplier = 4, GreenMultiplier = 5, BlueMultiplier = 6,
    AlphaMultiplier = 7, RedOffset = 8, GreenOffset = 9, BlueOffset = 10, AlphaOffset = 11,
}; }

namespace Filter { enum : uint32_t {
    Type = 1, Enabled = 2, BlurX = 3, BlurY = 4, Quality = 5, Angle = 6, Distance = 7, Strength = 8, Color = 9,
    HighlightColor = 10, ShadowColor = 11, Inner = 12, Knockout = 13, HideObject = 14, BevelType = 15,
    Brightness = 16, Contrast = 17, Saturation = 18, Hue = 19, Entries = 20, Xfl = 21,
}; }

namespace BitmapInstance { enum : uint32_t { LibraryItemName = 1 }; }

namespace Text { enum : uint32_t {
    Left = 1, Top = 2, Width = 3, Height = 4, AutoExpand = 5, TextType = 6, InstanceName = 7, LineType = 8, Runs = 9,
}; }

namespace TextRun { enum : uint32_t {
    Text = 1, Alignment = 2, AliasText = 3, AutoKern = 4, LetterSpacing = 5, LineHeight = 6, Size = 7,
    BitmapSize = 8, Face = 9, FillColor = 10, Xfl = 15,
}; }

namespace RectanglePrimitive { enum : uint32_t {
    Rect = 1, Fill = 2, Stroke = 3, TopLeftRadius = 4, TopRightRadius = 5, BottomLeftRadius = 6, BottomRightRadius = 7,
}; }
namespace OvalPrimitive { enum : uint32_t {
    Rect = 1, Fill = 2, Stroke = 3, StartAngle = 4, EndAngle = 5, InnerRadius = 6,
}; }
namespace RawXml { enum : uint32_t { Xml = 1 }; }

namespace Fill { enum : uint32_t {
    Matrix = 1, SpreadMethod = 2, InterpolationMethod = 3, Xfl = 15,
    Solid = 20, LinearGradient = 21, RadialGradient = 22, Bitmap = 23,
}; }
namespace Gradient { enum : uint32_t { Stops = 1, FocalPointRatio = 2 }; }
namespace GradientStop { enum : uint32_t { Ratio = 1, Color = 2 }; }
namespace BitmapFill { enum : uint32_t { BitmapPath = 1, Clipped = 2 }; }

namespace Stroke { enum : uint32_t {
    Kind = 1, Weight = 2, ScaleMode = 3, SolidStyle = 4, Caps = 5, Joints = 6, MiterLimit = 7, PixelHinting = 8,
    Dash1 = 9, Dash2 = 10, DotSpace = 11, StyleAttributes = 12, ExtraChildren = 13, WidthProfile = 14, Fill = 15,
    Xfl = 16, Brush = 30,
}; }
namespace WidthMarker { enum : uint32_t { Position = 1, Left = 2, Right = 3, Corner = 4 }; }

namespace MorphShape { enum : uint32_t { Segments = 1, HintsXml = 2, Xfl = 15 }; }
namespace MorphSegment { enum : uint32_t {
    StartA = 1, StartB = 2, StrokeIndex1 = 3, StrokeIndex2 = 4, FillIndex1 = 5, FillIndex2 = 6, Curves = 7, Xfl = 15,
}; }
namespace MorphCurve { enum : uint32_t { ControlA = 1, AnchorA = 2, ControlB = 3, AnchorB = 4, IsLine = 5, Xfl = 15 }; }

} // namespace phx::schema
