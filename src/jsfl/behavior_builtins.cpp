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
        {"linkobject", R"JS(
behavior({
    name: "Link to Object",
    description: "Moves, turns, scales or fades it as another instance does, by that instance's name, from where each starts: a shadow under a bouncing ball, a wheel turning as the car moves.",
    params: {
        target: { label: "Linked to", type: "object", value: "" },
        what: { label: "Follow its", type: "choice", choices: ["Move", "Turn", "Size", "Fade", "Move across as turn"] },
        amount: { label: "Amount (percent)", value: 100, min: -1000, max: 1000, step: 1 },
        lag: { label: "Lag (frames)", value: 0, min: 0, max: 240, step: 1 }
    },
    evaluate: function (ctx, p) {
        var now = ctx.other(p.target, ctx.frame - p.lag);
        var first = ctx.other(p.target, ctx.start);
        if (!now || !first)
            return {};
        var k = p.amount / 100;
        switch (p.what) {
        case "Turn": return { rotation: (now.rotation - first.rotation) * k };
        case "Size": return { scaleX: 1 + (now.scaleX / first.scaleX - 1) * k, scaleY: 1 + (now.scaleY / first.scaleY - 1) * k };
        case "Fade": return { alpha: Math.max(0, 1 + (now.alpha / Math.max(first.alpha, 0.0001) - 1) * k) };
        case "Move across as turn": return { rotation: (now.x - first.x) * k };
        default: return { x: (now.x - first.x) * k, y: (now.y - first.y) * k };
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
    };
    return sources;
}

} // namespace fla::behaviors
