// Exports frames of the document open in Animate as PNGs beside it, named
// <file>_f<frame>.png (frames counted from 1), the reference pictures
// Phoenix's renders are compared with. Save the document first.
// Run it from Commands > Run Command.
(function () {
    var doc = fl.getDocumentDOM();
    if (!doc || !doc.pathURI) {
        alert("Open and save the document first.");
        return;
    }
    var list = prompt("Frames to export (from 1), separated by commas:", "1,10,20");
    if (!list)
        return;
    var timeline = doc.getTimeline();
    var stem = doc.pathURI.replace(/\.fla$/i, "");
    var current = timeline.currentFrame;
    var done = [];
    var frames = list.split(",");
    for (var i = 0; i < frames.length; ++i) {
        var frame = parseInt(frames[i], 10);
        if (isNaN(frame) || frame < 1 || frame > timeline.frameCount)
            continue;
        timeline.currentFrame = frame - 1;
        doc.exportPNG(stem + "_f" + frame + ".png", true, true);
        done.push(frame);
    }
    timeline.currentFrame = current;
    fl.trace("Exported frames " + done.join(", ") + " beside " + FLfile.uriToPlatformPath(doc.pathURI));
})();
