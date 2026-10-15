// A behavior of your own. Phoenix calls evaluate for each frame the
// instance is on, and adds what it returns to the keyed motion: x and y in
// pixels, rotation in degrees, scale and alpha as factors, frame to run a
// graphic's loop on. Save, and the behavior is in Add Behavior at once.
behavior({
    name: "Drift",
    description: "Drifts sideways and bobs up and down.",
    params: {
        speed: { label: "Speed (pixels a second)", value: 40, min: -500, max: 500 },
        bob: { label: "Bob (pixels)", value: 4, min: 0, max: 50 },
        rate: { label: "Bobs per second", value: 1, min: 0, max: 10, step: 0.1 }
    },
    evaluate: function (ctx, p) {
        return {
            x: p.speed * (ctx.frame - ctx.start) / ctx.fps,
            y: Math.sin(ctx.time * 2 * Math.PI * p.rate) * p.bob
        };
    }
});
