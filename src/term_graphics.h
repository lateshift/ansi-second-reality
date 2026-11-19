#pragma once

#include "encoder.h"

class Graphics;

namespace sr {

extern Graphics graphics;

#ifdef __EMSCRIPTEN__
struct PlaybackStopped {};
#endif

void configureGraphics(Scale scale, bool mute, bool stats);
void startGraphicsPart(unsigned index);

} // namespace sr
