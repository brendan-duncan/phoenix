"""Makes the start screen's starter templates: ready-made documents with art
in them, written as XFL folders under resources/templates/<id>/, each with
template.json (title, summary, order) and thumbnail.png, a frame of it
rendered with no window.

    python tools/build_templates.py [path/to/Phoenix.exe]

The art is drawn here, in code: a bouncing ball keyed with squash and
stretch, a walk cycle rigged with layer parenting, a talking head with a
mouth chart (shapes A to H and X) and a title card. Without Phoenix (or
Pillow) the documents are written and the thumbnails left as they are.
"""

import json
import math
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATES = os.path.join(ROOT, 'resources', 'templates')
THUMB = (240, 135)

XMLNS = 'xmlns="http://ns.adobe.com/xfl/2008/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"'


# --- Paths -----------------------------------------------------------------

def tw(value):
    """Pixels as twips, the unit edges are written in."""
    return str(int(round(value * 20)))


class Path:
    """A closed outline of lines and quadratic curves, in pixels."""

    def __init__(self, x, y):
        self.start = (x, y)
        self.parts = []  # ('L', x, y) or ('Q', cx, cy, x, y)

    def line(self, x, y):
        self.parts.append(('L', x, y))
        return self

    def curve(self, cx, cy, x, y):
        self.parts.append(('Q', cx, cy, x, y))
        return self

    def points(self):
        out = [self.start]
        for part in self.parts:
            out.append(part[-2:])
        return out

    def reversed(self):
        pts = self.points()
        path = Path(*pts[-1])
        for i in range(len(self.parts) - 1, -1, -1):
            part = self.parts[i]
            to = pts[i]
            if part[0] == 'L':
                path.line(*to)
            else:
                path.curve(part[1], part[2], *to)
        return path

    def clockwise(self):
        """The same outline run clockwise on the screen (y down), as a fill
        on its right side wants."""
        pts = self.points()
        area = sum(pts[i][0] * pts[i + 1][1] - pts[i + 1][0] * pts[i][1] for i in range(len(pts) - 1))
        return self if area >= 0 else self.reversed()

    def edges(self):
        text = '!%s %s' % (tw(self.start[0]), tw(self.start[1]))
        for part in self.parts:
            if part[0] == 'L':
                text += '|%s %s' % (tw(part[1]), tw(part[2]))
            else:
                text += '[%s %s %s %s' % tuple(tw(v) for v in part[1:])
        return text


def ellipse(cx, cy, rx, ry):
    """Eight quadratic curves around an ellipse."""
    k = 1.0 / math.cos(math.pi / 8)
    path = Path(cx + rx, cy)
    for i in range(1, 9):
        a = math.pi / 4 * i
        m = a - math.pi / 8
        path.curve(cx + rx * k * math.cos(m), cy + ry * k * math.sin(m), cx + rx * math.cos(a), cy + ry * math.sin(a))
    return path


def circle(cx, cy, r):
    return ellipse(cx, cy, r, r)


def polygon(points):
    path = Path(*points[0])
    for p in points[1:]:
        path.line(*p)
    path.line(*points[0])
    return path


def rect(x0, y0, x1, y1):
    return polygon([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])


def rounded(x0, y0, x1, y1, r):
    path = Path(x0 + r, y0)
    path.line(x1 - r, y0).curve(x1, y0, x1, y0 + r)
    path.line(x1, y1 - r).curve(x1, y1, x1 - r, y1)
    path.line(x0 + r, y1).curve(x0, y1, x0, y1 - r)
    path.line(x0, y0 + r).curve(x0, y0, x0 + r, y0)
    return path


def capsule(length, r):
    """A limb from a joint at (0, 0) straight down to (0, length), round at
    both ends."""
    path = Path(-r, 0)
    path.line(-r, length)
    path.curve(-r, length + r, 0, length + r).curve(r, length + r, r, length)
    path.line(r, 0)
    path.curve(r, -r, 0, -r).curve(-r, -r, -r, 0)
    return path


def lens(x0, x1, cy, top, bottom):
    """A mouth opening: two curves between the corners, bowed up by top
    and down by bottom."""
    path = Path(x0, cy)
    path.curve((x0 + x1) / 2, cy - top * 2, x1, cy)
    path.curve((x0 + x1) / 2, cy + bottom * 2, x0, cy)
    return path


# --- Elements --------------------------------------------------------------

def color_xml(color):
    if isinstance(color, tuple):
        return '<SolidColor color="%s" alpha="%s"/>' % color
    return '<SolidColor color="%s"/>' % color


def shape(path, fill=None, stroke=None, weight=3):
    """A drawing object: path filled and outlined."""
    fills = '<fills><FillStyle index="1">%s</FillStyle></fills>' % color_xml(fill) if fill else ''
    strokes = ''
    if stroke:
        strokes = ('<strokes><StrokeStyle index="1"><SolidStroke weight="%s" scaleMode="normal" caps="round" '
                   'joints="round"><fill>%s</fill></SolidStroke></StrokeStyle></strokes>') % (weight, color_xml(stroke))
    edge = '<Edge%s%s edges="%s"/>' % (' fillStyle1="1"' if fill else '', ' strokeStyle="1"' if stroke else '',
                                      path.clockwise().edges())
    return '<DOMShape isDrawingObject="true">%s%s<edges>%s</edges></DOMShape>' % (fills, strokes, edge)


def line_shape(points, stroke, weight=3, curve=None):
    """An open line through points (a curve through curve's control
    point when given)."""
    text = '!%s %s' % (tw(points[0][0]), tw(points[0][1]))
    if curve:
        text += '[%s %s %s %s' % (tw(curve[0]), tw(curve[1]), tw(points[1][0]), tw(points[1][1]))
    else:
        for p in points[1:]:
            text += '|%s %s' % (tw(p[0]), tw(p[1]))
    return ('<DOMShape isDrawingObject="true"><strokes><StrokeStyle index="1"><SolidStroke weight="%s" '
            'scaleMode="normal" caps="round" joints="round"><fill>%s</fill></SolidStroke></StrokeStyle></strokes>'
            '<edges><Edge strokeStyle="1" edges="%s"/></edges></DOMShape>') % (weight, color_xml(stroke), text)


def matrix(tx=0.0, ty=0.0, rotate=0.0, sx=1.0, sy=1.0):
    r = math.radians(rotate)
    a, b = sx * math.cos(r), sx * math.sin(r)
    c, d = -sy * math.sin(r), sy * math.cos(r)
    attrs = ''
    for name, value, default in (('a', a, 1), ('b', b, 0), ('c', c, 0), ('d', d, 1), ('tx', tx, 0), ('ty', ty, 0)):
        if abs(value - default) > 1e-6:
            attrs += ' %s="%s"' % (name, ('%.6f' % value).rstrip('0').rstrip('.'))
    return '<matrix><Matrix%s/></matrix>' % attrs


def instance(name, tx=0.0, ty=0.0, rotate=0.0, sx=1.0, sy=1.0, alpha=None, extra=''):
    color = '<color><Color alphaMultiplier="%s"/></color>' % alpha if alpha is not None else ''
    return ('<DOMSymbolInstance libraryItemName="%s" symbolType="graphic" loop="loop"%s>%s'
            '<transformationPoint><Point/></transformationPoint>%s</DOMSymbolInstance>') % (
        name, extra, matrix(tx, ty, rotate, sx, sy), color)


def text(words, x, y, width, size, color, face='ArialMT', bold=False, animators=None):
    height = size * 1.3
    attrs = ' phoenixTextAnimators="%s"' % animators.replace('&', '&amp;') if animators else ''
    return ('<DOMStaticText%s left="0" top="0" width="%s" height="%s">%s<textRuns><DOMTextRun><characters>%s'
            '</characters><textAttrs><DOMTextAttrs face="%s" size="%s" fillColor="%s" alignment="center"%s/>'
            '</textAttrs></DOMTextRun></textRuns></DOMStaticText>') % (
        attrs, width, height, matrix(x, y), words, face, size, color, ' bold="true"' if bold else '')


def frame(index, duration, elements, tween=False, ease=None, label=None, extra=''):
    attrs = ' index="%d"' % index
    if duration > 1:
        attrs += ' duration="%d"' % duration
    attrs += ' keyMode="%d"' % (22017 if tween else 9728)
    if tween:
        attrs += ' tweenType="motion" motionTweenSnap="true"'
    if label:
        attrs += ' name="%s" labelType="name"' % label
    attrs += extra
    tweens = '<tweens><Ease target="all" method="%s"/></tweens>' % ease if tween and ease else ''
    return '<DOMFrame%s>%s<elements>%s</elements></DOMFrame>' % (attrs, tweens, ''.join(elements))


def layer(name, frames, color='#4F80FF', extra=''):
    return '<DOMLayer name="%s" color="%s"%s><frames>%s</frames></DOMLayer>' % (name, color, extra, ''.join(frames))


def keyed(keys, length, make, ease=None, extra=''):
    """Frames from keys, (frame, args) pairs in order: a classic tween
    from each key to the next, the last held to length; each key's
    elements made by make(args). ease is a method, or a function of
    the key's index giving one."""
    frames = []
    for i, (at, args) in enumerate(keys):
        last = i + 1 == len(keys)
        end = length if last else keys[i + 1][0]
        method = ease(i) if callable(ease) else ease
        frames.append(frame(at, end - at, [make(args)], tween=not last, ease=method, extra=extra))
    return frames


def write_symbol(folder, name, layers, attributes=''):
    os.makedirs(os.path.join(folder, 'LIBRARY'), exist_ok=True)
    xml = ('<?xml version="1.0" encoding="UTF-8"?>\n<DOMSymbolItem %s name="%s" symbolType="graphic"%s>'
           '<timeline><DOMTimeline name="%s"><layers>%s</layers></DOMTimeline></timeline></DOMSymbolItem>\n') % (
        XMLNS, name, attributes, name, ''.join(layers))
    with open(os.path.join(folder, 'LIBRARY', name + '.xml'), 'w', encoding='utf-8', newline='\n') as out:
        out.write(xml)


def write_document(folder, symbols, layers, background='#FFFFFF', width=1280, height=720, fps=24):
    includes = ''.join('<Include href="%s.xml"/>' % name for name in symbols)
    xml = ('<?xml version="1.0" encoding="UTF-8"?>\n<DOMDocument %s width="%d" height="%d" frameRate="%d" '
           'backgroundColor="%s" creatorInfo="Phoenix"><symbols>%s</symbols><timelines><DOMTimeline name="Scene 1">'
           '<layers>%s</layers></DOMTimeline></timelines></DOMDocument>\n') % (
        XMLNS, width, height, fps, background, includes, ''.join(layers))
    with open(os.path.join(folder, 'DOMDocument.xml'), 'w', encoding='utf-8', newline='\n') as out:
        out.write(xml)


def art(name, elements):
    """A one-frame symbol of drawings."""
    return name, [layer('Art', [frame(0, 1, elements)])]


def fresh(template_id, title, summary, order, thumbnail_frame):
    folder = os.path.join(TEMPLATES, template_id)
    if os.path.isdir(folder):
        for entry in os.listdir(folder):
            if entry != 'thumbnail.png':
                path = os.path.join(folder, entry)
                shutil.rmtree(path) if os.path.isdir(path) else os.remove(path)
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, 'template.json'), 'w', encoding='utf-8', newline='\n') as out:
        json.dump({'title': title, 'summary': summary, 'order': order, 'thumbnailFrame': thumbnail_frame},
                  out, indent=2)
        out.write('\n')
    return folder


# --- Bouncing ball -----------------------------------------------------------

def bouncing_ball():
    folder = fresh('bouncing-ball', 'Bouncing Ball',
                   'A ball bouncing in place, a one-second cycle: it eases into the floor, squashes, '
                   'stretches as it leaves and slows at the top. Its shadow grows as it falls.', 1, 11)
    # The ball's registration point is its bottom, so it squashes on the floor.
    write_symbol(folder, 'Ball', [layer('Art', [frame(0, 1, [
        shape(circle(0, -60, 60), '#E8483F', '#8C1D18', 3),
        shape(ellipse(-20, -86, 16, 10), ('#FFFFFF', '0.55')),
    ])])])
    write_symbol(folder, 'Shadow', [layer('Art', [frame(0, 1, [shape(ellipse(0, 0, 62, 11), ('#000000', '0.22'))])])])
    # (frame, height of the ball's bottom, width, height scale)
    keys = [(0, 210, 1.0, 1.0), (10, 556, 0.88, 1.16), (12, 562, 1.32, 0.72), (14, 545, 0.9, 1.12), (24, 210, 1.0, 1.0)]
    eases = {0: 'quadIn', 3: 'quadOut'}
    ball = keyed([(k[0], k) for k in keys], 25, lambda k: instance('Ball', 640, k[1], 0, k[2], k[3]),
                 ease=lambda i: eases.get(i))
    shadow_scale = {0: 0.45, 10: 0.95, 12: 1.15, 14: 0.95, 24: 0.45}
    shadow = keyed([(k[0], k) for k in keys], 25,
                   lambda k: instance('Shadow', 640, 566, 0, shadow_scale[k[0]], shadow_scale[k[0]]),
                   ease=lambda i: eases.get(i))
    floor = [frame(0, 25, [shape(rect(0, 560, 1280, 720), '#7BB661'), shape(rect(0, 560, 1280, 572), '#93C878')])]
    write_document(folder, ['Ball', 'Shadow'], [
        layer('Ball', ball, '#FF4F4F', ' current="true" isSelected="true"'),
        layer('Shadow', shadow, '#4FFF4F'),
        layer('Floor', floor, '#9933CC'),
    ], background='#CFE8F5')


# --- Walk cycle -------------------------------------------------------------

def walk_cycle():
    folder = fresh('walk-cycle', 'Walk Cycle',
                   'A character walking in place, a one-second cycle keyed every 6 frames. Each limb is its own '
                   'layer parented to the one it hangs from: turn a thigh and the shin and shoe go with it.', 2, 0)
    skin, shirt, shirt_back = '#F2C9A0', '#3E7CB1', '#2F5F8A'
    pants, pants_back, shoe, line = '#44475A', '#2E3040', '#262626', '#3A2A20'
    hair = '#4A2E1E'

    hair_cap = Path(-34, -146).curve(-38, -190, 4, -188).curve(44, -186, 40, -150).curve(22, -170, -2, -166)
    hair_cap.curve(-22, -164, -34, -146)
    body = [
        shape(rect(-7, -118, 7, -98), skin),
        shape(rounded(-21, -106, 21, -2, 10), shirt, line, 2),
        shape(ellipse(0, -4, 22, 12), pants, line, 2),
        shape(circle(4, -148, 36), skin, line, 2),
        shape(hair_cap, hair),
        shape(circle(-28, -146, 8), skin, line, 2),
        shape(circle(22, -152, 4.5), '#1E1E1E'),
        shape(circle(41, -140, 6), '#E8B088'),
        line_shape([(22, -128), (34, -130)], line, 2, curve=(28, -125)),
    ]
    symbols = [art('Body', body)]
    for suffix, cloth, cloth_dark, trouser in (('Front', shirt, shirt_back, pants), ('Back', shirt_back, shirt_back, pants_back)):
        symbols.append(art(suffix + ' Thigh', [shape(capsule(72, 13), trouser, line, 2)]))
        symbols.append(art(suffix + ' Shin', [shape(capsule(68, 11), trouser, line, 2),
                                              shape(rounded(-13, 66, 34, 84, 8), shoe, line, 2)]))
        symbols.append(art(suffix + ' Upper Arm', [shape(capsule(50, 10), cloth, line, 2)]))
        symbols.append(art(suffix + ' Forearm', [shape(capsule(42, 8), skin if suffix == 'Front' else '#DDB28A', line, 2),
                                                 shape(circle(0, 48, 9), skin if suffix == 'Front' else '#DDB28A', line, 2)]))
    for name, layers in symbols:
        write_symbol(folder, name, layers)

    frames_at = [0, 6, 12, 18, 24]
    poses = {
        'Body': [404, 385, 404, 385, 404],
        'Front Thigh': [-24, 2, 22, -14, -24],
        'Front Shin': [4, 2, 6, 50, 4],
        'Back Thigh': [22, -14, -24, 2, 22],
        'Back Shin': [6, 50, 4, 2, 6],
        'Front Upper Arm': [18, 0, -20, 0, 18],
        'Front Forearm': [-10, -15, -30, -15, -10],
        'Back Upper Arm': [-20, 0, 18, 0, -20],
        'Back Forearm': [-30, -15, -10, -15, -30],
    }
    # Where each part hangs in its parent, and the parent's rigging number.
    joints = {
        'Front Thigh': (4, -2, 0), 'Back Thigh': (-4, -2, 0),
        'Front Shin': (0, 72, 1), 'Back Shin': (0, 72, 2),
        'Front Upper Arm': (2, -98, 0), 'Back Upper Arm': (-2, -98, 0),
        'Front Forearm': (0, 50, 3), 'Back Forearm': (0, 50, 4),
    }
    rigging = {'Body': 0, 'Front Thigh': 1, 'Back Thigh': 2, 'Front Upper Arm': 3, 'Back Upper Arm': 4}
    order = ['Front Forearm', 'Front Upper Arm', 'Front Shin', 'Front Thigh', 'Body', 'Back Thigh', 'Back Shin',
             'Back Upper Arm', 'Back Forearm']
    layers = []
    for name in order:
        values = poses[name]
        if name == 'Body':
            make = lambda v, n=name: instance(n, 640, v, 3, 1.4, 1.4)
            extra = ''
        else:
            x, y, parent = joints[name]
            make = lambda v, n=name, x=x, y=y: instance(n, x, y, v)
            extra = ' parentLayerIndex="%d"' % parent
        frames = keyed(list(zip(frames_at, values)), 25, make, ease='sineInOut', extra=extra)
        attrs = ' layerRiggingIndex="%d"' % rigging[name] if name in rigging else ''
        if name == 'Body':
            attrs += ' current="true" isSelected="true"'
        layers.append(layer(name, frames, '#FF8000' if name == 'Body' else '#4FFFFF', attrs))
    ground = [frame(0, 25, [shape(rect(0, 606, 1280, 720), '#9CC98A'), shape(ellipse(655, 609, 110, 11), ('#000000', '0.18'))])]
    layers.append(layer('Ground', ground, '#9933CC'))
    write_document(folder, [name for name, _ in symbols], layers, background='#F4EEDC')


# --- Lip-sync head ----------------------------------------------------------

def lip_sync_head():
    folder = fresh('lip-sync-head', 'Lip-Sync Head',
                   'A talking head whose mouth is a graphic with the nine mouth shapes, A to H and X, one a frame '
                   'and labeled. Import a voice recording, select the mouth and choose Modify > Lip Sync.', 3, 0)
    lip, inside, teeth, tongue = '#8E3B46', '#4A1622', '#FFFFFF', '#E07A86'

    def opening(top, bottom, width=48, teeth_top=True, tongue_low=True, tongue_up=False):
        parts = [shape(lens(-width, width, 0, top, bottom), inside, lip, 4)]
        if teeth_top:
            parts.append(shape(lens(-width * 0.7, width * 0.7, -top * 0.55, top * 0.45, -top * 0.15), teeth))
        if tongue_low and bottom > 10:
            parts.append(shape(ellipse(0, bottom * 0.6, width * 0.45, bottom * 0.3), tongue))
        if tongue_up:
            parts.append(shape(ellipse(0, -top * 0.15, width * 0.35, top * 0.3), tongue))
        return parts

    shapes = [
        ('A', [shape(lens(-46, 46, 0, 3, 4), lip)]),
        ('B', [shape(lens(-48, 48, 0, 9, 9), inside, lip, 4),
               shape(rect(-34, -6, 34, 6), teeth)]),
        ('C', opening(16, 20)),
        ('D', opening(24, 34, 52)),
        ('E', [shape(ellipse(0, 4, 30, 22), inside, lip, 5), shape(ellipse(0, 16, 16, 7), tongue)]),
        ('F', [shape(ellipse(0, 2, 15, 15), inside, lip, 7)]),
        ('G', [shape(lens(-44, 44, 0, 10, 8), inside, lip, 4),
               shape(lens(-30, 30, -2, 8, 3), teeth),
               line_shape([(-30, 6), (30, 6)], lip, 5, curve=(0, 2))]),
        ('H', opening(18, 24, 48, True, False, True)),
        ('X', [line_shape([(-40, -2), (40, -2)], lip, 5, curve=(0, 10))]),
    ]
    frames = [frame(i, 1, parts, label=name) for i, (name, parts) in enumerate(shapes)]
    write_symbol(folder, 'Mouth', [layer('Shapes', frames, '#000000')],
                 ' phoenixMouthChart="A=1;B=2;C=3;D=4;E=5;F=6;G=7;H=8;X=9"')

    skin, line, hair = '#F5CBA7', '#5A3A2A', '#7A4A2A'
    hair_top = Path(440, 310).curve(430, 110, 640, 105).curve(850, 110, 840, 310)
    hair_top.curve(790, 210, 640, 200).curve(490, 210, 440, 310)
    face = [
        shape(ellipse(445, 345, 30, 42), skin, line, 3),
        shape(ellipse(835, 345, 30, 42), skin, line, 3),
        shape(ellipse(640, 340, 200, 225), skin, line, 4),
        shape(hair_top, hair, line, 3),
        shape(ellipse(565, 320, 34, 40), '#FFFFFF', line, 3),
        shape(ellipse(715, 320, 34, 40), '#FFFFFF', line, 3),
        shape(circle(572, 328, 14), '#3B2A1E'),
        shape(circle(708, 328, 14), '#3B2A1E'),
        shape(circle(576, 323, 4), '#FFFFFF'),
        shape(circle(712, 323, 4), '#FFFFFF'),
        line_shape([(525, 262), (600, 262)], line, 6, curve=(562, 246)),
        line_shape([(680, 262), (755, 262)], line, 6, curve=(718, 246)),
        line_shape([(640, 345), (628, 400)], line, 4, curve=(625, 380)),
        line_shape([(628, 400), (652, 402)], line, 4),
        shape(ellipse(520, 420, 32, 18), ('#F08080', '0.35')),
        shape(ellipse(760, 420, 32, 18), ('#F08080', '0.35')),
    ]
    mouth = [frame(0, 48, [instance('Mouth', 640, 462, 0, 1.4, 1.4, extra=' firstFrame="8"').replace(
        'loop="loop"', 'loop="single frame"')])]
    write_document(folder, ['Mouth'], [
        layer('Mouth', mouth, '#FF4F4F', ' current="true" isSelected="true"'),
        layer('Face', [frame(0, 48, face)], '#4FFF4F'),
    ], background='#BFD7EA')


# --- Title card --------------------------------------------------------------

def title_card():
    folder = fresh('title-card', 'Title Card',
                   'A title that drops in letter by letter, a bar that wipes in under it and a line that fades '
                   'up: three seconds to open a film. Double-click the words to change them.', 4, 60)
    accent = '#F2B33D'
    write_symbol(folder, 'Bar', [layer('Art', [frame(0, 1, [shape(rounded(0, -5, 520, 5, 5), accent)])])])
    write_symbol(folder, 'Subtitle', [layer('Words', [frame(0, 1, [
        text('A short line about it', 0, 0, 760, 36, '#C9D6EA')])])])
    bar = keyed([(8, 0.01), (26, 1.0)], 72, lambda s: instance('Bar', 380, 400, 0, s, 1.0), ease='quadOut')
    bar = [frame(0, 8, [])] + bar
    subtitle = keyed([(24, 0), (40, 1)], 72, lambda a: instance('Subtitle', 260, 430, alpha=a), ease='sineOut')
    subtitle = [frame(0, 24, [])] + subtitle
    title = [frame(0, 72, [text('Your Title Here', 140, 270, 1000, 96, '#FFFFFF', bold=True,
                                animators='cascade?delay=0&distance=40&duration=0.4&fade=true&from=Above&stagger=0.05')])]
    backdrop = [frame(0, 72, [
        shape(circle(1110, 120, 260), ('#2E4372', '0.8')),
        shape(circle(160, 660, 200), ('#2A3B62', '0.8')),
        shape(rect(0, 690, 1280, 720), accent),
    ])]
    write_document(folder, ['Bar', 'Subtitle'], [
        layer('Title', title, '#FF4F4F', ' current="true" isSelected="true"'),
        layer('Subtitle', subtitle, '#4FFF4F'),
        layer('Bar', bar, '#4F4FFF'),
        layer('Backdrop', backdrop, '#9933CC'),
    ], background='#1E2A44')


# --- Thumbnails -------------------------------------------------------------

def phoenix_exe(argv):
    for arg in argv:
        if arg.lower().endswith('.exe') or os.path.basename(arg) == 'Phoenix':
            return os.path.abspath(arg)
    for guess in ('build/RelWithDebInfo/Phoenix.exe', 'build/Release/Phoenix.exe', 'build/Phoenix'):
        path = os.path.join(ROOT, guess)
        if os.path.exists(path):
            return path
    return None


def thumbnail(exe, folder, at):
    try:
        from PIL import Image
    except ImportError:
        print('  no Pillow: thumbnail left as it is')
        return
    work = tempfile.mkdtemp(prefix='phoenix-template-')
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
    try:
        subprocess.run([exe, '--export-movie', os.path.join(folder, 'DOMDocument.xml'), os.path.join(work, 'f.png')],
                       env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600, check=False)
        names = sorted(n for n in os.listdir(work) if n.endswith('.png'))
        if not names:
            print('  nothing rendered')
            return
        image = Image.open(os.path.join(work, names[min(at, len(names) - 1)])).convert('RGB')
        image = image.resize(THUMB, Image.LANCZOS)
        image.save(os.path.join(folder, 'thumbnail.png'), optimize=True)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    exe = phoenix_exe(sys.argv[1:])
    for build in (bouncing_ball, walk_cycle, lip_sync_head, title_card):
        build()
    for name in sorted(os.listdir(TEMPLATES)):
        folder = os.path.join(TEMPLATES, name)
        info = json.load(open(os.path.join(folder, 'template.json'), encoding='utf-8'))
        print(name)
        if exe:
            thumbnail(exe, folder, info.get('thumbnailFrame', 0))


if __name__ == '__main__':
    main()
