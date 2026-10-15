#include "behavior_engine.h"

namespace fla::behaviors {

// The behaviors Phoenix comes with, in the same form as anyone's own: a
// behavior file in the Behaviors folder can stand in for one of these by
// using its id as its file name.
const std::vector<std::pair<std::string, std::string>>& Engine::builtInSources()
{
    static const std::vector<std::pair<std::string, std::string>> sources = {
        {"wiggle", R"JS(
behavior({
    name: "Wiggle",
    description: "Shakes it about at random, smoothly: its place, turn and size.",
    params: {
        frequency: { label: "Wiggles per second", value: 2, min: 0, max: 30, step: 0.1 },
        position: { label: "Move (pixels)", value: 10, min: 0, max: 500, step: 1 },
        rotation: { label: "Turn (degrees)", value: 0, min: 0, max: 180, step: 1 },
        scale: { label: "Size (percent)", value: 0, min: 0, max: 100, step: 1 },
        seed: { label: "Seed", type: "seed", value: 1 }
    },
    evaluate: function (ctx, p) {
        var t = ctx.time * p.frequency;
        // Two octaves, so it does not look like one slow wave.
        function wobble(n) { return (ctx.noise(t, n) * 2 + ctx.noise(t * 2.3, n + 50)) / 3; }
        return {
            x: wobble(1) * p.position,
            y: wobble(2) * p.position,
            rotation: wobble(3) * p.rotation,
            scale: 1 + wobble(4) * p.scale / 100
        };
    }
});
)JS"},
        {"oscillate", R"JS(
behavior({
    name: "Oscillate",
    description: "Swings back and forth evenly: up and down, side to side, turning, growing or fading.",
    params: {
        motion: { label: "Motion", type: "choice", choices: ["Up and down", "Side to side", "Turn", "Size", "Fade"] },
        amount: { label: "Amount (pixels, degrees or percent)", value: 20, min: 0, max: 1000, step: 1 },
        period: { label: "Seconds per swing", value: 1, min: 0.05, max: 60, step: 0.05 },
        phase: { label: "Start at (degrees)", type: "angle", value: 0, min: 0, max: 360, step: 1 }
    },
    evaluate: function (ctx, p) {
        var wave = Math.sin(2 * Math.PI * ctx.time / p.period + p.phase * Math.PI / 180);
        switch (p.motion) {
        case "Side to side": return { x: wave * p.amount };
        case "Turn": return { rotation: wave * p.amount };
        case "Size": return { scale: 1 + wave * p.amount / 100 };
        case "Fade": return { alpha: Math.max(0, 1 - (wave + 1) / 2 * Math.min(p.amount, 100) / 100) };
        default: return { y: wave * p.amount };
        }
    }
});
)JS"},
        {"spin", R"JS(
behavior({
    name: "Spin",
    description: "Turns steadily about its transformation point.",
    params: {
        speed: { label: "Degrees per second", type: "angle", value: 90, min: -3600, max: 3600, step: 1 }
    },
    evaluate: function (ctx, p) {
        return { rotation: p.speed * (ctx.frame - ctx.start) / ctx.fps };
    }
});
)JS"},
        {"pulse", R"JS(
behavior({
    name: "Pulse",
    description: "Grows and shrinks like a heartbeat.",
    params: {
        rate: { label: "Beats per second", value: 1.2, min: 0.05, max: 10, step: 0.05 },
        amount: { label: "Grows by (percent)", value: 15, min: 0, max: 200, step: 1 }
    },
    evaluate: function (ctx, p) {
        var phase = (ctx.time * p.rate) % 1;
        // A quick swell, then a slower ease back.
        var beat = phase < 0.15 ? Math.sin(phase / 0.15 * Math.PI / 2) : Math.pow(1 - (phase - 0.15) / 0.85, 3);
        return { scale: 1 + beat * p.amount / 100 };
    }
});
)JS"},
        {"fade", R"JS(
behavior({
    name: "Fade In and Out",
    description: "Fades in at the start of its keyframes and out at their end.",
    params: {
        fadeIn: { label: "Fade in (frames)", value: 12, min: 0, max: 600, step: 1 },
        fadeOut: { label: "Fade out (frames)", value: 12, min: 0, max: 600, step: 1 }
    },
    evaluate: function (ctx, p) {
        var alpha = 1;
        if (p.fadeIn > 0)
            alpha = Math.min(alpha, (ctx.frame - ctx.start + 1) / p.fadeIn);
        if (p.fadeOut > 0)
            alpha = Math.min(alpha, (ctx.end - ctx.frame + 1) / p.fadeOut);
        return { alpha: Math.max(0, Math.min(1, alpha)) };
    }
});
)JS"},
        {"blink", R"JS(
behavior({
    name: "Blink",
    description: "Disappears and comes back, on a beat or at random.",
    params: {
        rate: { label: "Blinks per second", value: 2, min: 0.05, max: 30, step: 0.05 },
        shown: { label: "Shown (percent of the time)", value: 50, min: 0, max: 100, step: 1 },
        random: { label: "At random", type: "checkbox", value: false },
        seed: { label: "Seed", type: "seed", value: 1 }
    },
    evaluate: function (ctx, p) {
        var beat = ctx.time * p.rate;
        var on = p.random ? ctx.random(Math.floor(beat)) < p.shown / 100 : beat % 1 < p.shown / 100;
        return { alpha: on ? 1 : 0 };
    }
});
)JS"},
        {"circle", R"JS(
behavior({
    name: "Repeat in a Circle",
    description: "Copies it round a circle about where it is, each turned to follow the circle if you like. Key the radius, the number of copies or the angles to animate the ring.",
    params: {
        count: { label: "Copies", value: 8, min: 1, max: 360, step: 1 },
        radius: { label: "Radius (pixels)", value: 100, min: 0, max: 4000, step: 1 },
        start: { label: "Start angle (degrees)", type: "angle", value: 0, min: -720, max: 720, step: 1 },
        sweep: { label: "Sweep (degrees)", type: "angle", value: 360, min: -720, max: 720, step: 1 },
        align: { label: "Turn copies with the circle", type: "checkbox", value: true }
    },
    evaluate: function (ctx, p) {
        var n = Math.max(1, Math.round(p.count));
        // A full circle spaces them evenly round it; part of one puts
        // copies at both ends.
        var whole = Math.abs(p.sweep) >= 360;
        var copies = [];
        for (var i = 0; i < n; i++) {
            var t = whole ? i / n : (n > 1 ? i / (n - 1) : 0);
            var degrees = p.start + p.sweep * t;
            var a = degrees * Math.PI / 180;
            copies.push({ x: Math.cos(a) * p.radius, y: Math.sin(a) * p.radius, rotation: p.align ? degrees : 0 });
        }
        return { copies: copies };
    }
});
)JS"},
        {"grid", R"JS(
behavior({
    name: "Repeat in a Line or Grid",
    description: "Copies it in a row, or in rows and columns, from where it is. Key the spacing or the counts to animate the layout.",
    params: {
        columns: { label: "Copies across", value: 5, min: 1, max: 200, step: 1 },
        rows: { label: "Rows", value: 1, min: 1, max: 200, step: 1 },
        across: { label: "Space across (pixels)", value: 60, min: -2000, max: 2000, step: 1 },
        down: { label: "Space down (pixels)", value: 60, min: -2000, max: 2000, step: 1 },
        centered: { label: "Centered on it", type: "checkbox", value: false }
    },
    evaluate: function (ctx, p) {
        var columns = Math.max(1, Math.round(p.columns));
        var rows = Math.max(1, Math.round(p.rows));
        var ox = p.centered ? -(columns - 1) * p.across / 2 : 0;
        var oy = p.centered ? -(rows - 1) * p.down / 2 : 0;
        var copies = [];
        for (var r = 0; r < rows; r++)
            for (var c = 0; c < columns; c++)
                copies.push({ x: ox + c * p.across, y: oy + r * p.down });
        return { copies: copies };
    }
});
)JS"},
        {"pathrepeat", R"JS(
behavior({
    name: "Repeat along a Path",
    description: "Copies it along the line drawn on the motion guide its layer is under, spaced evenly from one place to another; slide them along, or key any setting, to animate them. Put the layer under a motion guide first.",
    params: {
        count: { label: "Copies", value: 10, min: 1, max: 1000, step: 1 },
        from: { label: "From (percent of the path)", value: 0, min: 0, max: 100, step: 0.5 },
        to: { label: "To (percent of the path)", value: 100, min: 0, max: 100, step: 0.5 },
        offset: { label: "Slide along (percent)", value: 0, min: -1000, max: 1000, step: 0.5 },
        wrap: { label: "Wrap round to the start", type: "checkbox", value: true },
        align: { label: "Turn copies with the path", type: "checkbox", value: true }
    },
    evaluate: function (ctx, p) {
        if (!ctx.path)
            throw new Error("Repeat along a Path needs its layer under a motion guide with a line drawn on it.");
        var n = Math.max(1, Math.round(p.count));
        // The whole path wrapped round spaces them evenly round it, with
        // none on top of another at the join.
        var whole = p.wrap && p.to - p.from >= 100;
        var copies = [];
        for (var i = 0; i < n; i++) {
            var t = n > 1 ? i / (whole ? n : n - 1) : 0;
            var percent = p.from + (p.to - p.from) * t + p.offset;
            if (p.wrap)
                percent = ((percent % 100) + 100) % 100;
            else if (percent < 0 || percent > 100)
                continue;
            var at = ctx.path.at(ctx.path.length * percent / 100);
            copies.push({ x: at.x - ctx.keyed.x, y: at.y - ctx.keyed.y, rotation: p.align ? at.angle : 0 });
        }
        return { copies: copies };
    }
});
)JS"},
        {"scatter", R"JS(
behavior({
    name: "Scatter in a Shape",
    description: "Copies it at random places inside the shape drawn on the motion guide its layer is under: a field of flowers, stars in a window, a crowd in a square. Draw a closed line on the guide first.",
    params: {
        count: { label: "Copies", value: 30, min: 1, max: 2000, step: 1 },
        spacing: { label: "Keep apart (pixels)", value: 20, min: 0, max: 1000, step: 1 },
        turn: { label: "Turn at random (degrees, up to)", value: 0, min: 0, max: 180, step: 1 },
        sizes: { label: "Sizes vary (percent)", value: 0, min: 0, max: 100, step: 1 },
        seed: { label: "Seed", type: "seed", value: 1 }
    },
    evaluate: function (ctx, p) {
        if (!ctx.path)
            throw new Error("Scatter in a Shape needs its layer under a motion guide with a closed line drawn on it.");
        // The guide's line as an outline, its ends joined.
        var outline = [];
        var steps = 240;
        for (var s = 0; s < steps; s++)
            outline.push(ctx.path.at(ctx.path.length * s / steps));
        var minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
        for (var o = 0; o < outline.length; o++) {
            minX = Math.min(minX, outline[o].x); maxX = Math.max(maxX, outline[o].x);
            minY = Math.min(minY, outline[o].y); maxY = Math.max(maxY, outline[o].y);
        }
        var inside = function (x, y) {
            var in_ = false;
            for (var i = 0, j = outline.length - 1; i < outline.length; j = i++) {
                var a = outline[i], b = outline[j];
                if ((a.y > y) !== (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)
                    in_ = !in_;
            }
            return in_;
        };
        // Random places inside, tried a few times each to keep them apart;
        // the same places every frame, from the seed.
        var n = Math.max(1, Math.round(p.count));
        var placed = [];
        var copies = [];
        var tries = 0;
        for (var c = 0; placed.length < n && tries < n * 60; tries++) {
            var x = minX + (maxX - minX) * ctx.random(tries * 2);
            var y = minY + (maxY - minY) * ctx.random(tries * 2 + 1);
            if (!inside(x, y))
                continue;
            var near = false;
            for (var k = 0; k < placed.length && !near; k++)
                near = Math.hypot(placed[k].x - x, placed[k].y - y) < p.spacing;
            if (near && tries < n * 40)
                continue;
            placed.push({ x: x, y: y });
            var size = 1 - p.sizes / 100 * ctx.random(100000 + c);
            copies.push({ x: x - ctx.keyed.x, y: y - ctx.keyed.y, rotation: p.turn * (2 * ctx.random(200000 + c) - 1), scale: size });
            c++;
        }
        return { copies: copies };
    }
});
)JS"},
        {"followpath", R"JS(
behavior({
    name: "Follow Path",
    description: "Puts it on the line drawn on the motion guide its layer is under: key how far along it is, or give it a speed, and it travels the line, turned with it if you like.",
    params: {
        progress: { label: "Along the path (percent)", value: 0, min: -1000, max: 1000, step: 0.5 },
        speed: { label: "Then on, percent per second", value: 0, min: -1000, max: 1000, step: 0.5 },
        loop: { label: "Loop round", type: "checkbox", value: true },
        align: { label: "Turn with the path", type: "checkbox", value: true }
    },
    evaluate: function (ctx, p) {
        if (!ctx.path)
            throw new Error("Follow Path needs its layer under a motion guide with a line drawn on it.");
        var percent = p.progress + p.speed * (ctx.frame - ctx.start) / ctx.fps;
        if (percent < 0 || percent > 100)
            percent = p.loop ? ((percent % 100) + 100) % 100 : Math.max(0, Math.min(100, percent));
        var at = ctx.path.at(ctx.path.length * percent / 100);
        return { x: at.x - ctx.keyed.x, y: at.y - ctx.keyed.y, rotation: p.align ? at.angle : 0 };
    }
});
)JS"},
        {"lookat", R"JS(
behavior({
    name: "Look At",
    description: "Turns it to face another instance, by that instance's name: eyes that follow, a spotlight on a dancer, an arrow that points.",
    params: {
        target: { label: "Look at", type: "object", value: "" },
        offset: { label: "Facing (degrees)", type: "angle", value: 0, min: -360, max: 360, step: 1 },
        amount: { label: "How much (percent)", value: 100, min: 0, max: 100, step: 1 }
    },
    evaluate: function (ctx, p) {
        var target = ctx.other(p.target);
        if (!target)
            return {};
        var x = ctx.keyed.x + ctx.current.x, y = ctx.keyed.y + ctx.current.y;
        var angle = Math.atan2(target.y - y, target.x - x) * 180 / Math.PI + p.offset;
        var now = ctx.keyed.rotation + ctx.current.rotation;
        // The short way round.
        var turn = ((angle - now) % 360 + 540) % 360 - 180;
        return { rotation: turn * p.amount / 100 };
    }
});
)JS"},
        {"recorded", R"JS(
behavior({
    name: "Recorded Motion",
    description: "A path recorded with Control > Motion Sketch, as keys on how far it moves across and down; change the keys, or record again.",
    params: {
        dx: { label: "Across (pixels)", value: 0, min: -100000, max: 100000, step: 0.5 },
        dy: { label: "Down (pixels)", value: 0, min: -100000, max: 100000, step: 0.5 }
    },
    evaluate: function (ctx, p) {
        return { x: p.dx, y: p.dy };
    }
});
)JS"},
        {"timeremap", R"JS(
behavior({
    name: "Time Remap",
    description: "Says which frame of the graphic shows, frame by frame: key Frame shown from 1 to 48 over 96 frames for slow motion, hold it still for a freeze, key it from high to low to run it backwards. The graphic's own loop is set aside while it is on.",
    params: {
        frame: { label: "Frame shown", value: 1, min: 1, max: 100000, step: 0.1 }
    },
    evaluate: function (ctx, p) {
        return { holdFrame: Math.max(0, Math.floor(p.frame) - 1) };
    }
});
)JS"},
        {"linkobject", R"JS(
behavior({
    name: "Link to Object",
    description: "Moves, turns, scales or fades it as another instance does, by that instance's name, from where each starts: a shadow under a bouncing ball, a wheel turning as the car moves, a needle rising with a level. Scale, offset and limit the change as you like.",
    params: {
        target: { label: "Linked to", type: "object", value: "" },
        what: { label: "Follow its", type: "choice", choices: ["Move", "Turn", "Size", "Fade", "Move across as turn", "Move across", "Move down"] },
        into: { label: "Change my", type: "choice", choices: ["Same", "Move across", "Move down", "Turn", "Size", "Fade"] },
        amount: { label: "Amount (percent)", value: 100, min: -1000, max: 1000, step: 1 },
        offset: { label: "Offset", value: 0, min: -5000, max: 5000, step: 1 },
        limit: { label: "Keep within limits", type: "checkbox", value: false },
        low: { label: "Lowest", value: -100, min: -5000, max: 5000, step: 1 },
        high: { label: "Highest", value: 100, min: -5000, max: 5000, step: 1 },
        lag: { label: "Lag (frames)", value: 0, min: 0, max: 240, step: 1 }
    },
    evaluate: function (ctx, p) {
        var now = ctx.other(p.target, ctx.frame - p.lag);
        var first = ctx.other(p.target, ctx.start);
        if (!now || !first)
            return {};
        var k = p.amount / 100;
        var shape = function (v) {
            v = v * k + p.offset;
            return p.limit ? Math.max(Math.min(p.low, p.high), Math.min(Math.max(p.low, p.high), v)) : v;
        };
        // Moving both ways at once, as it did.
        if (p.what === "Move" && (p.into === "Same" || !p.into))
            return { x: shape(now.x - first.x), y: shape(now.y - first.y) };
        // The change in what it follows, in its own units: pixels, degrees,
        // or percent for size and fade.
        var change;
        var natural;
        switch (p.what) {
        case "Turn": change = now.rotation - first.rotation; natural = "Turn"; break;
        case "Size": change = (now.scaleX / first.scaleX - 1) * 100; natural = "Size"; break;
        case "Fade": change = (now.alpha / Math.max(first.alpha, 0.0001) - 1) * 100; natural = "Fade"; break;
        case "Move across as turn": change = now.x - first.x; natural = "Turn"; break;
        case "Move down": change = now.y - first.y; natural = "Move down"; break;
        default: change = now.x - first.x; natural = "Move across"; break;
        }
        var v = shape(change);
        switch (!p.into || p.into === "Same" ? natural : p.into) {
        case "Move down": return { y: v };
        case "Turn": return { rotation: v };
        case "Size": return { scale: Math.max(0, 1 + v / 100) };
        case "Fade": return { alpha: Math.max(0, 1 + v / 100) };
        default: return { x: v };
        }
    }
});
)JS"},
        {"linkcontroller", R"JS(
behavior({
    name: "Link to Controller",
    description: "Sets a property from a master controller's slider: as the slider goes from one end to the other, the property goes from one value to another.",
    params: {
        controller: { label: "Controller", type: "controller", value: "" },
        what: { label: "Property", type: "choice", choices: ["Move across", "Move down", "Turn", "Size", "Fade"] },
        from: { label: "At the slider's start", value: 0, min: -5000, max: 5000, step: 1 },
        to: { label: "At the slider's end", value: 100, min: -5000, max: 5000, step: 1 }
    },
    evaluate: function (ctx, p) {
        var value = ctx.controller(p.controller);
        if (value === null)
            return {};
        var v = p.from + (p.to - p.from) * value;
        switch (p.what) {
        case "Move down": return { y: v };
        case "Turn": return { rotation: v };
        case "Size": return { scale: v / 100 };
        case "Fade": return { alpha: Math.max(0, v / 100) };
        default: return { x: v };
        }
    }
});
)JS"},
        {"effector", R"JS(
behavior({
    name: "Effector",
    description: "Changes the copies a Repeat behavior made by how near each is to a place, or by their order: copies grow, turn, move or fade as an area passes over them. Key its place or size to sweep it across.",
    params: {
        shape: { label: "Strength by", type: "choice", choices: ["Distance from a point", "Across, left to right", "Down, top to bottom", "Order of the copies", "Every copy alike"] },
        cx: { label: "Point across (pixels from it)", value: 0, min: -4000, max: 4000, step: 1 },
        cy: { label: "Point down (pixels from it)", value: 0, min: -4000, max: 4000, step: 1 },
        size: { label: "Reach (pixels)", value: 100, min: 1, max: 4000, step: 1 },
        invert: { label: "Strongest far away", type: "checkbox", value: false },
        moveX: { label: "Move across (pixels)", value: 0, min: -2000, max: 2000, step: 1 },
        moveY: { label: "Move down (pixels)", value: -20, min: -2000, max: 2000, step: 1 },
        turn: { label: "Turn (degrees)", type: "angle", value: 0, min: -720, max: 720, step: 1 },
        grow: { label: "Grow (percent)", value: 50, min: -100, max: 1000, step: 1 },
        fade: { label: "Fade (percent)", value: 0, min: 0, max: 100, step: 1 },
        frames: { label: "Frames on", value: 0, min: -240, max: 240, step: 1 }
    },
    evaluate: function (ctx, p) {
        var x = ctx.current.x, y = ctx.current.y;
        var s;
        switch (p.shape) {
        case "Across, left to right": s = (x - p.cx) / p.size + 0.5; break;
        case "Down, top to bottom": s = (y - p.cy) / p.size + 0.5; break;
        case "Order of the copies": s = ctx.copies > 1 ? ctx.copy / (ctx.copies - 1) : 1; break;
        case "Every copy alike": s = 1; break;
        default: s = 1 - Math.sqrt((x - p.cx) * (x - p.cx) + (y - p.cy) * (y - p.cy)) / p.size;
        }
        s = Math.max(0, Math.min(1, s));
        // Smoothed at the edge of its reach.
        s = s * s * (3 - 2 * s);
        if (p.invert)
            s = 1 - s;
        return {
            x: p.moveX * s, y: p.moveY * s, rotation: p.turn * s,
            scale: Math.max(0, 1 + p.grow / 100 * s), alpha: 1 - p.fade / 100 * s,
            frame: Math.round(p.frames * s)
        };
    }
});
)JS"},
        {"particles", R"JS(
behavior({
    name: "Particles",
    description: "Sends out copies of it as particles: sparks, smoke, rain, confetti, bubbles. They are born at a steady rate, fly off, fall, turn, grow or shrink and fade, and die. The same frame always shows the same particles.",
    params: {
        rate: { label: "Born per second", value: 20, min: 0, max: 500, step: 0.5 },
        life: { label: "Life (seconds)", value: 2, min: 0.05, max: 30, step: 0.05 },
        emitFor: { label: "Stop after (seconds, 0 never)", value: 0, min: 0, max: 600, step: 0.1 },
        width: { label: "Born across (pixels)", value: 0, min: 0, max: 4000, step: 1 },
        height: { label: "Born down (pixels)", value: 0, min: 0, max: 4000, step: 1 },
        direction: { label: "Direction (degrees)", type: "angle", value: -90, min: -360, max: 360, step: 1 },
        spread: { label: "Spread (degrees)", type: "angle", value: 30, min: 0, max: 360, step: 1 },
        speed: { label: "Speed (pixels per second)", value: 150, min: 0, max: 5000, step: 1 },
        speedRandom: { label: "Speed varies (percent)", value: 30, min: 0, max: 100, step: 1 },
        gravity: { label: "Gravity (pixels per second, down)", value: 200, min: -5000, max: 5000, step: 1 },
        turbulence: { label: "Turbulence (pixels)", value: 0, min: 0, max: 500, step: 1 },
        spin: { label: "Spin (degrees per second, either way)", value: 90, min: 0, max: 3600, step: 1 },
        startSize: { label: "Size at birth (percent)", value: 100, min: 0, max: 1000, step: 1 },
        endSize: { label: "Size at death (percent)", value: 50, min: 0, max: 1000, step: 1 },
        sizeRandom: { label: "Size varies (percent)", value: 20, min: 0, max: 100, step: 1 },
        fade: { label: "Fade out", type: "checkbox", value: true },
        turn: { label: "Turn to the way they fly", type: "checkbox", value: false },
        loopFromBirth: { label: "Loops start at birth", type: "checkbox", value: true },
        seed: { label: "Seed", type: "seed", value: 1 }
    },
    evaluate: function (ctx, p) {
        var t = (ctx.frame - ctx.start) / ctx.fps;
        var rate = Math.max(0, p.rate);
        if (rate <= 0)
            return { copies: [] };
        var life = Math.max(0.05, p.life);
        var last = Math.floor(t * rate);
        if (p.emitFor > 0)
            last = Math.min(last, Math.floor(p.emitFor * rate));
        var first = Math.max(0, Math.ceil((t - life) * rate));
        var copies = [];
        for (var k = first; k <= last && copies.length < 2000; k++) {
            var age = t - k / rate;
            if (age < 0 || age >= life)
                continue;
            var u = age / life;
            var r = function (n) { return ctx.random(k * 16 + n); };
            var angle = (p.direction + (r(1) - 0.5) * p.spread) * Math.PI / 180;
            var speed = p.speed * (1 - p.speedRandom / 100 * r(2));
            var vx = Math.cos(angle) * speed, vy = Math.sin(angle) * speed;
            var x = (r(3) - 0.5) * p.width + vx * age;
            var y = (r(4) - 0.5) * p.height + vy * age + 0.5 * p.gravity * age * age;
            if (p.turbulence > 0) {
                x += ctx.noise(age * 2, k * 2) * p.turbulence;
                y += ctx.noise(age * 2, k * 2 + 1) * p.turbulence;
            }
            var size = (p.startSize + (p.endSize - p.startSize) * u) / 100 * (1 - p.sizeRandom / 100 * r(5));
            var rotation = (r(6) * 2 - 1) * p.spin * age;
            if (p.turn)
                rotation += Math.atan2(vy + p.gravity * age, vx) * 180 / Math.PI;
            var alpha = p.fade ? Math.min(1, (1 - u) * 3) : 1;
            var copy = { x: x, y: y, rotation: rotation, scale: Math.max(0, size), alpha: alpha };
            if (p.loopFromBirth)
                copy.frame = Math.round(age * ctx.fps) - (ctx.frame - ctx.start);
            copies.push(copy);
        }
        return { copies: copies };
    }
});
)JS"},
        {"stagger", R"JS(
behavior({
    name: "Stagger",
    description: "Puts each copy's graphic loop a few frames after the one before, so copies of one cycle move out of step: crowds, grass, waves. Use it after a Repeat behavior.",
    params: {
        frames: { label: "Frames between copies", value: 2, min: -240, max: 240, step: 1 },
        random: { label: "In random order", type: "checkbox", value: false },
        seed: { label: "Seed", type: "seed", value: 1 }
    },
    evaluate: function (ctx, p) {
        var place = p.random ? Math.floor(ctx.random(1) * ctx.copies) : ctx.copy;
        return { frame: Math.round(place * p.frames) };
    }
});
)JS"},
        {"follow", R"JS(
behavior({
    name: "Follow with Lag",
    description: "Trails its own keyed motion on a spring: it lags behind, catches up and overshoots, for hair, antennas and props.",
    params: {
        response: { label: "Response (per second)", value: 3, min: 0.1, max: 30, step: 0.1 },
        bounce: { label: "Bounce (percent)", value: 40, min: 0, max: 95, step: 1 },
        turn: { label: "Turn as well", type: "checkbox", value: true }
    },
    evaluate: function (ctx, p) {
        var omega = 2 * Math.PI * p.response;
        var zeta = 1 - p.bounce / 100;
        var dt = 1 / ctx.fps;
        // Run the spring over the last few seconds of the keyed motion.
        var history = Math.min(ctx.frame - ctx.start, Math.ceil(ctx.fps * 3));
        var first = ctx.keyedAt(ctx.frame - history) || ctx.keyed;
        var x = first.x, y = first.y, r = first.rotation, vx = 0, vy = 0, vr = 0;
        for (var f = ctx.frame - history + 1; f <= ctx.frame; f++) {
            var k = ctx.keyedAt(f) || ctx.keyed;
            for (var s = 0; s < 4; s++) {
                var h = dt / 4;
                vx += (omega * omega * (k.x - x) - 2 * zeta * omega * vx) * h;
                vy += (omega * omega * (k.y - y) - 2 * zeta * omega * vy) * h;
                vr += (omega * omega * (k.rotation - r) - 2 * zeta * omega * vr) * h;
                x += vx * h;
                y += vy * h;
                r += vr * h;
            }
        }
        return { x: x - ctx.keyed.x, y: y - ctx.keyed.y, rotation: p.turn ? r - ctx.keyed.rotation : 0 };
    }
});
)JS"},
        {"loop", R"JS(
behavior({
    name: "Loop",
    description: "Repeats the motion keyed over the first frames of its keyframes for as long as they run: round and round, back and forth, or carrying on the way it was going.",
    params: {
        mode: { label: "Loop", choices: ["Cycle", "Back and Forth", "Continue"] },
        length: { label: "Loop the first (frames)", value: 24, min: 2, max: 10000, step: 1 }
    },
    evaluate: function (ctx, p) {
        var n = Math.round(p.length);
        var local = ctx.frame - ctx.start;
        if (n < 2 || local < n)
            return {};
        var keyed = ctx.keyed;
        var at;
        if (p.mode === "Continue") {
            // On from the last frame of the loop at the speed it ended with.
            var last = ctx.keyedAt(ctx.start + n - 1);
            var before = ctx.keyedAt(ctx.start + n - 2);
            if (!last || !before)
                return {};
            var on = local - (n - 1);
            at = {
                x: last.x + (last.x - before.x) * on, y: last.y + (last.y - before.y) * on,
                rotation: last.rotation + (last.rotation - before.rotation) * on,
                scaleX: last.scaleX, scaleY: last.scaleY, alpha: last.alpha
            };
        } else {
            var from;
            if (p.mode === "Back and Forth") {
                var period = 2 * (n - 1);
                var q = local % period;
                from = q < n ? q : period - q;
            } else {
                from = local % n;
            }
            at = ctx.keyedAt(ctx.start + from);
            if (!at)
                return {};
        }
        var ratio = function (a, b) { return Math.abs(b) > 1e-9 ? a / b : 1; };
        return {
            x: at.x - keyed.x, y: at.y - keyed.y, rotation: at.rotation - keyed.rotation,
            scaleX: ratio(at.scaleX, keyed.scaleX), scaleY: ratio(at.scaleY, keyed.scaleY), alpha: ratio(at.alpha, keyed.alpha)
        };
    }
});
)JS"},
        {"jiggle", R"JS(
behavior({
    name: "Jiggle",
    description: "Makes it sway and bounce on a spring as it is carried about, turning about its transformation point: antennas, ears, a feather, a sign on a chain. Works with the layers it is parented to as well as its own keys.",
    params: {
        points: { label: "It points", choices: ["Up", "Down", "Left", "Right"] },
        amount: { label: "Sway (degrees per 10 pixels of lag)", value: 6, min: 0, max: 90, step: 0.5 },
        limit: { label: "At most (degrees)", value: 45, min: 0, max: 180, step: 1 },
        response: { label: "Springiness (swings per second)", value: 2, min: 0.1, max: 20, step: 0.1 },
        bounce: { label: "Bounce (percent)", value: 60, min: 0, max: 100, step: 1 }
    },
    evaluate: function (ctx, p) {
        var omega = 2 * Math.PI * p.response;
        var zeta = 1 - p.bounce / 100;
        var dt = 1 / ctx.fps;
        // Where its pivot is in the world on a frame: its keys moved by its
        // parents.
        var world = function (f) {
            var k = ctx.keyedAt(f) || ctx.keyed;
            return ctx.parentAt(f, k.x, k.y);
        };
        // A weight on a spring follows the pivot; how far it trails is the sway.
        var history = Math.min(ctx.frame - ctx.start, Math.ceil(ctx.fps * 3));
        var target = world(ctx.frame - history);
        var x = target.x, y = target.y, vx = 0, vy = 0;
        for (var f = ctx.frame - history + 1; f <= ctx.frame; f++) {
            target = world(f);
            for (var s = 0; s < 4; s++) {
                var h = dt / 4;
                vx += (omega * omega * (target.x - x) - 2 * zeta * omega * vx) * h;
                vy += (omega * omega * (target.y - y) - 2 * zeta * omega * vy) * h;
                x += vx * h;
                y += vy * h;
            }
        }
        // The lag across the way it points turns it toward where it trails.
        var lagX = x - target.x, lagY = y - target.y;
        var across = p.points === "Down" ? -lagX : p.points === "Left" ? -lagY : p.points === "Right" ? lagY : lagX;
        var turn = across / 10 * p.amount;
        return { rotation: Math.max(-p.limit, Math.min(p.limit, turn)) };
    }
});
)JS"},
        {"sound", R"JS(
behavior({
    name: "Follow Sound",
    description: "Moves with how loud the scene's sound is: grows, jumps, turns, shows or changes frame on the beats.",
    params: {
        change: { label: "Change", choices: ["Scale", "Move Up", "Move Right", "Turn", "Opacity", "Symbol Frame"] },
        amount: { label: "Amount (percent, pixels or degrees)", value: 30, min: 0, max: 1000, step: 1 },
        frames: { label: "Frames at the loudest (Symbol Frame)", value: 8, min: 1, max: 1000, step: 1 },
        quiet: { label: "Ignore below (percent)", value: 10, min: 0, max: 99, step: 1 },
        smoothing: { label: "Fall off (frames)", value: 3, min: 0, max: 48, step: 1 },
        delay: { label: "Delay (frames)", value: 0, min: -48, max: 48, step: 1 }
    },
    evaluate: function (ctx, p) {
        // The loudness now, falling away slowly after a beat rather than
        // dropping at once: the loudest of the last few frames, faded.
        var now = ctx.frame - p.delay;
        var level = 0;
        for (var back = 0; back <= p.smoothing; back++) {
            var heard = ctx.loudness(now - back) * (1 - back / (p.smoothing + 1));
            level = Math.max(level, heard);
        }
        var floor = p.quiet / 100;
        level = Math.max(0, (level - floor) / (1 - floor));
        switch (p.change) {
        case "Move Up": return { y: -level * p.amount };
        case "Move Right": return { x: level * p.amount };
        case "Turn": return { rotation: level * p.amount };
        case "Opacity": return { alpha: Math.min(1, level * p.amount / 100) };
        case "Symbol Frame": return { frame: Math.round(level * (p.frames - 1)) };
        default: return { scale: 1 + level * p.amount / 100 };
        }
    }
});
)JS"},
    };
    return sources;
}

} // namespace fla::behaviors
