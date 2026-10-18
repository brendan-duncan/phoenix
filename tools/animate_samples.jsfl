// Sets up the two sample files Phoenix still needs from Animate, as far as a
// script can: Animate's scripting has no way to add bones, so those are
// drawn by hand afterwards (see the steps in TODO.md or the chat).
//
//   bones_springs.fla: an "Arm" layer with three movie clips in a row
//   (Upper arm, Forearm, Hand; the Forearm's transformation point moved to
//   its far end) and a "Tail" layer with a long shape, both 20 frames long.
//   Left open, ready for the Bone tool.
//
//   guides.fla: four ruler guides set through the timeline's
//   setGuidelines(), in the format Phoenix guessed. What Animate reads back
//   is written to guides_api.txt, which confirms or corrects the format.
//
// Run it from Commands > Run Command. It asks before replacing a file.
(function () {
    var path = prompt("Folder to save the samples in:", "D:\\fla");
    if (!path)
        return;
    var folder = FLfile.platformPathToURI(path.replace(/[\\\/]$/, "") + "\\");

    var save = function (doc, name) {
        var uri = folder + name;
        if (FLfile.exists(uri) && !confirm(name + " already exists in " + path + ". Replace it?"))
            return false;
        return fl.saveDocument(doc, uri);
    };

    // bones_springs.fla
    var doc = fl.createDocument("timeline");
    doc.width = 550;
    doc.height = 400;
    var timeline = doc.getTimeline();
    timeline.layers[0].name = "Arm";
    var parts = [["Upper arm", 60, "#C86432"], ["Forearm", 180, "#D2963C"], ["Hand", 300, "#E6C850"]];
    for (var i = 0; i < parts.length; ++i) {
        var left = parts[i][1];
        doc.selectNone();
        doc.setFillColor(parts[i][2]);
        doc.addNewRectangle({left: left, top: 180, right: left + 110, bottom: 220}, 0);
        doc.setSelectionRect({left: left - 5, top: 170, right: left + 115, bottom: 230}, true, false);
        doc.convertToSymbol("movie clip", parts[i][0], "center");
        if (parts[i][0] == "Forearm" && doc.selection.length == 1)
            doc.selection[0].setTransformationPoint({x: 55, y: 0});
    }
    doc.selectNone();
    var tail = timeline.addNewLayer("Tail", "normal", false);
    timeline.currentLayer = tail;
    doc.setFillColor("#5A8CC8");
    doc.addNewRectangle({left: 125, top: 300, right: 425, bottom: 340}, 0, false, true);
    doc.selectNone();
    timeline.insertFrames(19, true, 0);
    timeline.currentFrame = 0;
    var bonesSaved = save(doc, "bones_springs.fla");

    // guides.fla
    var guides = fl.createDocument("timeline");
    guides.width = 550;
    guides.height = 400;
    guides.setFillColor("#999999");
    guides.addNewRectangle({left: 200, top: 150, right: 350, bottom: 250}, 0);
    guides.selectNone();
    var guideTimeline = guides.getTimeline();
    var report = "";
    try {
        report += "before: " + guideTimeline.getGuidelines() + "\n";
        guideTimeline.setGuidelines('<guidelines>' +
            '<guideline direction="h">100</guideline><guideline direction="h">300</guideline>' +
            '<guideline direction="v">150</guideline><guideline direction="v">400</guideline>' +
            '</guidelines>');
        report += "after setting: " + guideTimeline.getGuidelines() + "\n";
    } catch (e) {
        report += "error: " + e + "\n";
    }
    FLfile.write(folder + "guides_api.txt", report);
    var guidesSaved = save(guides, "guides.fla");

    fl.trace("bones_springs.fla " + (bonesSaved ? "saved" : "not saved") +
        ", guides.fla " + (guidesSaved ? "saved" : "not saved") + " in " + path + ".");
    fl.trace(report);
})();
