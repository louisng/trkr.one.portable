#pragma once
// Screen manager: routes key events, draws the status bar, the hold-` menu,
// and the active screen into an off-screen canvas.

namespace ui {

void begin();
void tick();  // call every loop after M5Cardputer.update()

}  // namespace ui
