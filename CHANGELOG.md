# Changelog

What changed in each release of Phoenix, newest first.

## 0.10.0 (Unreleased)

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
