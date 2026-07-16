#pragma once
#include <stdint.h>

// Shared 20x20 pixel-art sprite format for the info-panel pets (dog + cat).
// Each animation carries a 10-entry RGB565 palette; cell values 0..9 index it.
#define SPRITE_PALETTE_SIZE 10

typedef struct {
    const char *name;
    uint16_t frame_count;
    const uint16_t *palette;
    const uint8_t (*frames)[400];
    const uint16_t *holds;
} pet_anim_def_t;
