#pragma once

#include <cstddef>
#include <functional>

namespace fla {

/// Work split across the processor's cores: \a body is called with ranges
/// [from, to) that together cover [0, \a count), each at least \a grain
/// long, on a pool of threads kept for the life of the program, and the
/// call returns once all of them are done. Each range is worked on by one
/// thread, so code that writes only its own range needs no locks, and the
/// result is the same as doing it in one go. Called from inside such a
/// range, it runs in one go on that thread. PHOENIX_THREADS=1 in the
/// environment turns the pool off. Free of Qt.
void parallelFor(size_t count, size_t grain, const std::function<void(size_t from, size_t to)>& body);

/// \a body for each row of an image \a height rows tall and \a width
/// pixels wide, rows shared out as parallelFor does, enough of them to a
/// thread that handing them out costs little next to the work: a small
/// image is done in one go.
void parallelRows(int height, int width, const std::function<void(int y)>& body);

/// Ranges of a run of \a count pixels, shared out the same way.
void parallelPixels(size_t count, const std::function<void(size_t from, size_t to)>& body);

/// How many threads parallelFor uses, the caller's own included.
int parallelThreads();

} // namespace fla
