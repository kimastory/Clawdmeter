#pragma once
#include <lvgl.h>

// Two-creature companion system for the info panel: a dog and a cat sit in the
// top-left corner and act out paired "scenes" (playing, fighting, napping…)
// that change frequently. Add both sprites as the topmost children of `screen`
// so they float above every panel screen.
void pets_init(lv_obj_t* screen);
void pets_tick(void);            // advance frame animation + scene timer
void pets_poke(void);            // jump to a fresh random scene now (button)
void pets_set_mood(int group);   // 0..3, biases which scenes are more likely
