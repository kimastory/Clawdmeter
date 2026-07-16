#include "splash.h"
#include "splash_animations.h"
#include "theme.h"
#include "usage_rate.h"
#include "display_cfg.h"
#include <Arduino.h>
#include <string.h>
#include <esp_heap_caps.h>

// 20x20 grid. The mascot is intentionally small here so the splash screen can
// double as a compact calendar view.
#define GRID         20
#ifdef M5STACK_CORE2
#define CELL         5
#define MASCOT_X     12
#define MASCOT_Y     42
#define CLOCK_Y      150
#define CLOCK_W      112
#define CAL_X        124
#define CAL_Y        40
#define CAL_W        184
#define CAL_CELL_H   19
#define CAL_HEAD_H   24
#define CAL_DOW_Y    30
#define CAL_GRID_Y   50
#define CAL_FONT_DAY font_styrene_12
#define CAL_FONT_HDR font_styrene_16
#else
#define CELL         8
#define MASCOT_X     20
#define MASCOT_Y     100
#define CLOCK_Y      270
#define CLOCK_W      160
#define CAL_X        200
#define CAL_Y        92
#define CAL_W        260
#define CAL_CELL_H   31
#define CAL_HEAD_H   42
#define CAL_DOW_Y    54
#define CAL_GRID_Y   86
#define CAL_FONT_DAY font_styrene_20
#define CAL_FONT_HDR font_styrene_28
#endif
// Info panel: the mascot is a small always-on companion in the top-left
// corner (the big clock + calendar are drawn separately by panel.cpp).
#ifdef CLAWDMETER_INFO_PANEL
#undef CELL
#undef MASCOT_X
#undef MASCOT_Y
#define CELL         2
#define MASCOT_X     6
#define MASCOT_Y     6
#endif

#define CANVAS_W     (GRID * CELL)
#define CANVAS_H     (GRID * CELL)
#define CAL_COLS     7
#define CAL_ROWS     6
#define CAL_CELL_W   (CAL_W / CAL_COLS)

// Background fallback when palette is missing
#define COL_EMPTY    0x0000  // true black (matches THEME_BG)

LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_12);

static lv_obj_t *splash_container = NULL;
static lv_obj_t *canvas = NULL;
static lv_obj_t *clock_label = NULL;
static lv_obj_t *label_status = NULL;     // shown only when no animations loaded
static lv_obj_t *calendar_root = NULL;
static lv_obj_t *calendar_title = NULL;
static lv_obj_t *calendar_cells[CAL_ROWS * CAL_COLS] = {NULL};
static lv_obj_t *calendar_labels[CAL_ROWS * CAL_COLS] = {NULL};
static uint16_t *canvas_buf = NULL;        // 480x480 RGB565 (PSRAM)

static uint16_t cur_anim = 0;
static uint16_t cur_frame = 0;
static uint32_t frame_started_ms = 0;
static uint32_t last_pick_ms = 0;
static bool active = false;
static int group_override = -1;  // -1 = use usage_rate_group(); else forced mood
static int shown_year = 0;
static int shown_month = 0;
static int shown_day = 0;
static int clock_hour = 0;
static int clock_minute = 0;
static int clock_second = 0;
static uint32_t clock_base_ms = 0;
static int last_rendered_second = -1;

// While splash is showing, auto-cycle to the next animation in the current
// rate-driven group every this many ms.
#define SPLASH_ROTATE_INTERVAL_MS 20000

// Usage-rate animation groups: 4 groups × up to 4 animations each.
// Filled at init by matching literal names from splash_anims[].
#define GROUP_COUNT 4
#define GROUP_MAX   4
static int8_t  group_lists[GROUP_COUNT][GROUP_MAX];
static uint8_t group_size[GROUP_COUNT] = {0};
static uint8_t group_rotation[GROUP_COUNT] = {0};

#ifdef CLAWDMETER_INFO_PANEL
// Info panel: groups map to the puppy's MOOD, driven by weather + air quality.
// 0 = sleepy/down (bad air or storm) … 3 = happy/playful (clean + clear).
static const char* GROUP_NAMES[GROUP_COUNT][GROUP_MAX] = {
    { "sleep", "idle", NULL, NULL },     // Group 0 — sleepy / down
    { "idle", "blink", NULL, NULL },     // Group 1 — calm
    { "wag", "idle", NULL, NULL },       // Group 2 — content
    { "happy", "wag", NULL, NULL },      // Group 3 — happy / playful
};
#else
static const char* GROUP_NAMES[GROUP_COUNT][GROUP_MAX] = {
    // Group 0 — idle / sleepy
    { "expression sleep", "idle breathe", "idle blink", "expression wink" },
    // Group 1 — normal pace
    { "idle look around", "work think", "work coding", NULL },
    // Group 2 — active
    { "dance sway", "expression surprise", "dance bounce", NULL },
    // Group 3 — heavy
    { "dance bounce dj", "dance sway dj", "dance djmix", NULL },
};
#endif

static void resolve_group_lists(void) {
    for (int g = 0; g < GROUP_COUNT; g++) {
        group_size[g] = 0;
        for (int s = 0; s < GROUP_MAX; s++) {
            group_lists[g][s] = -1;
            const char* want = GROUP_NAMES[g][s];
            if (!want) continue;
            for (int i = 0; i < SPLASH_ANIM_COUNT; i++) {
                if (strcmp(splash_anims[i].name, want) == 0) {
                    group_lists[g][group_size[g]++] = (int8_t)i;
                    break;
                }
            }
        }
    }
}

static void render_frame(const uint8_t *cells, const uint16_t *palette) {
    for (int gy = 0; gy < GRID; gy++) {
        uint16_t row[CANVAS_W];
        for (int gx = 0; gx < GRID; gx++) {
            uint8_t code = cells[gy * GRID + gx];
            uint16_t color = (palette && code < SPLASH_PALETTE_SIZE) ? palette[code] : COL_EMPTY;
            uint16_t *p = &row[gx * CELL];
            for (int i = 0; i < CELL; i++) p[i] = color;
        }
        for (int dy = 0; dy < CELL; dy++) {
            memcpy(&canvas_buf[(gy * CELL + dy) * CANVAS_W], row, CANVAS_W * 2);
        }
    }
    if (canvas) lv_obj_invalidate(canvas);
}

static void show_placeholder() {
    // Solid dark background + centered status label.
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) canvas_buf[i] = COL_EMPTY;
    if (canvas) lv_obj_invalidate(canvas);
    if (label_status) lv_obj_clear_flag(label_status, LV_OBJ_FLAG_HIDDEN);
}

static bool is_leap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int days_in_month(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap(year)) return 29;
    if (month < 1 || month > 12) return 0;
    return days[month - 1];
}

// Sakamoto's algorithm. Returns 0=Sunday, 1=Monday, ...
static int weekday(int year, int month, int day) {
    static const int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) year--;
    return (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + day) % 7;
}

static void calendar_clear_cell(int idx) {
    lv_label_set_text(calendar_labels[idx], "");
    lv_obj_set_style_bg_opa(calendar_cells[idx], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(calendar_cells[idx], 0, 0);
}

static void calendar_set_cell(int idx, int day, bool today) {
    lv_label_set_text_fmt(calendar_labels[idx], "%d", day);
    lv_obj_set_style_text_color(calendar_labels[idx], today ? THEME_BG : THEME_TEXT, 0);
    lv_obj_set_style_bg_color(calendar_cells[idx], today ? THEME_ACCENT : THEME_PANEL, 0);
    lv_obj_set_style_bg_opa(calendar_cells[idx], today ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(calendar_cells[idx], today ? 6 : 0, 0);
    lv_obj_set_style_border_width(calendar_cells[idx], today ? 0 : 0, 0);
}

static void calendar_render(int year, int month, int day) {
    if (!calendar_root || year <= 0 || month <= 0 || day <= 0) return;

    static const char* const months[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    lv_label_set_text_fmt(calendar_title, "%s %d", months[month - 1], year);

    for (int i = 0; i < CAL_ROWS * CAL_COLS; i++) calendar_clear_cell(i);

    int first = weekday(year, month, 1);
    int count = days_in_month(year, month);
    for (int d = 1; d <= count; d++) {
        int idx = first + d - 1;
        if (idx >= 0 && idx < CAL_ROWS * CAL_COLS) {
            calendar_set_cell(idx, d, d == day);
        }
    }
}

static void init_calendar(lv_obj_t *parent) {
    calendar_root = lv_obj_create(parent);
    lv_obj_set_pos(calendar_root, CAL_X, CAL_Y);
    lv_obj_set_size(calendar_root, CAL_W, LCD_HEIGHT - CAL_Y - 8);
    lv_obj_set_style_bg_opa(calendar_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(calendar_root, 0, 0);
    lv_obj_set_style_pad_all(calendar_root, 0, 0);
    lv_obj_clear_flag(calendar_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(calendar_root, LV_OBJ_FLAG_EVENT_BUBBLE);

    calendar_title = lv_label_create(calendar_root);
    lv_label_set_text(calendar_title, "Calendar");
    lv_obj_set_style_text_font(calendar_title, &CAL_FONT_HDR, 0);
    lv_obj_set_style_text_color(calendar_title, THEME_TEXT, 0);
    lv_obj_set_size(calendar_title, CAL_W, CAL_HEAD_H);
    lv_obj_set_style_text_align(calendar_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(calendar_title, 0, 0);

    static const char* const dows[] = {"S", "M", "T", "W", "T", "F", "S"};
    for (int c = 0; c < CAL_COLS; c++) {
        lv_obj_t* lbl = lv_label_create(calendar_root);
        lv_label_set_text(lbl, dows[c]);
        lv_obj_set_style_text_font(lbl, &CAL_FONT_DAY, 0);
        lv_obj_set_style_text_color(lbl, THEME_DIM, 0);
        lv_obj_set_size(lbl, CAL_CELL_W, CAL_CELL_H);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(lbl, c * CAL_CELL_W, CAL_DOW_Y);
    }

    for (int r = 0; r < CAL_ROWS; r++) {
        for (int c = 0; c < CAL_COLS; c++) {
            int idx = r * CAL_COLS + c;
            calendar_cells[idx] = lv_obj_create(calendar_root);
            lv_obj_set_pos(calendar_cells[idx], c * CAL_CELL_W, CAL_GRID_Y + r * CAL_CELL_H);
            lv_obj_set_size(calendar_cells[idx], CAL_CELL_W - 2, CAL_CELL_H - 2);
            lv_obj_set_style_bg_opa(calendar_cells[idx], LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(calendar_cells[idx], 0, 0);
            lv_obj_set_style_pad_all(calendar_cells[idx], 0, 0);
            lv_obj_clear_flag(calendar_cells[idx], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(calendar_cells[idx], LV_OBJ_FLAG_EVENT_BUBBLE);

            calendar_labels[idx] = lv_label_create(calendar_cells[idx]);
            lv_obj_set_style_text_font(calendar_labels[idx], &CAL_FONT_DAY, 0);
            lv_obj_set_style_text_color(calendar_labels[idx], THEME_TEXT, 0);
            lv_obj_center(calendar_labels[idx]);
        }
    }
}

static void render_clock(void) {
    if (!clock_label || clock_base_ms == 0) return;

    uint32_t elapsed = (millis() - clock_base_ms) / 1000;
    int total = clock_hour * 3600 + clock_minute * 60 + clock_second + (int)elapsed;
    total %= 24 * 3600;

    int hour24 = total / 3600;
    int minute = (total / 60) % 60;
    int second = total % 60;
    if (second == last_rendered_second) return;
    last_rendered_second = second;

    int hour12 = hour24 % 12;
    if (hour12 == 0) hour12 = 12;
    lv_label_set_text_fmt(clock_label, "%s %d:%02d:%02d",
                          hour24 < 12 ? "AM" : "PM", hour12, minute, second);
}

void splash_init(lv_obj_t *parent) {
    canvas_buf = (uint16_t*)heap_caps_malloc(CANVAS_W * CANVAS_H * 2, MALLOC_CAP_SPIRAM);
    if (!canvas_buf) {
        Serial.println("splash: failed to alloc canvas buffer");
        return;
    }

#ifdef CLAWDMETER_INFO_PANEL
    // Info panel: the mascot is a small always-on companion. It is added to the
    // screen LAST (after panel.cpp's screen containers), so as the topmost
    // sibling it draws above every screen and is captured by screenshots.
    // Clock + calendar are owned by panel.cpp.
    canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, canvas_buf, CANVAS_W, CANVAS_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas, MASCOT_X, MASCOT_Y);
    resolve_group_lists();
    if (SPLASH_ANIM_COUNT > 0) {
        const splash_anim_def_t *a = &splash_anims[0];
        render_frame(a->frames[0], a->palette);
        frame_started_ms = millis();
    }
    active = true;   // there is no separate splash screen; always animate
    return;
#endif

    splash_container = lv_obj_create(parent);
    lv_obj_set_size(splash_container, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_pos(splash_container, 0, 0);
    lv_obj_set_style_bg_color(splash_container, THEME_BG, 0);
    lv_obj_set_style_bg_opa(splash_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(splash_container, 0, 0);
    lv_obj_set_style_pad_all(splash_container, 0, 0);
    lv_obj_clear_flag(splash_container, LV_OBJ_FLAG_SCROLLABLE);

    canvas = lv_canvas_create(splash_container);
    lv_canvas_set_buffer(canvas, canvas_buf, CANVAS_W, CANVAS_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas, MASCOT_X, MASCOT_Y);
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_EVENT_BUBBLE);

    clock_label = lv_label_create(splash_container);
    lv_label_set_text(clock_label, "AM --:--:--");
    lv_obj_set_style_text_font(clock_label, &CAL_FONT_HDR, 0);
    lv_obj_set_style_text_color(clock_label, THEME_TEXT, 0);
    lv_obj_set_size(clock_label, CLOCK_W, 24);
    lv_obj_set_style_text_align(clock_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(clock_label, MASCOT_X - 6, CLOCK_Y);

    init_calendar(splash_container);

    // Placeholder label (visible only when no animations are loaded)
    label_status = lv_label_create(splash_container);
    lv_label_set_text(label_status,
        "no animations loaded\n\n"
        "run tools/scrape_claudepix.js\n"
        "then tools/convert_to_c.js");
    lv_obj_set_style_text_font(label_status, &font_styrene_28, 0);
    lv_obj_set_style_text_color(label_status, lv_color_hex(0xb0aea5), 0);
    lv_obj_set_style_text_align(label_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label_status);

    resolve_group_lists();

    if (SPLASH_ANIM_COUNT == 0) {
        show_placeholder();
    } else {
        lv_obj_add_flag(label_status, LV_OBJ_FLAG_HIDDEN);
        const splash_anim_def_t *a = &splash_anims[0];
        render_frame(a->frames[0], a->palette);
        frame_started_ms = millis();
    }

    lv_obj_add_flag(splash_container, LV_OBJ_FLAG_HIDDEN);
}

void splash_tick(void) {
    if (!active) return;
    render_clock();
    if (SPLASH_ANIM_COUNT == 0) return;

    // Auto-rotate to the next animation in the current group.
    if (millis() - last_pick_ms >= SPLASH_ROTATE_INTERVAL_MS) {
        splash_pick_for_current_rate();
    }

    const splash_anim_def_t *a = &splash_anims[cur_anim];
    if (a->frame_count == 0) return;

    uint16_t hold = a->holds[cur_frame];
    if (millis() - frame_started_ms >= hold) {
        cur_frame = (cur_frame + 1) % a->frame_count;
        frame_started_ms = millis();
        render_frame(a->frames[cur_frame], a->palette);
    }
}

void splash_next(void) {
    if (SPLASH_ANIM_COUNT == 0) return;
    cur_anim = (cur_anim + 1) % SPLASH_ANIM_COUNT;
    cur_frame = 0;
    frame_started_ms = millis();
    last_pick_ms = frame_started_ms;
    const splash_anim_def_t *a = &splash_anims[cur_anim];
    render_frame(a->frames[0], a->palette);
    Serial.printf("splash: -> %s\n", a->name);
}

void splash_set_group(int group) {
    if (group < 0) { group_override = -1; return; }
    if (group >= GROUP_COUNT) group = GROUP_COUNT - 1;
    group_override = group;
    splash_pick_for_current_rate();  // reflect the new mood immediately
}

void splash_pick_for_current_rate(void) {
    if (SPLASH_ANIM_COUNT == 0) return;
    int g = (group_override >= 0) ? group_override : usage_rate_group();
    if (g < 0 || g >= GROUP_COUNT) g = 0;
    if (group_size[g] == 0) return;

    uint8_t slot = group_rotation[g] % group_size[g];
    group_rotation[g]++;
    int8_t idx = group_lists[g][slot];
    if (idx < 0) return;

    cur_anim = (uint16_t)idx;
    cur_frame = 0;
    frame_started_ms = millis();
    last_pick_ms = frame_started_ms;
    const splash_anim_def_t *a = &splash_anims[cur_anim];
    render_frame(a->frames[0], a->palette);
}

void splash_update_calendar(const UsageData* data) {
    if (!data || data->year <= 0 || data->month <= 0 || data->day <= 0) return;
    if (data->hour >= 0 && data->hour < 24 &&
        data->minute >= 0 && data->minute < 60 &&
        data->second >= 0 && data->second < 60) {
        clock_hour = data->hour;
        clock_minute = data->minute;
        clock_second = data->second;
        clock_base_ms = millis();
        last_rendered_second = -1;
        render_clock();
    }
    if (data->year != shown_year || data->month != shown_month || data->day != shown_day) {
        shown_year = data->year;
        shown_month = data->month;
        shown_day = data->day;
        calendar_render(shown_year, shown_month, shown_day);
    }
}

void splash_set_datetime(int year, int month, int day,
                         int hour, int minute, int second) {
    if (year <= 0 || month <= 0 || day <= 0) return;
    if (hour >= 0 && hour < 24 && minute >= 0 && minute < 60 &&
        second >= 0 && second < 60) {
        clock_hour = hour;
        clock_minute = minute;
        clock_second = second;
        clock_base_ms = millis();
        last_rendered_second = -1;
        render_clock();
    }
    if (year != shown_year || month != shown_month || day != shown_day) {
        shown_year = year;
        shown_month = month;
        shown_day = day;
        calendar_render(shown_year, shown_month, shown_day);
    }
}

bool splash_is_active(void) { return active; }

void splash_show(void) {
    splash_pick_for_current_rate();
    if (splash_container) lv_obj_clear_flag(splash_container, LV_OBJ_FLAG_HIDDEN);
    active = true;
}

void splash_hide(void) {
    if (splash_container) lv_obj_add_flag(splash_container, LV_OBJ_FLAG_HIDDEN);
    active = false;
}

lv_obj_t* splash_get_root(void) {
    return splash_container;
}
