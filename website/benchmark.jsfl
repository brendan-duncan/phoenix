// Times the same steps as "Phoenix benchmark <file>" in Adobe Animate, for
// "Phoenix benchmark <file> --against <file>.animate.json".
// Run it from Commands > Run Command; pick the FLA when asked. It writes
// <file>.animate.json beside the FLA.
(function () {
    var uri = fl.browseForFileURL("open", "The FLA to time");
    if (!uri)
        return;
    var started = new Date().getTime();
    var doc = fl.openDocument(uri);
    var openMs = new Date().getTime() - started;

    // Every frame of the timeline to PNG, timed per frame.
    var frames = doc.getTimeline().frameCount;
    var folder = uri.replace(/[^\/]*$/, "") + "animate_benchmark/";
    FLfile.createFolder(folder);
    started = new Date().getTime();
    doc.exportPNG(folder + "frame.png", true, false);
    var renderMs = (new Date().getTime() - started) / Math.max(frames, 1);

    started = new Date().getTime();
    doc.saveAsCopy(folder + "copy.fla");
    var saveMs = new Date().getTime() - started;

    var result = '{\n  "file": "' + uri.replace(/^.*\//, "") + '",\n' +
        '  "frames": ' + frames + ',\n' +
        '  "open_ms": ' + openMs + ',\n' +
        '  "render_ms_per_frame": ' + renderMs + ',\n' +
        '  "save_fla_ms": ' + saveMs + '\n}\n';
    FLfile.write(uri.replace(/\.fla$/i, "") + ".animate.json", result);
    fl.closeDocument(doc, false);
    fl.trace(result);
})();
