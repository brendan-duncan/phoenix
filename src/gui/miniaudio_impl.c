/* miniaudio's implementation, compiled once. Only playback and decoding are
   used: no capture, encoding or generators. */
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
/* Its own code, not ours: it ignores read()'s result in places. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-result"
#endif
#include "../third_party/miniaudio/miniaudio.h"
