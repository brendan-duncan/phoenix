/* miniaudio's implementation, compiled once. Only playback and decoding are
   used: no capture, encoding or generators. */
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "../third_party/miniaudio/miniaudio.h"
