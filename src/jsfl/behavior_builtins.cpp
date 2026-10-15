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
