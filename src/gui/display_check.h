#pragma once

#include <QStringList>

/// Phoenix display-check <out folder> [--fla <file>]: draws display lists
/// built in code (shapes, gradients, bitmaps, blend modes, filters, masks,
/// text, a paint program's canvas) with the display painter Test Movie
/// uses, and writes them as PNGs to look at. With --fla, the movie is
/// played a few frames and drawn as Test Movie draws it, with display
/// objects added among its timeline's. Prints how long the pixel loops a
/// brush stroke runs take. Exits 1 when an image could not be written.
int displayCheck(const QStringList& args);
