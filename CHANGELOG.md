# Changelog

What changed in each release of Phoenix, newest first.

## 0.16.0

- Updates and downloads now come through phoenixanimate.com, and Help > Phoenix Help opens the guide there

## 0.15.1

- Node graphs, adjustment layers and layer modes work on the stage alone in the view, as in an export: Vignette, ramps, wipes and other effects that go by the edges follow the stage's edges, not the window's, and art off the stage shows without them

## 0.15.0

- Node graphs: Time Offset and Frame Hold nodes; a renamed layer keeps its Layer nodes; SVG exports show the graph; Export FLA for Other Apps bakes each graph into a layer of pictures; scripts and AI assistants can list, read and set graphs
- Command-line renders no longer stop with a "no platform plugin" box in fresh builds and installs: the offscreen platform is deployed with Phoenix
- Layers show badges for the effects they have on (motion blur, echo or posterize time, layer mode, mask edge, effect layer, node graph); hover for what each does, click to change its settings
- Motion blur follows what animates inside graphics and movie clips in Test Movie too, and leaves a stopped or held symbol sharp inside
- Time Remap works on movie clips too, which follow it instead of playing on their own (in Test Movie as well), and its Frame shown can be shaped as a curve in the Motion Editor
- Bone springs bend boned shapes too: a shape's sprung bones trail, swing past and settle, on the stage, in Test Movie and in exports
- Effects on one instance: a symbol instance can carry any of the adjustment effects (outline, glow, ripple, gradients, color grades, wipes...) on its own picture, from the new Effects side of the Behaviors panel, keyed like any other, on the stage, in Test Movie and in every export
- Export FLA for Other Apps carries more effects: Glow and Outline as Glow filters, Hue/Saturation as Adjust Color, Fill and Levels as color effects, Directional Blur as a one-axis Blur; effects with no counterpart are drawn into bitmaps a frame at a time, so the look survives
- Masks: objects on a mask layer can each have their own feather, kept by keyframe and blended through tweens; a masked layer can be masked by other mask layers too, added, subtracted or intersected (Also Masked By); and SVG exports write feathered masks as soft SVG masks instead of hard clips
- Vertical text takes letter animators, and Text on a Path runs it along a motion guide, its letters standing as they do in the column
- Bake to Keyframes and Export FLA for Other Apps keep tapered lines (as filled outlines), dashes with their caps, and gradients on strokes and fills, so other apps show shape operators as Phoenix does
- 3D: up to four lights in their own colors, soft shadows that clips nearer the viewer cast on turned clips behind them, and extrusion (a Depth for a movie clip, its edges drawn back and shaded by the lights)
- Many behavior copies of movie clips, and of instances with filters, blend modes or color effects, draw from shared pictures instead of one by one, with the same result
- Browse Presets shows shape operators on sample shapes and adjustment effects on a sample picture, each with a still in the list and playing beside it
- Export FLA for Other Apps bakes templates further: parts pinned to text are moved and stretched to fit each instance's words, and symbols with responsive time play with their middle stretched to the span, as in Phoenix
- Node graphs: Window > Node Graph composites a timeline's layers through nodes (Layer, Merge with 13 blend modes, Composite, Matte, Mix, Transform and every adjustment effect), each node showing a picture of its result; search to add nodes, drop links on empty space, view any node on the stage, bypass, backdrops, a minimap, and keyed settings
- HTML extension panels (CEP) show in Phoenix: Window > Extensions opens them as docks, their scripts run JSFL in a context that keeps its functions between calls, and their panel menus, context menus, file access and events work
- FLA files with components (Button, ColorPicker, ComboBox and the rest) open instead of failing, and keep their components when saved
- SWF movies published with a preloader for TLF text open as the movie itself, not the preloader's empty frames

## 0.14.0

- Native extensions: C and C++ libraries in the External Libraries folder, written for the `mm_jsapi.h` extension API, add functions scripts can call
- Illustrator and EPS import: area and path text (Illustrator 11 and later too), patterns, placed images, Zstandard-compressed Illustrator files, shadings as gradients, image masks, and the widths of fonts the file carries; Photoshop EPS images and CMYK JPEGs come in with the right colors
- Opening a SWF movie finds its tweens: smooth movement, fades, turns and eases come back as classic tweens, and morphs as shape tweens, instead of a keyframe on every frame; embedded video comes back as a video item of its size and length
- JSFL: adding, duplicating, renaming, reordering and deleting scenes, `importFile`, sounds and videos renamed and deleted in the library, `exportToFile` for every bitmap and sound, filters, text strings, union, punch, crop, space, match, distribute to layers or keyframes, and finding objects by name or kind
- Python as a module: `import phoenix` from your own Python opens, changes, renders and saves documents in a windowless Phoenix
- Python scripts: keyword arguments (`timeline.insert_frames(5, all_layers=True)`), and `fl.runScript` runs JSFL scripts too; `Phoenix batch --python` runs scripts inside Phoenix, like `--jsfl`
- SWF export: sounds inside movie clips play, sounds keep their envelopes, in and out points and loops, and each library sound keeps its own compression; Animate's stream and event sound settings are read from the file
- Old binary FLA files open from Flash 5, MX, MX 2004, Flash 8, CS3 and CS4, including the versions Phoenix refused before; more of each reads too: rectangle primitives, videos on the stage, CS4 text, exported symbols, and scripts in MX files

## 0.13.3

- Tutorials: each lesson has a picture of how it turns out, which plays when you pick it, and tags (Animation, Motion Graphics, Rigging and more) to show only the lessons about one thing

## 0.13.2

- Many crashes fixed: closing a document tab, adding bones then closing, undoing while an AI assistant edits, objects kept by scripts after they are gone, Test Movie clips made and removed by frame scripts, very large blurs, and high-density screens running out of memory
- Started from Explorer or a shortcut, Phoenix no longer leaves a console window open beside it
- Damaged or hostile SWF, FLA, PSD, EPS, PDF, SVG, extension and lip sync files are refused or read in part, rather than hanging Phoenix, closing it, or writing outside their folder
- The Behaviors panel, Echo and Posterize Time, Mask Edge, bone springs, 3D Lights and Depth and template sliders use hot text numbers (drag to scrub, click to type) instead of arrow boxes
- On Windows, a crash leaves a report in %LOCALAPPDATA%\Phoenix\Crashes to send with a bug report

## 0.13.1

- Objects with behaviors that make copies (particles, rings, rows) are selected by clicking any copy that shows, not only the first
- Effects use every processor core: filters, adjustment-layer effects, mask edges and blurs draw 5 to 25 times faster, looking exactly as before
- Motion blur draws about 15 times faster, and with echoes about 90 times: only the part of the picture the layer covers is blurred, and frames already blurred are reused
- The Text tool types c, x and v again; they were being taken for the copy, cut and paste keys

## 0.13.0

- Eight new motion graphics lessons: motion blur and echo, glow and wipes, dashes and text on a path, a sound visualizer, controls on a null, 3D cards in the light, Motion Sketch, and a template that fits
- Vignette darkens the edges of the whole picture, empty stage included, not just what is drawn
- Lessons 18 and 21 say Shift+, (Control > Rewind) to go back to frame 1, instead of Home

- Opening a SWF movie keeps the stacking inside a shape drawn in several style groups: a nose no longer shows the outline of the mouth behind it

- Time Remap: Blend between frames cross-fades between a graphic's frames in slow motion

- 3D: a light that shades movie clips as they turn, and depth of field that blurs them away from a focus depth (Lights and Depth... in the 3D section of the Properties panel)

- Audio Spectrum and Audio Waveform behaviors: bars and dots that dance to the scene's sound, in a row or round a circle; ctx.spectrum() and ctx.waveform() for your own behaviors

- Behaviors: controls on a null (Slider, Angle, Point, Color and Checkbox Control) that Link to Control and your own behaviors follow; behaviors can tint
- Add Behavior > Browse Presets... shows ready-made animations and every behavior or letter animator moving before you add it

- Templates: checkbox fields (two-frame clips), lists (clips with labeled frames), a font for each text field, Pin to Text to stretch or move a part with the words, and responsive time (stretch and outro labels) so intros and outros keep their timing

- Control > Motion Sketch: press and move on the stage while the movie plays, and the selected instance follows the path you draw

- Layers: Echo (a trail of earlier poses) and Posterize Time; Motion Editor: Easy Ease, Ease In, Ease Out and a speed curve
- Letter animators: Flip In, Blur In and Source Text (keyed words); Animate Letters tips and swings letters in depth; Add Animator > Presets
- Shape operators: Twist, and Merge Paths (add, subtract, intersect, exclude)
- Masks: Mask Edge feathers, grows, shrinks or inverts a mask; Layer Mode sets a layer to Stencil, Silhouette or Preserve Transparency
- Adjustment layers: 25 new effects: directional, radial and lens blur, Glow, Gradient Ramp, 4-Color Gradient, Fill, Outline, Fractal Noise, Bulge, Ripple, Bend, Displacement Map, Levels, Curves, Hue/Saturation, Tritone, Vignette, Grain, Chromatic Aberration, and linear, radial, blinds and dissolve wipes
- Letter animators: Text on a Path lays text along a motion guide with a margin to key; Wiggly shakes each letter on its own
- Strokes: Dashes (with an offset to key) and Taper (start and end width, a travelling wave) shape operators; Apply to Strokes in the Color panel gives lines a gradient; width profiles now survive other shape operators
- Time Remap behavior: key which frame of a graphic shows, for slow motion, freezes and running it backwards
- Motion blur: turn it on for a layer from its right-click menu; the shutter angle and samples are on the Doc tab
- Opening a SWF movie brings back its scenes, by name, instead of one long scene
- SWF export: the scene's event, start and stop sounds go in as event sounds of their own instead of being mixed into the stream
- Paste in Place with frames chosen on the timeline pastes onto every keyframe they cover
- Motion Editor: color and filter curves stay within what each property can be (tint channels 0 to 255, no blur below 0)

## 0.12.0

- Ctrl-dragging an outline with the Selection tool puts a new corner where you grab it and drags that, instead of moving the next corner along
- Export FLA for Other Apps carries inverted track mattes (Layer and Erase blending) and adjustment-layer art (a mask over the filtered layers); SVG exports draw adjustment layers
- Many copies of a graphic (crowds, grids, particles) draw from one picture of it: a thousand copies of a detailed symbol went from 433 ms a frame to 5 ms
- Bone springs: a bone of an armature of symbols can trail, overshoot and settle as the armature moves, set with Spring and Damping when you click it with the Bone tool
- Lessons: steps that ask for several settings wait for Next, and the Behaviors panel's Seed box shows its whole number
- Shape hints steer a shape tween's outline along with its fill, so the two stay together through the morph
- A mask with a shape tween no longer cuts off between its keyframes
- Tutorials: Show Final opens how a lesson turns out in a tab of its own, from the step window or the Tutorials dialog
- A line or shape just drawn bends when dragged with the Selection tool (Ctrl adds a corner) rather than moving; click it first to move it
- The Text tool's Family and Text type lists keep their arrows in a narrow Properties panel
- Templates: sliders (a named clip of several frames holds on the frame an instance picks) and image fields (a named clip holding a bitmap shows an image file instead)
- Template fields are filled in under Template in the Properties panel
- Render Queue: Add Data... renders a movie for each row of a CSV file in the background
- Link to Object: pick the instance by clicking it on the stage, drive any change from any other, with an amount, offset and limits; a dashed line shows each link
- Phoenix play --image saves the frame the movie ends on as Test Movie shows it; tools/consistency_check.py compares it with exported stills and movies
- Text no longer comes out as empty boxes from Phoenix render, play, batch and the other commands run with no window
- Adjustment layers: Turbulent Displace and Wave Displace push the picture below about, in random swirls or traveling waves
- Baking behaviors, shape operators and letter animators keeps a keyframe only where something changes, holding the frames between

## 0.11.0

- Commands > Open Claude Code in a Terminal Window is gone: Claude Code opens in its panel (Commands > Open Claude Code), which can float on its own
- Asset Warp: turned pins, frozen pins and bones between pins in files are drawn as they bend the art; with the Asset Warp tool, Shift-drag turns a pin, F freezes it and Ctrl-click joins two pins with a bone
- Export FLA for Other Apps also carries track mattes (as masks), adjustment layers (as filters on a movie clip) and template values (as symbol copies); Phoenix --convert in out --for-other-apps does the same from the command line
- A mask that moves with a classic tween masks where it has moved to, rather than only where it started
- AI assistants can set behaviors, shape operators and letter animators, make track mattes and adjustment layers, fill in template fields and render from data, and rename layers, symbols or instances to a naming convention
- Pausing Test Movie holds its sounds too, and they play on from where they were
- Behaviors: Loop repeats keyed motion (cycle, back and forth, continue); Jiggle sways parts on a spring as they are carried, through layer parenting; Scatter in a Shape places copies at random inside a guide's outline
- Shape operators: Offset Path, Round Corners, Pucker and Bloat and Repeater; Animate Letters can also space out, color and blur letters
- A new lesson, A bouncy antenna
- Playing or scrubbing within a keyframe keeps the selection, so panels stay on it; the layer list marks adjustment layers and layers shown through a track matte
- Eight new lessons in Help > Tutorials: behaviors, writing your own behavior, a crowd from one cycle, draw-on lines, an animated title, particles, track mattes and adjustment layers, and lower thirds from a template
- Add Behavior > New Behavior... starts a behavior of your own from an example and opens it in the script editor; Open Behaviors Folder shows where they are kept
- Symbol templates: named dynamic text and movie clips in a symbol become fields each instance fills with its own words and colors (Modify > Symbol > Template Fields); File > Export > Render from Data, or Phoenix render --data, renders a movie for each row of a CSV file
- Dynamic and input text no longer show their dotted box in exported images and movies
- Behaviors: Follow Sound moves, scales, turns, fades or changes the frame of an instance with how loud the scene's sound is; your own behaviors can ask ctx.loudness()
- Track mattes: a layer can show through the layer above it, by its alpha or its lightness, soft edges and all; adjustment layers blur, color-correct or tint everything below them, with keyable settings
- Letter animators on text: Typewriter, Cascade In, Bounce In, Fade by Word, Scramble, Wave and Animate Letters, the text staying editable; baked into a text field a letter
- Shape operators: Trim Paths draws a line on or off, Wiggle Path boils outlines, Zig-zag puts teeth or waves along them; keyed in the Behaviors panel, baked like behaviors
- Behaviors: Particles sends out copies that are born, fly, fall, spin, grow and fade, the same on every playback
- Test Movie keeps time with its sound: frames follow the clock instead of drifting behind when drawing is slow
- Behaviors: Look At, Link to Object and Link to Controller follow other instances by name or a master controller; Effector changes copies by distance, position or order; the Function view draws what behaviors make of a property as a dashed curve
- Behaviors: Repeat along a Path and Follow Path place copies, or the instance, along the line on a motion guide
- Behaviors: settings can be keyed over time; Repeat in a Circle, Repeat in a Line or Grid and Stagger draw copies; Bake to Keyframes, and File > Export > Export FLA for Other Apps writes a copy with behaviors baked
- Behaviors: Window > Behaviors gives symbol instances motion worked out every frame, with no keyframes: Wiggle, Oscillate, Spin, Pulse, Fade In and Out, Blink and Follow with Lag; your own behaviors are JavaScript files in the Behaviors folder

## 0.10.0

- Default keys follow the standard FLA-editing layout: B for the Brush and Shift+B for the Nib Brush; new Modify > Align and Text menus (Ctrl+Alt+1 to 8, Ctrl+Shift+B/I/L/C/R/J); Close All, Find Next, Hide Edges, Show Shape Hints and Phoenix Help (F1); keys for Pasteboard and Snap to Objects
- The Update Ready message shows over the start screen instead of hiding behind it
- Python scripts can listen for events with fl.addEventListener, as JSFL scripts do
- Commands > Manage Extensions installs extension packages (.zxp, or a folder) whose commands and tools join Phoenix, and removes them; opening or dropping a .zxp installs it
- Text in Illustrator and PDF files comes in as editable text, in its own fonts; EPS text is spaced by the fonts' widths
- SWF movies compressed with LZMA open, and fonts of the newer embedded kind keep their names
- Flash CS3 and CS4 .fla files open whole: shapes drawn with curves and frames with custom eases no longer leave parts out
- Exporting from the command line no longer waits on a message box when a document opens with a warning
- Test Movie plays a button's sounds: Over on rolling over, Down on pressing, Hit on releasing, Up on rolling out; opened SWF movies keep them
- The Asset Warp tool makes a puppet from a shape, bitmap, group or graphic: click it, and the first pin goes where you clicked
- Window > Collapse Right (or Left) Panels to Icons folds a side's panels into a strip of their names, each opening beside it, for more stage
- Adding a bone to a posed armature keeps the eases between its poses
- Test Movie: click an input text field and type into it (Selection.setFocus and stage.focus too), and sounds placed on keyframes play
- Test Movie runs ActionScript 3 class files: the document class, on the main timeline before its first frame, and symbols' linked classes on their clips
- A new page in the guide, ActionScript and Test Movie, covers the Actions panel and what runs in Test Movie
- Playing on the stage runs on into the next scene, and looping the whole scene wraps from the last scene to the first
- Orient to path on the Frame tab, for classic and motion tweens
- Pose commands for armatures: Insert, Copy, Paste and Clear Pose, from the frame menu and Edit > Timeline > Pose
- Library > Properties... sets a symbol's type, class and 9-slice scaling; with 9-slice on, drag its guides inside the symbol
- Import reads EPS and PostScript files: Illustrator EPS through the artwork inside, and any other EPS (FreeHand, Photoshop and the rest) by running its PostScript
- Illustrator import reads many more files: ones saved without PDF content and ones from Illustrator 8 and older (Illustrator's own format, with gradients, clipping masks and text), and in PDF-based files spot colors, clipping masks, repeating patterns and palette images
- Phoenix --export-image draws artwork files (SVG, Illustrator, EPS, PDF) to an image
- Phoenix --view opens a file in a player window without the editor: documents, FLA, XFL and SWF movies play, SVG, Illustrator, PDF, Photoshop files and images show; on Windows, View in Phoenix in the right-click menu and Phoenix Viewer under Open with
- Test Movie has a time slider, icon buttons, and shows only the stage
- Textured brush strokes look the same in Animate: FLA and XFL files carry the grain as a repeating bitmap fill, and Phoenix reads them back as live textures
- Line art draws faster: a stroke's outline is made once and kept, not again on every repaint
- Scripts can listen for events with fl.addEventListener: documents opened, made, saved, closed and switched, the timeline, layer, frame and selection changing, and publishing
- JSFL tools: scripts in the Tools folder become tools in the Tools panel, previewing on fl.drawingLayer as you drag and drawing shapes with path.makeShape(), with their settings from an options file
- File > Open reads SWF movies, bringing a published movie back as a document: shapes, symbols, a layer for each depth, bitmaps, sounds and text, for when only the .swf survives
- Palettes shared between documents: save a palette to a file and link it from other documents, and color changes in one reach the rest
- SWF movies carry the sound, streamed with the frames, and Publish Settings chooses its compression: MP3, ADPCM, raw or none, with its quality, sample rate and channels

## 0.9.0

- Phoenix for Linux: a 64-bit AppImage that runs from the file and updates itself
- Control > Test Movie (Ctrl+Enter) plays the scene with its ActionScript 1 and 2: frame scripts, button handlers and clip events run, buttons answer the mouse, and what scripts trace and the errors they stop on go to the Output panel, so old Flash games such as dress-up games can be played
- Window > Actions (F9) writes ActionScript: the script of the frame at the playhead, or of the selected button or movie clip, edited as you type with each script one step to undo, a script navigator listing every script in the document, a pin, and syntax checks that mark the line in error
- Commands > Script Editor writes, opens, saves and runs JSFL and Python scripts (Ctrl+Enter), and edits ActionScript files, in the same editor: colored code, kept indents, find and replace, go to line and toggle comment
- Test Movie plays every scene in turn and takes the keyboard (Key.isDown, keyPress handlers, key listeners), and has Pause and Step
- Test Movie runs ActionScript 3 frame scripts with addEventListener: clicks and the other mouse events on movie clips and buttons, enterFrame, and key events on the stage
- Test Movie runs what games most often use: text fields set from scripts and fields showing a variable, startDrag and stopDrag with the drop target, hitTest and hitTestObject, the mouse position, attachMovie and duplicateMovieClip, new and addChild for symbols with a class, library sounds from scripts, and getTimer
- In Test Movie, buttons and clips are clicked by their art rather than their bounding box
- Double-clicking a script error in the Output panel opens the script in the Actions panel at its line
- The Actions panel's Target button inserts the path to a named movie clip or button
- Code hints in the Actions panel and Script Editor: a dot offers what can follow it, and Ctrl+Space every word that fits, instance names included
- The Actions panel's Snippets menu puts in ready-made ActionScript 2 and 3 for clicks, dragging, keys, text fields and sounds
- `Phoenix play <file>` plays a movie's scripts with no window and prints what they trace and the errors they stop on; `--click-all` clicks every button
- ActionScript fixes: `x += 1` and the other compound assignments, `++x`, `a[i] = v`, and `new Object()` and `new Array()` work as they should
- Commands > Open Claude Code now runs Claude Code in a panel inside Phoenix, with copy, paste and scrollback; Open Claude Code in a Terminal Window still opens it in a terminal of its own

## 0.8.0

- Commands > Open Claude Code opens Claude Code in a terminal, already connected to Phoenix and let in without asking
- Themes reach more of the window: transform handles, the selection marquee, color swatch borders, the X-sheet and Motion Editor playheads, and the Outliner's icons, which now show on light themes too
- Bones can be added to an armature that already has several poses, on any frame: in the other poses the new bone turns with the one it hangs from
- Scripts can use the same operations as AI assistants: `fl.phoenix.call("in_betweens", {...})`, with `fl.phoenix.operations()` to list them
- AI assistants can make in-betweens between two keyframes, turn poses into a walk or run cycle, lip sync a mouth to a sound or cues, recolor artwork to a palette, and show a color script of the scene
- JSFL on macOS: saving or writing to a file that does not exist yet keeps its whole path (`/Users/...`, `/var/...`) instead of losing the first folder
- Trace Bitmap draws cleaner edges: soft and noisy borders between colors trace as straight lines instead of steps and spikes, upright and level edges stay exactly so, and the thin halos and slivers along edges fold away, with far fewer specks and points
- In dialogs, Enter in a number field keeps the value typed and presses the dialog's main button, as it did before number fields became hot text; F2 opens a field for typing
- No more faint lines of the background between fills that meet, in traced artwork and any shape with neighboring fills, on the stage and in exports
- Trace Bitmap fits curves: rounds come out smooth, while straight sides and corners stay sharp (Curve fit Tight and smoother; Pixels and Very Tight keep the pixels' steps)

## 0.7.0

- JSFL scripts: run them from the new Commands menu, keep your own in a Commands folder, and see their output and errors in the Output panel (F2); each run undoes in one step
- `Phoenix script.jsfl` runs a script in the open Phoenix, and `Phoenix run-script` runs one on documents with no window
- Older scripts that use E4X XML (`x.@id`, `x..item`, XML literals) run
- Scripts can ask for their settings in XUL dialogs (`fl.xmlPanel`, `fl.xmlPanelFromString`), with handlers and `fl.xmlui`
- Python scripts: run `.py` files from the Commands menu or `Phoenix run-script`, with your own Python and its packages, using the same objects and names as JSFL
- `Phoenix batch --jsfl script.jsfl` runs a script on every document in a folder, and `--save` writes back what it changed
- Scripts can read a shape's outline (vertices, edges, contours, fills and strokes) and edit it, for exporters that walk shapes
- Scripts can keep named data on objects, library items and the document, saved with the file
- Scripts can copy, cut, paste and duplicate whole layers, move a layer into a folder, export SWF movies, publish, and run command lines
- Trace Bitmap previews on the stage: Preview traces the selected bitmaps in place with how many areas and points they make, Auto Preview traces again as settings change, and Cancel puts the bitmaps back; the docs walk through choosing settings on a sample picture
- Scripts can trace bitmaps with `document.traceBitmap`
- Right-click a bitmap on the stage for Trace Bitmap and Swap Bitmap
- Every number field, in dialogs as well as panels, is now hot text: drag it to scrub, click to type, Tab between fields, and step with the arrow keys or the wheel; no more tiny arrow buttons
- Safe zones and aspect-ratio frames over the stage (View > Safe Zones, or the button on the edit bar): title and action safe, thirds, and 16:9, 4:3, 1.85:1, 2.39:1, 1:1, 4:5, 9:16 or your own frames, with presets, a default for new documents, snapping to their edges, and an option to draw them into review exports

## 0.6.0

- Closing the last document brings back the start screen
- Document tabs: a right-click menu (Close Others, Close All, Copy Path, Show in Explorer) and Ctrl+Shift+Tab
- Move a tab to a window of its own, by its menu or by dragging it off the tab bar, and drag it back to merge
- Copy and paste between documents and windows brings the symbols, bitmaps and sounds it uses
- Quitting with several unsaved documents offers Save All, Discard All or a review of each
- Recovery copies cover every open document, and several can be recovered at once
- Phoenix reopens the documents that were open when you quit
- Each tab keeps its zoom and scroll, and documents in the background use less memory

- 3D Position and View: set a movie clip's depth, the perspective angle and the vanishing point
- Saving as FLA or XFL lists the Phoenix-only features the document uses; XFL folders keep them all
- Bones: symbol armatures play between poses, the Bone tool bones shapes, and the new Bind tool picks which bone each point follows
- AI assistants such as Claude can look at and change your document through MCP, with every change undoable
- An animated graphic's selection box and snapping follow the frame it shows, not its first
- The camera can fade, tint or dim the whole scene, keyed and tweened with its moves
- Scale handles snap exactly onto guides, grid lines and other artwork's edges and anchor points
- Merge drawing leaves no hairline gaps where shapes nearly share an edge, and rejoins curves an earlier merge cut once the cut is gone

## 0.5.2

- Interactive tutorials: nine guided lessons in Help > Tutorials, from your first animation to rigging
- Install page links straight to the latest installers

## 0.5.1

- Movie clip sounds play while the document plays, with View > Mute Movie Clip Sounds
- XFL folders open from their .xfl file
- Photoshop large documents (.psb) import
- Settings move to a new location; your old settings come with them
- Updated user guide and screenshots, showing the version they describe

## 0.5.0

- Stage controls on the edit bar: center, rotate, clip or dim outside the stage, and a zoom box
- Frame scripts: stop(), play() and gotos work in playback with Control > Enable Simple Frame Actions
- Movie clips play on the stage during playback
- Layer parenting can leave out the parent's scale, skew or flip
- 9-slice scaling works on rotated instances and with filters, blends and color effects
- Custom Stroke Style dialog for dashed, dotted, ragged, stipple and hatched strokes
- Paste SVG, PDF artwork and copied files from other apps; copies carry SVG out
- TLF text opens as classic text
- Older binary FLA files open more completely
- Render queue for exporting several movies in the background
- Publish, Publish Settings and SWF export
- Video export and import need nothing installed: ffmpeg comes with Phoenix
- Brush presets, tips, dynamics, Auto Fill and bitmap painting
- Better SVG, PDF and PSD import
- Animate Standard keyboard shortcuts, and importing shortcut files
- Toolbox color controls redrawn; Flow Brush renamed Brush
- Much more stable with large documents, long sessions and damaged files

## 0.4.0

- Motion tweens you can create and edit, with the Motion Editor, motion paths and motion presets
- Shape hints, graphic loop options, and Copy and Paste Motion
- Bone tool, Asset Warp tool, 3D rotation and translation, and the Camera tool
- Width tool, Paint Brush with art and pattern brushes, textured and bitmap brushes
- Rigging: pegs, deformers, function curves, master controllers and an X-sheet
- Multiplane camera with layer depth
- Lip sync from a recording or cue files
- Line and color art layers, effect layers and linked palettes
- Export sprite sheets, ProRes, SVG sequences and compositing passes
- Shot breakdown, linked shared assets, review notes and animatic import
- Document tabs, templates, scenes panel, Find and Replace, History panel
- Library upgrades: external libraries, use counts, folders and properties
- Text: paragraph settings, vertical text, spell check and font mapping
- Themes, workspaces and a dockable, customizable toolbox
- Command line tools: render, doctor, diff, batch and benchmark
- An HTML user guide

## 0.3.0

- Export movies, animated GIFs, images, PNG sequences and sound
- Classic, shape and object motion tweens play, with ease presets and custom eases
- IK armatures and Asset Warp puppets play
- Color panel with gradients and bitmap fills, Swatches panel
- Align, Transform and Info panels, and Combine Objects
- Edit filters, primitives and stroke styles in the Properties panel
- Import images, sounds and video
- Copy and paste through the system clipboard
- Layer Properties, layer menu and drag-to-reorder layers
- Phoenix document format (.phx), plus opening older binary FLA files
- Start screen, autosave and crash recovery
- Brushes with pen pressure and tilt, and the anchor point tools

## 0.2.0

- Signed and notarized macOS installer

## 0.1.0

- First release: open, edit, play and save FLA files with drawing tools, a timeline, symbols and a library
