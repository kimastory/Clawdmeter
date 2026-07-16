#include "pets.h"
#include "pet_sprite.h"
#include "dog_animations.h"
#include "cat_animations.h"
#include "display_cfg.h"
#include <Arduino.h>
#include <lvgl.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <esp_random.h>

#define GRID     20
#define CELL     2
#define CANVAS_W (GRID * CELL)   // 40
#define CANVAS_H (GRID * CELL)

typedef struct {
    lv_obj_t*                canvas;
    uint16_t*                buf;
    const pet_anim_def_t*    catalog;
    int                      count;
    int                      anim;
    int                      frame;
    uint32_t                 frame_ms;
} Creature;

static Creature dog, cat;

// Paired scenes: {dog animation, cat animation, vibe}. vibe 0=hostile .. 3=friendly.
typedef struct { const char* dog; const char* cat; int vibe; } Scene;
static const Scene SCENES[] = {
    {"idle",  "idle",  2},   // 데면데면
    {"wag",   "happy", 3},   // 사이좋게
    {"happy", "happy", 3},   // 신나게 놀기
    {"bark",  "hiss",  0},   // 싸움
    {"sleep", "sleep", 1},   // 같이 낮잠
    {"wag",   "alert", 2},   // 강아지 장난 / 고양이 경계
    {"blink", "idle",  2},   // 평온
    {"happy", "hiss",  1},   // 강아지 신남 / 고양이 짜증
};
#define SCENE_COUNT ((int)(sizeof(SCENES) / sizeof(SCENES[0])))

static int      cur_scene = -1;
static int      mood = 2;
static uint32_t scene_ms = 0;
static uint32_t scene_dur = 5000;

static uint32_t rnd(uint32_t n) { return n ? (esp_random() % n) : 0; }

static void creature_render(Creature* c) {
    const pet_anim_def_t* a = &c->catalog[c->anim];
    const uint8_t* cells = a->frames[c->frame];
    const uint16_t* pal = a->palette;
    for (int gy = 0; gy < GRID; gy++) {
        uint16_t row[CANVAS_W];
        for (int gx = 0; gx < GRID; gx++) {
            uint8_t code = cells[gy * GRID + gx];
            uint16_t color = (code < SPRITE_PALETTE_SIZE) ? pal[code] : 0x0000;
            for (int i = 0; i < CELL; i++) row[gx * CELL + i] = color;
        }
        for (int dy = 0; dy < CELL; dy++)
            memcpy(&c->buf[(gy * CELL + dy) * CANVAS_W], row, CANVAS_W * 2);
    }
    if (c->canvas) lv_obj_invalidate(c->canvas);
}

static void creature_set(Creature* c, const char* name) {
    for (int i = 0; i < c->count; i++) {
        if (strcmp(c->catalog[i].name, name) == 0) {
            c->anim = i;
            c->frame = 0;
            c->frame_ms = millis();
            creature_render(c);
            return;
        }
    }
}

static void creature_tick(Creature* c) {
    const pet_anim_def_t* a = &c->catalog[c->anim];
    if (a->frame_count == 0) return;
    if (millis() - c->frame_ms >= a->holds[c->frame]) {
        c->frame = (c->frame + 1) % a->frame_count;
        c->frame_ms = millis();
        creature_render(c);
    }
}

// Weighted pick: scenes whose vibe matches the current mood are more likely,
// but everything stays possible so the pair keeps surprising you.
static void pick_scene(void) {
    int weights[SCENE_COUNT], total = 0;
    for (int i = 0; i < SCENE_COUNT; i++) {
        int w = 1 + (3 - abs(SCENES[i].vibe - mood));  // 1..4
        if (i == cur_scene) w = 0;                      // avoid repeating
        weights[i] = w;
        total += w;
    }
    int r = (int)rnd(total), pick = 0;
    for (int i = 0; i < SCENE_COUNT; i++) {
        r -= weights[i];
        if (r < 0) { pick = i; break; }
    }
    cur_scene = pick;
    creature_set(&dog, SCENES[pick].dog);
    creature_set(&cat, SCENES[pick].cat);
    scene_ms = millis();
    scene_dur = 4000 + rnd(4000);   // 4–8s, "수시로"
}

static lv_obj_t* make_canvas(lv_obj_t* parent, uint16_t** buf, int x, int y) {
    *buf = (uint16_t*)heap_caps_malloc(CANVAS_W * CANVAS_H * 2, MALLOC_CAP_SPIRAM);
    lv_obj_t* cv = lv_canvas_create(parent);
    lv_canvas_set_buffer(cv, *buf, CANVAS_W, CANVAS_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(cv, x, y);
    return cv;
}

void pets_init(lv_obj_t* screen) {
    const int y = 6, margin = 6, gap = 2;
    int dog_x = margin;                      // top-left: dog left, cat to its right
    int cat_x = dog_x + CANVAS_W + gap;

    dog.catalog = dog_anims; dog.count = DOG_ANIM_COUNT;
    cat.catalog = cat_anims; cat.count = CAT_ANIM_COUNT;
    dog.canvas = make_canvas(screen, &dog.buf, dog_x, y);
    cat.canvas = make_canvas(screen, &cat.buf, cat_x, y);

    pick_scene();
}

void pets_tick(void) {
    creature_tick(&dog);
    creature_tick(&cat);
    if (millis() - scene_ms >= scene_dur) pick_scene();
}

void pets_poke(void) { pick_scene(); }

void pets_set_mood(int group) {
    if (group < 0) group = 0;
    if (group > 3) group = 3;
    mood = group;
}
