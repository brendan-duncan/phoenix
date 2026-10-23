# Changelog

What changed in each release of Phoenix, newest first.

## 0.23.3

- On a layer moved by a peg or a parent layer, the Subselection tool's anchors and handles sit on the shape where it is drawn, and the Pen, Pencil and Magic Wand pick up its paths and fills there too
- Stepping or scrubbing to another keyframe keeps the selection: it moves to the same instance, bitmap or drawing on that keyframe, so the Properties panel and the stage stay on what you were editing
- Brush and eraser strokes keep up with the pen on the CPU stage, the web editor's without WebGPU included: the stroke under way is painted as it grows rather than from its start at every move, and the stage under it is drawn once for the stroke

## 0.23.2

- Drop a PHX, FLA, XFL or SWF file on the web editor to open it, on the window or the Welcome screen; on the desktop, SWF files and drops on the Welcome screen open too
- `Phoenix <file> --play` opens the file and plays it in Test Movie, filling the window
- `Phoenix <file> --tutorial <lesson>` runs the lesson on that file, rather than on a document of its own
- Text set in Arial, Helvetica, Times New Roman, Courier New, Calibri or Cambria is drawn in Liberation Sans, Serif or Mono, Carlito or Caladea when those are installed and the original is not: they have the same widths, so text keeps its layout, and the font no longer counts as missing
- Fonts in the folder `PHOENIX_FONTS_DIR` names are used beside the system's

## 0.23.1

- ActionScript 2 has flash.geom's Point, Rectangle, Matrix, ColorTransform and Transform (and a movie clip's transform) in movies made for SWF 8 or later; x, y, alpha and the other ActionScript 3 names are ordinary variables in a movie's compiled ActionScript 2; for-in visits properties newest first, NaN equals NaN, and joining an array writes undefined as undefined, as SWF movies have them
- ActionScript 2 has flash.display.BitmapData in movies made for SWF 8 or later, with the numbers its methods give in place of errors, and BitmapData.loadBitmap for a library bitmap; bitmaps read back the exact colors SWF movies give, and copyPixels, threshold, getColorBoundsRect, noise, perlinNoise and pixelDissolve work as they do in SWF movies, in ActionScript 3 too
- ActionScript 2 has flash.filters (Bevel, Blur, ColorMatrix, Convolution, DisplacementMap, DropShadow, Glow, GradientBevel and GradientGlow) and a movie clip's filters, keeping each number as SWF movies do, and BitmapData.applyFilter and generateFilterRect; an imported SWF's filter angles, strengths and blurs come in exactly
- ActionScript 2 has flash.external.ExternalInterface, which says it is not available (there is no web page around a movie in Phoenix), with its helpers that write values as XML and read them back
- ActionScript 3 has Namespace and QName, and a movie's namespace constants are Namespace objects
- ActionScript 3 has flash.xml's XMLDocument, XMLNode and XMLNodeType, parsing as ActionScript 1 and 2's XML does
- ActionScript 3 has flash.text.StyleSheet (parseCSS, setStyle, getStyle, transform) and TextField.styleSheet: HTML text set after takes each tag's and class's styles, as SWF movies show it; TextField.getTextRuns too
- The displacement map filter: DisplacementMapFilter in ActionScript 3 and 2 moves each pixel by a map bitmap's channels, on display objects and through BitmapData.applyFilter, in Test Movie
- ActionScript 3's transform.matrix3D: a display object given one is three-dimensional (its matrix reads null) and shows with its two-dimensional part; setting matrix or a null matrix3D makes it flat again; and a transform assigned to another object carries it
- Text with characters past the Basic Multilingual Plane (emoji) comes through ActionScript 3 whole, built a character at a time or compiled in halves
- Malformed XML in ActionScript 3 throws the error SWF movies throw for each mistake (an unclosed element, comment, CDATA section, attribute or processing instruction, a bad name, markup after the root), and an end tag may have spaces or attributes after its name
- In ActionScript 3, a class's prototype is one object whose properties its instances find, set or deleted, and whose constructor is the class
- ActionScript 3 has flash.utils.Proxy: a subclass's getProperty, setProperty, callProperty, hasProperty, deleteProperty and nextNameIndex, nextName and nextValue answer for the names it does not declare and for for-in and for each
- ActionScript 3's ByteArray writes and reads objects (writeObject, readObject) in AMF3 and AMF0, with registerClassAlias, getClassByAlias, ObjectEncoding and IExternalizable; for-in over an array in a movie's compiled ActionScript 3 gives its indexes as numbers
- ActionScript 3's SharedObject keeps its data in .sol files, as SharedObjects in SWF movies are kept, so dates, byte arrays and typed objects come back as they were; its size is its file's, and data saved by earlier versions of Phoenix is still read
- ActionScript 1 and 2 have SharedObject in movies made for SWF 6 or later: getLocal, data, flush, getSize and clear, kept in .sol files as SWF movies keep them and saved again as the movie ends
- ActionScript 1 and 2 have XML and XMLNode in movies made for SWF 5 or later: parsing as SWF movies do (leniently, with a status for what went wrong, the XML declaration and DOCTYPE kept), the node tree with its attributes, siblings, namespaces and idMap, toString, and XML.load and sendAndLoad from a file or the web
- ActionScript 1 and 2 have LoadVars (load, send, sendAndLoad, decode, toString); escape in ActionScript 1 and 2 encodes every character but letters and digits, and a property's getter that reads itself gets undefined, as SWF movies have them
- In a movie's compiled ActionScript 3, a name in more than one private namespace (a class's and its file's) is found in any of them, such as a helper class written after a package in the same file
- Test Movie says when a document class does not extend Sprite (the main timeline cannot be one), as SWF movies do
- ActionScript 2's movie clips have depths: getDepth, swapDepths, getNextHighestDepth and getInstanceAtDepth, and attachMovie, createEmptyMovieClip, duplicateMovieClip and attachBitmap put a clip at the depth given, in place of what was there; removeMovieClip takes off only what is at a script's depth; an imported SWF keeps each layer's depth, and clips placed without a name are named instance1, instance2... as SWF movies name them
- ActionScript 2 has the drawing API (lineStyle, beginFill, beginGradientFill, beginBitmapFill, moveTo, lineTo, curveTo, endFill, clear), localToGlobal, globalToLocal, getBounds and getRect worked out to the twip as SWF movies do, setMask, getSWFVersion, and a movie clip's blendMode, cacheAsBitmap, opaqueBackground and scrollRect
- ActionScript 2 has TextField and TextFormat: createTextField, every text field's properties and methods (text, htmlText, textColor, border, autoSize, scroll, maxChars, setTextFormat, getTextFormat, replaceText and the rest), the variable a field shows kept in step with the field, and its _x, _alpha and other properties, as SWF movies have them; an imported SWF's HTML text fields keep their HTML, and its text fields with no font are in Times New Roman, as SWF movies show them
- ActionScript 1 and 2 have AsBroadcaster, and Key, Mouse, Selection and text fields broadcast through it: Mouse's listeners hear onMouseDown, onMouseUp and onMouseMove
- Test Movie loads other movies and images as SWF movies ask: loadMovie, loadMovieNum and unloadMovie into clips and levels, MovieClipLoader with its onLoadStart, onLoadProgress, onLoadComplete, onLoadInit and onLoadError, loadVariables, and getBytesLoaded and getBytesTotal; a loaded movie's clips find its own library
- Phoenix's SWF movies write loadMovie, loadVariables and getURL's method as SWF players read them, so they load into the right target
- ActionScript runs faster: a string's length, sorting with a compare function, filling an array from its end, objects with many properties and Math's functions take a fraction of the time they did

## 0.23.0

- The web editor's phone layout has the stroke and fill colors on its tools row
- Drawing guides and the symmetry axis show in the web editor when the GPU draws the stage
- An imported SWF masks only on the frames where a mask is placed, so art at a depth used as a mask elsewhere in the movie no longer disappears
- ActionScript 3 has Vector3D, Matrix3D, PerspectiveProjection, Utils3D and Orientation3D, Matrix's row and column methods, and transform.perspectiveProjection; stack traces name classes as pkg::Class, static methods as Class$/method, and include the movie's own functions; large numbers print to fifteen digits
- Adjust Color makes the colors SWF movies published from FLA files show, on the stage, in Test Movie and in Phoenix's own SWF and HTML5 movies; an imported SWF's Adjust Color comes back with its settings
- Gradients blend from stop to stop as SWF movies have them: a gradient from clear black to white passes through a see-through gray, so it darkens what is under it, rather than fading white
- The Subtract blend mode is exact where art is see-through or soft-edged, and the Alpha blend mode leaves what is under it alone where the instance is clear or absent, as SWF movies have them
- In Test Movie, scripts read a tweened movie clip's position and alpha where the tween has them on that frame, and its width and height as an SWF movie measures them, without the margin left for smoothing edges
- Color Matrix and Convolution filters, added from Add Filter or brought in with an imported SWF movie, and published to SWF; an SWF's color matrix comes back as Adjust Color where it is one
- Pins in the States panel keep instances at their distance from the window's edges, or stretch them between two edges, so a published page lays itself out to fill any window
- Scrolling in the States panel: a movie clip scrolls inside a box, dragged, flicked or wheeled, gliding to a stop and springing back from its ends
- Bindings in the States panel show an input on the stage, kept up to date: a text field's text, or a movie clip's frame, place, turn, fade, size or visibility
- Published HTML5 pages let the page read and set the movie's inputs, fire its triggers and hear when its state changes
- States can follow a number: a state shows the frame a number input picks and keeps up as it changes, for gauges, dials and faces from sad to happy
- Layers of states: a movie clip on the timeline gets states of its own that play at the same time as the timeline's, going by the same inputs
- More slice guides: a symbol can have any number of guides across and down, the strips between them taking turns keeping their size and stretching, such as a panel whose top edge has a tab that keeps its size
- Soft edges on vector art: Feather in the Properties panel fades a fill's or a stroke's edge over the pixels you choose, for soft shading, glows and shadows that stay vectors
- Joysticks in the Controllers panel: a handle dragged about a square drives some properties across and others down, such as a head that turns and nods, and can pick a graphic's drawing from a grid of them

## 0.22.0

- The web editor has a phone layout: the stage, large buttons for the common tools and playing (with loop, onion skin and a time slider), the timeline and Properties as drawers that slide over the stage and close when you tap off them, and the other panels one at a time; a long press on the stage opens the pop-up palette, and a button sets a turned stage straight; phones get it the first time, and View > Phone Layout switches it
- The web editor asks before a tab with unsaved changes closes, starts tablets in the Simple workspace, plays sound more smoothly, and gives the commands on keys the browser keeps (Ctrl+N, Ctrl+W, Ctrl+T, Ctrl+Tab) keys with Alt
- The stage is drawn on the GPU (WebGPU), several times faster than before, in the desktop app and the web editor; where WebGPU is missing or lacks what the stage needs, the CPU draws it as before, and View > Stage Renderer switches between GPU and CPU
- Playing is several times faster with a script tool installed, such as Rig Tool: tools and scripts not listening for an event no longer redraw everything from scratch on each frame
- Phoenix is built on Qt 6.12, a long-term support release

## 0.21.3

- SWF panels in the Configuration folder's WindowSWF show under Window > Extensions, and their buttons run JSFL through MMExecute
- SWF movies in script dialogs can call JSFL too, reaching the dialog's controls through fl.xmlui
- Test Movie: interface components such as buttons build without a stack overflow, and art drawn over a button leaves it its clicks
- Script errors in Test Movie say what the error was rather than [object Error]
- Phoenix runs in a browser: the web editor at phoenixanimate.com/webeditor opens, draws, animates, plays and saves documents, with files opened from the browser and saved as downloads

## 0.21.2

- Inserting a keyframe inside a classic or shape tween puts the in-between pose on it and keeps both halves tweened with the same ease
- Publish Settings has Include hidden layers for the SWF movie and Test Movie, on unless a file's settings turn it off
- JSFL: `document.viewMatrix`, `document.canTestMovie()`, `fl.reloadTools()`, `fl.Math.transformPoint` and `enterEditMode('inPlace')`
- JSFL: `fl.selectTool` says whether the tool was there, and finds a script tool by its full name, such as "Rig Tool"
- JSFL: `testMovie()` plays the document as the script has it then, and the script goes on once Test Movie is closed
- JSFL: `setSelectedFrames` moves the playhead to the last of the frames, selects what is there, and shows the frames selected in the timeline
- JSFL: leaving a symbol with `exitEditMode()` keeps its instance selected
- JSFL: a shape's `x` and `y` are the middle of its box, and `exportPublishProfileString` gives the profile itself, so scripts can copy a profile and put it back
- JSFL tools get Ctrl instead of switching to the Selection tool, `fl.tools.constrainPoint` keeps to 45° only while Shift is held, and a tool no longer freezes Phoenix when it moves the playhead

## 0.21.1

- Clicking a fill or stroke swatch (toolbox or Properties) opens a swatch pop-up: the document's colors, a row of gradients, recent colors, hex and alpha fields, No Color and the full color picker
- Pick a gradient from the pop-up to fill the selected shape with it, fitted to the shape, or to draw new shapes with it
- A shape's gradient fill or stroke now shows in its swatch on the Properties panel, and a gradient swatch can be picked for a shape's stroke
- Gradient swatches saved in an FLA show in the pop-up; documents without any get white to black, color and ramp gradients
- The Swatches panel shows the gradient swatches under the colors: click one to fill with it, Add Swatch keeps a gradient fill, and right-click deletes one; FLA files save them
- Moving over the swatch pop-up shows each color or gradient on the selected shapes; nothing changes until you click, and Esc puts them back
- No Color from the toolbox takes the fill or stroke off the selected shapes too, keeping it on a shape that has nothing else
- Text, tint, filter, stage, grid, guide and other color swatches open the swatch pop-up too, with the full color picker a click away
- Rectangle and oval primitives show the swatch under the pointer too, and the toolbox swatches color selected primitives
- Right-click a gradient swatch in the Swatches panel to duplicate it

## 0.21.0

- First-use hints are off until you turn them on with Help > Show Hints
- Grayscale has a button beside Mirror View on the edit bar; the Simple workspace is chosen from Window > Workspaces instead of the edit bar
- View > View Document... is gone from the View menu
- Insert Inbetween (Modify > Timeline, or right-click the frames) draws a new keyframe between two drawings: halfway, a third, two thirds, or favoring either key
- Fill Inbetweens draws every empty frame between two drawings, spaced evenly, easing in or easing out
- Inbetweens follow shape hints, move symbols part way, and are ordinary keyframes you can touch up
- File > Share saves an animated GIF, animated WebP, animated PNG, MP4 or WebM for the web and social sites
- Share offers each social site's size, frame rate, how many times it plays, quality, a transparent background, and a guess at the file size before you save
- After sharing, Show in Folder opens the folder with the file selected
- The render command writes .webp, .apng and .webm too, with --quality, --fps and --fit
- View > Mirror View (Shift+M, or the mirror button on the edit bar) flips the view of the stage left to right to check a drawing; every tool still works, and the art is not changed
- View > Grayscale shows the stage in shades of gray to check light and dark values; the colors, color pickers and exports are not changed
- Reference images: pin pictures beside or over the stage (View > Reference Images, or Alt-drop picture files on the stage); move, size, turn, fade, lock and hide them, and pick their colors with the Eyedropper
- Reference images are saved with the document, the same in every scene, and never exported or published
- View > Canvas Only (Tab) puts away every panel and toolbar to leave the stage alone; Tab again brings them back as they were
- Touch screens: pinch to zoom, twist two fingers to turn the view, and tap with two fingers to undo
- Pop-up palette: press ` over the stage for your favorite tools, recent colors and a color wheel at the pointer (works with a pen; can open on right-click too)
- Color panel: a color wheel for the fill or stroke, and a row of the colors you drew with lately, remembered between sessions
- Simple workspace: just the stage, timeline, common tools, Properties, Library and Color, one click from the full layout on the edit bar or the start screen
- First-use hints: a short tip on what to try next the first time you draw, add a keyframe, make a symbol or make a tween (Help > Show Hints, Reset Hints)
- Starter templates on the start screen: a bouncing ball, a rigged walk cycle, a lip-sync head and a title card, each opened as a new document
- Paint Bucket Scribble: drag across a drawing and every area you pass over fills, with a preview while you drag
- Paint Bucket Lasso: draw a loop and every area inside it fills; Only empty regions keeps lines and earlier colors safe
- Fill on: one Paint Bucket click fills the same area on every drawing in the selected frames, the onion skin range or the rest of the layer
- Follow moving regions finds the area on each drawing as it moves, and the status bar lists frames where nothing matched
- File > Import > Import Paper Drawings: scanned or photographed pencil drawings cleaned up and traced into vector lines, one to a keyframe
- Paper cleanup evens out shadows and paper tint, finds the lines by darkness, removes specks, closes small gaps and smooths, with a live preview
- Symmetry drawing (View > Symmetry, or the toolbox button): Pencil, Paint Brush, Brush, Nib Brush, Ink Brush and Eraser strokes are copied across a mirror axis, four ways, or round a center with as many copies as you like, optionally mirrored
- The Drawing Guides tool places rulers, parallel rulers, ellipses, concentric ellipses and 1-, 2- and 3-point perspective that freehand strokes snap to, with a magnetism setting; guides are saved with the document and never exported
- A Stabilizer for the Pencil, Paint Brush, Brush, Nib Brush and Eraser, as the Ink Brush has: the line trails the pen on a string, shown as a circle round the line's end, and catches up when you lift
- The status bar shows what the tool does and what Shift, Alt and Ctrl add, changing as you hold the keys; View > Tool Hints turns it off
- Window > States: make a movie clip or the whole movie answer clicks and keys with no code; frame labels become states that loop, play once or hold
- Transitions between states go on a click, press, release or hover on a named button or clip, a key, the end of a state's animation, a timer or an input's value
- Inputs (on/off switches, numbers and triggers) and listeners that set them when something is clicked or a key is pressed
- The states are written as ordinary ActionScript 3, or 1 and 2, so they play in Test Movie, SWF and HTML5 pages and other apps
- A new tutorial, also a lesson in Help > Tutorials: a pet that answers clicks
- Test Movie runs ActionScript 1 and 2's createEmptyMovieClip, and handlers set by scripts such as onRelease and onEnterFrame
- Many brush strokes of one color crossing in a small area, with Object Drawing off or with radial symmetry, no longer merge with pieces missing

## 0.20.0

- A boned shape whose last points are bound to no bone keeps the binding of its other points when posed or rebound
- Gravity and wind for springy bones (Pose > Gravity and Wind): hair, tails and plants hang and sway with no poses, gusting the same way every time
- Smooth Joints (Pose > Smooth Joints) blends a boned shape's points between the bones at each joint, so elbows and knees bend in a curve
- The Bind tool shows how much of each point the chosen bone moves, and Alt-click or Alt+Shift-click gives it more or less
- Six new tutorials, also lessons in Help > Tutorials: a head turn on a master controller, a multiplane camera move, a cycle with Loop and Function Curves, linked palettes, linked master symbols, and timing with the X-sheet
- Onion skin Shift and Trace: slide, turn or resize the faint copy of another frame to trace over it, without changing that frame; Reset Shift and Trace puts them back
- Modify > Deformer > Corner Pin: drag a symbol's four corners anywhere and its art follows in true perspective, for signs, screens and floors; shapes, text and bitmaps are put in a symbol first
- Corner pins tween between keyframes and show in movies, SWF movies and HTML5 pages
- Sort by Depth (right-click a layer): its movie clips are drawn by their Z, the furthest behind, and swap places as Z tweens
- Export FLA for Other Apps lists a depth-sorted layer's clips in depth order, a keyframe a frame where they pass each other
- Export FLA for Other Apps draws bent instances (corner pins, curve and envelope deformers) into bitmaps, so other apps show them bent
- Bone corrections: as a bone turns, it can correct the boned shape it bends, such as an elbow that keeps its crease, drawn with the bone turned
- A bone can swap a graphic's drawings as it turns, for a hand or a head that turns, or fade an instance in or out
- Bones that drive a correction are ringed in orange; the Bone Corrections dialog lists, changes and removes what each bone drives
- Bone corrections play in exported movies and images, and are drawn in when publishing HTML5, exporting SWF and exporting FLA for other apps
- Right-click a bone for a bone menu: lock it, give it a target, and set its options
- Lock a bone, such as a foot, and it stays planted on the stage while you pose the rest; unlock it on the frame where it lifts
- Drag the joint at an armature's root to move the whole armature, its locked bones staying put
- A bone can reach for a point on the stage, which glides from pose to pose, or for another bone
- Independent Angle keeps a bone's angle on the stage as the bones above it turn, so a foot stays flat as the leg swings
- Squash and Stretch: Alt-drag a bone to stretch it and its art, optionally thinning as it grows
- IK Stretch lets a limb grow, up to the amount you choose, to reach past its length
- Locks, targets and stretches are worked into ordinary poses, so other apps play the same motion
- The guide has six new tutorials, also lessons in Help > Tutorials: bones between symbols, bones inside a shape and the Bind tool, bone springs, Asset Warp pins on a picture, the Curve Deformer, and the Frame Picker
- Ctrl-click with the Bind tool unbinds a point, and with the Asset Warp tool joins two pins with a bone, instead of calling up the Selection tool
- A symbol bent by a deformer is picked by clicking where it shows bent, not where it was drawn

## 0.19.0

- The scene name above the stage drops down: switch scenes, add, duplicate or delete one, or open the Scene panel
- Old binary FLA files saved again as FLA open in other apps: the saved file names its XFL version, and JPEG images are marked as JPEGs with their size
- Old binary FLA files open on the scene they were saved on, not always the first
- Text in old binary FLA files keeps its line breaks and grows to fit its lines, as it was made, instead of wrapping in a fixed box
- Motion tweens in old binary FLA files keep their clockwise and counterclockwise turns
- Text runs of only spaces, such as the space between two differently styled words, are no longer lost when an FLA opens
- Fill and stroke colors with transparency open at exactly the opacity the FLA gives, not one step lower
- View > Guides > Lock Guides is kept in the document, as FLA files keep it, instead of being one setting for every file
- Bone springs and joint limits from FLA files play as saved: an arm or tail swings, overshoots and settles just as the file has it
- Bone springs and joint limits are saved the way FLA files keep them, so other apps read them
- Springs are worked into every frame of an armature, so the bones swing with the art, and an armature can end swung past its last pose
- Posing a boned shape from an FLA file bends it by the binding saved with it
- The guide has a Lip Sync tutorial, also a lesson in Help > Tutorials: key a talking face's mouth to a line of dialogue, then touch up a shape with the Frame Picker
- Lip Sync listens to a sound already in the library, starting with the one on the timeline, as well as to a recording on disk
- Lip Sync's mouth chart fills itself in from the mouth symbol's frame labels: label each frame A, B, C... and each shape goes to its frame
- Lip sync without Rhubarb opens the mouth with the first sound of a word, rather than a frame or two late
- The Frame Picker captions each picture with its frame label as well as its number, such as "2 B" on a mouth chart
- The Lip Sync window shows a whole mouth chart without scrolling

## 0.18.2

- The guide covers much more: new pages on importing artwork, 3D and video, and filters, text, tweens, export, sound, symbols, the timeline, color and the drawing tools in depth, with new pictures
- Dragging the edge of a brush stroke or other smooth outline with the Selection tool bends it in one smooth curve, spreading along the outline instead of creasing at the point grabbed
- Motion tweens from FLA files play their 3D: clips turned about X and Y and moved in Z show in perspective, at the file's view angle and vanishing point
- Motion tweens from FLA files ease each property by its own Motion Editor ease: eases in and out, stop and start, bounce in and custom curves
- A clip that FLA files place with a 3D matrix opens where it belongs, and saves back as it was
- FLA camera layers play: pan, zoom, rotation and color effect, tweened; layers attached to the camera stay put and keep their color
- The Camera tool moves an FLA file's camera layer, keying its keyframes as the file keeps them
- HTML5 pages and SWF movies show the 3D motion and the camera as the stage does

## 0.18.1

- Old binary FLA files: scripts written in normal mode, and the action records of the oldest files, open as the ActionScript they stand for, so their buttons, frames and clips work in Test Movie
- Test Movie runs ActionScript 1 as old movies use it: a variable never set reads as undefined, `add`, `eq`, `ne`, `and`, `or` and `not` work, and so do `set()`, `eval()` and `call()`
- Saving as XFL works when a library item's name has a character no file name can hold, such as `?`: its file gets a safe name and the item keeps its own
- Command line: `play --click` and `--drag` press ActionScript 1 and 2 buttons as the window does, so drag and drop movies can be checked
- Old binary FLA files saved with a printer chosen in Page Setup open in full
- The Brush, Nib Brush and Ink Brush paint in the fill color with Fill unchecked, and the Pencil and Paint Brush draw with the stroke's color, weight and brush with Stroke unchecked, not a black hairline
- Old binary FLA files whose buttons carry scripts saved by Flash 8 open in full
- ActionScript 2's `_global` works in Test Movie: what a script sets there every script sees
- The Tools panel's colors are one unit
- The Tools panel adds columns when the window is too short to show every tool, and goes back to the columns you chose when there is room; along the top or bottom it adds rows

## 0.18.0

- Strokes drawn with FLA files' art and pattern paint brushes draw on the stage and in exports and SWF movies, and save as they were; the Paint Brush can draw with the file's brushes, listed under Brushes in this document
- Brush Library (Window menu): brushes to paint with in any document, by category, with pictures; Phoenix comes with brushes of its own, and Import Brushes from Document adds a document's brushes to your library
- A brush from the Brush Library is copied into the document with the first stroke drawn with it, and saved in the file as FLA files keep brushes
- Copying or pasting strokes, frames or symbols to another document, or importing symbols from one, brings the paint brushes they use
- Document Brushes lists the document's paint brushes with pictures and stroke counts, and selects, renames, takes off or removes unused brushes
- Custom Stroke Style sets a stroke's kind and weight, and for a paint brush stroke the brush and its options, changing every stroke drawn with it or kept as a new brush
- Paint brush strokes drawn with a pen follow its pressure and tilt within the brush's narrowest and widest, and show their width profiles
- Pattern brushes turn sharp corners as their corner option says (centered, between, sliced or overlapping), and art brushes bend round them
- Width profiles changed with the Width tool or a pen are saved in FLA and XFL files
- SWF movies keep the movie clips, buttons and text fields inside graphic symbols as real clips scripts reach by name, the graphic's frames placed into the timeline as it loops, plays once or holds a frame
- SWF movies: shape tweens go in as morph shapes the player blends, buttons keep their masks, and buttons play the sounds on their Up, Over, Down and Hit frames
- SWF movies: the soundtrack covers every scene, played in a row, and event sounds start on the timeline they are on, so a movie clip a script stops starts no more of its sounds
- ActionScript 1 and 2 compiled into SWF movies runs a `finally` block when `break`, `continue` or `return` leaves its `try`
- Test Movie runs a movie clip's `on()` handlers, and runs them and its `onClipEvent()` handlers on the clip itself, as players do
- HTML5 Canvas scripts draw in Test Movie: `createjs.Shape` and its `Graphics` (fills, strokes, gradients, bitmap fills, arcs, shapes and the short forms), `Text`, `Bitmap`, `Container`, `Sprite` with `SpriteSheet`, and `MovieClip` timelines made in code
- HTML5 Canvas scripts' filters, `cache()`, `shadow`, masks and `compositeOperation` show in Test Movie, and `skewX`, `skewY`, `regX` and `regY` move objects as in CreateJS
- HTML5 Canvas scripts reach an instance before its keyframe, and an instance placed again by a later keyframe keeps its listeners
- SWF movies with ActionScript 1 and 2 run their scripts when opened: Test Movie and `Phoenix play` run the compiled frame actions, `#initclip` actions, button `on()` and movie clip `onClipEvent()` handlers, with prototypes, ActionScript 2 classes, `Object.registerClass`, `onEnterFrame` and `setInterval`
- HTML5 pages draw Phoenix's own effects: behaviors, the multiplane camera, templates and effects a filter carries stay live; adjustment layers, effect layers, track mattes, soft masks, motion blur, shape operators, 3D and node graphs go as pictures of each frame, packed on WebP sheets
- HTML5 pages' sounds are MP3 (or AAC) from the ffmpeg Phoenix ships, WAV only without it
- HTML5 pages show TLF text as real text and compiled clips as the movie they carry
- HTML5 pages of ActionScript 2 movies draw the version 2 components (Button, CheckBox, RadioButton, TextInput, TextArea, Label, ComboBox, List, NumericStepper) from their parameters, with their events
- HTML5 Canvas pages' scripts draw as in Test Movie: CreateJS's whole Graphics API, Text, Bitmap, Sprite sheets, script-made clips, filters, shadows, masks, composite operations, skew and registration points
- Export FLA for Other Apps bakes Link to Control behaviors with the controls they follow
- HTML panels that ask for Node.js get it: `require` of their own files and packages, `fs`, `path`, `child_process`, `http` and `https`, `events`, `Buffer`, `zlib`, `crypto` and more, written into Phoenix; `cep.process` runs programs for any panel
- More of JSFL: the selection's stroke and fill (`setStroke`, `setStrokeStyle` and their kin), `fl.selectTool` and `fl.tools`, `zoomFactor`, publish profiles with `asVersion` and `docClass`, custom eases, primitive rectangles and ovals, `convertSelectionToBitmap` and `testMovie`
- Import takes FLA and XFL documents and SWF movies: their library, and their main timeline as a movie clip; scripts' `importFile` and `importEmbeddedSWF` too
- Deleting a sound or video still in use takes it off its frames, as one step to undo
- Script dialogs: a target list offers the timeline's named instances, and an embedded SWF panel plays in the dialog

## 0.17.1

- Publish Settings is laid out in two columns, where and what to publish beside the formats' options, so it fits a laptop's screen

## 0.17.0

- Publish configurations: Publish Settings can keep several, each with its own formats, options and location; File > Publish lists them, the current one on Shift+F12. A document with no publish settings now publishes an HTML5 page rather than an SWF movie and its page
- Publish says what it wrote: the files in the Output panel, and a dialog to open the page in the browser or show the files. Publish Settings has a Publish button and a Location: a parent folder (the document's by default) and a publish folder made in it for the files. A new document can be published before it is saved
- HTML5 pages play ActionScript 3 movies with their scripts: frame scripts, the document class, linked classes and the components are translated to JavaScript, with a source map back to the .as files; what does not translate is reported with its file or frame and line
- Published SWF movies carry their ActionScript 3, compiled: frame scripts, the document class, linked symbols' classes and the classes they use, and the fl.* components with their inspector parameters, so ActionScript 3 movies run outside Phoenix; what does not compile is reported with its file or frame and line
- Publish to HTML5: a web page that plays the movie in any current browser with Phoenix's own player, no SWF needed, with its symbols, tweens, masks, filters, blend modes, text, bitmaps and sounds; HTML5 Canvas documents' scripts run with CreateJS's API. Also from the command line with `--out page.html`
- HTML5 pages run ActionScript 1 and 2: frame scripts, button and clip handlers, `#initclip` and ActionScript 2 classes are translated to JavaScript, with movie clips, buttons, text fields, `Key`, `Mouse`, `Sound`, `Color`, `LoadVars`, `XML` and `SharedObject` as the scripts expect
- The HTML page Publish writes for an SWF movie plays it in today's browsers with Ruffle, loaded from the web
- HTML5 Canvas documents: make one with File > New (Platform), open and save them with their publish settings kept, and play their JavaScript in Test Movie with CreateJS's timelines, mouse and key events, Ticker, Tween, setTimeout, `console.log` and `alert`; the Actions panel checks their scripts as JavaScript
- SWF export and Publish keep the movie's structure and its ActionScript 1 and 2: movie clips, buttons, instance names, masks, labels and text fields go in as they are, and frame scripts, button and clip handlers and ActionScript 2 classes are compiled in, so published movies run in any player
- Published SWF movies run their ActionScript 3 in Test Movie: the classes compiled into the movie, its components among them, play as they were published, and a movie behind a preloader plays the movie it loads

## 0.16.1

- Parented layers from Animate files play as Animate shows them: a child turns with its parent but keeps its own size, so arms and legs rigged with layer parenting no longer shrink or flip
- Color settings of effects, text animators and behaviors have a swatch that opens the color picker, beside the value to type

## 0.16.0

- ActionScript 3 movies play with the flash.* classes in Test Movie: the display list, drawing, BitmapData, events, filters, text, ByteArray, Timer, loading files and images, SharedObject; library symbols are classes, frame scripts share their timeline, and classes load from the class path and Preferences
- Updates and downloads now come through phoenixanimate.com, and Help > Phoenix Help opens the guide there
- The guide has a Developers section: the .phx, FLA and binary FLA formats

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
