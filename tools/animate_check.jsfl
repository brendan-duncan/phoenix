// The Animate half of tools/animate_check.py: opens each original and each
// copy Phoenix saved, as files.txt beside this script lists them, exports
// its first, middle and last frames as PNG into animate/, and writes
// animate.json with what it found (frames, layers, library items, any
// failure to open). Run it from Commands > Run Command; it closes every
// file it opens without saving.
(function () {
    var folder = fl.scriptURI.replace(/[^\/]*$/, "");
    var list = FLfile.read(folder + "files.txt");
    if (!list) {
        alert("No files.txt beside the script: run tools/animate_check.py prepare first.");
        return;
    }
    var out = folder + "animate/";
    FLfile.createFolder(out);
    var names = list.replace(/\r/g, "").split("\n");
    var results = [];
    var quote = function (text) {
        return '"' + String(text).replace(/\\/g, "\\\\").replace(/"/g, '\\"') + '"';
    };
    for (var i = 0; i < names.length; ++i) {
        var name = names[i];
        if (!name)
            continue;
        var entry = '{"file": ' + quote(name);
        var started = new Date().getTime();
        var doc = null;
        try {
            doc = fl.openDocument(folder + name);
        } catch (e) {
            entry += ', "error": ' + quote("could not open: " + e);
        }
        if (doc) {
            var timeline = doc.getTimeline();
            var count = timeline.frameCount;
            var frames = [0, Math.floor((count - 1) / 2), Math.max(0, count - 1)];
            var stem = name.replace(/\.fla$/i, "");
            for (var f = 0; f < frames.length; ++f) {
                timeline.currentFrame = frames[f];
                doc.exportPNG(out + stem + "." + (frames[f] + 1) + ".png", true, true);
            }
            entry += ', "openMs": ' + (new Date().getTime() - started) +
                ', "frames": ' + count +
                ', "layers": ' + timeline.layerCount +
                ', "scenes": ' + doc.timelines.length +
                ', "libraryItems": ' + doc.library.items.length +
                ', "exported": [' + (frames[0] + 1) + ", " + (frames[1] + 1) + ", " + (frames[2] + 1) + "]";
            fl.closeDocument(doc, false);
        } else if (entry.indexOf('"error"') < 0) {
            entry += ', "error": "Animate opened nothing"';
        }
        entry += "}";
        results.push(entry);
        fl.trace((i + 1) + "/" + names.length + " " + name);
    }
    FLfile.write(folder + "animate.json", "[\n" + results.join(",\n") + "\n]\n");
    fl.trace("Wrote " + folder + "animate.json; now run tools/animate_check.py compare on this folder.");
})();
