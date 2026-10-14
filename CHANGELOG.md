# Changelog

What changed in each release of Phoenix, newest first.

## 0.7.0 (Unreleased)

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

## 0.6.0 (2026-10-13)

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

## 0.5.2 (2026-10-13)

- Interactive tutorials: nine guided lessons in Help > Tutorials, from your first animation to rigging
- Install page links straight to the latest installers

## 0.5.1 (2026-10-13)

- Movie clip sounds play while the document plays, with View > Mute Movie Clip Sounds
- XFL folders open from their .xfl file
- Photoshop large documents (.psb) import
- Settings move to a new location; your old settings come with them
- Updated user guide and screenshots, showing the version they describe

## 0.5.0 (2026-10-12)

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

## 0.4.0 (2026-10-12)

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

## 0.3.0 (2026-10-11)

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

## 0.2.0 (2026-10-11)

- Signed and notarized macOS installer

## 0.1.0 (2026-10-10)

- First release: open, edit, play and save FLA files with drawing tools, a timeline, symbols and a library
