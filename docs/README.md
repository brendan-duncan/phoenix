# Phoenix technical documentation

Notes for people working on Phoenix's source. The user guide and website are in
[website/](../website/) and published at [phoenixanimate.com](https://phoenixanimate.com).

## Start here

- [building.md](building.md): building Phoenix, running the tests, command-line checks
- [architecture.md](architecture.md): how the source is laid out, the document model, editing and undo, the GUI

## File formats

- [fla-format.md](fla-format.md): FLA and XFL, how Phoenix reads and writes them, the old binary FLA
- [phx-format.md](phx-format.md): Phoenix's own .phx format; the schema is [phoenix_format.proto](phoenix_format.proto)
- [swf.md](swf.md): SWF export and import

## Importing

- [importers.md](importers.md): Photoshop documents, vector artwork, images, sound, video, scans and extension packages

## Scripting and automation

- [jsfl.md](jsfl.md): the JSFL engine, its object model, tools, dialogs and extensions
- [python-and-mcp.md](python-and-mcp.md): Python scripts, Phoenix's operations, and the MCP server for AI assistants
- [actionscript.md](actionscript.md): how Test Movie, the player and HTML5 pages run ActionScript

## Drawing

- [geometry.md](geometry.md): shapes, curves, how fills are built, merge drawing, shape tweens and rendering

## Writing these notes

Each note describes the code as it is, with links to the files that hold it. When a change makes a
note wrong, fix the note in the same commit. Name functions and classes rather than line numbers,
which drift.
