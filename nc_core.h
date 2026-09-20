/* Night Client core: config, touch capture and FPS meter. Pure C, no game/ImGui deps. */
#ifndef NC_CORE_H
#define NC_CORE_H
#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- config ---------------- */
typedef struct {
    int   autosprint;
    int   fps_on;
    float fps_x, fps_y;     /* 0..1 position on screen */
    float fps_scale;        /* text size multiplier */
    int   fps_bg;           /* dark background behind the text */
    float ui_scale;         /* menu size multiplier */
    float n_x, n_y;         /* N button position, 0..1 */
    int   n_always;         /* debug: show the N button outside of Settings */
} NcConfig;

void nc_cfg_defaults(NcConfig *c);
int  nc_cfg_load(NcConfig *c, const char *path);       /* returns 1 if the file existed */
int  nc_cfg_save(const NcConfig *c, const char *path); /* returns 1 on success */
int  nc_cfg_equal(const NcConfig *a, const NcConfig *b);

/* ---------------- touch capture ---------------- */
enum { NC_EV_DOWN = 1, NC_EV_MOVE = 2, NC_EV_UP = 3 };

typedef struct { int visible; float x, y, w, h; } NcRect;
typedef struct { NcRect n, win; int captured; } NcTouch;

typedef void (*NcPush)(void *user, int type, float x, float y);

/* Android motion actions: 0 down, 1 up, 2 move, 3 cancel, 5 pointer down, 6 pointer up.
 * Returns 1 if the event was taken by Night Client (do not pass it to the game). */
int nc_touch_event(NcTouch *t, int action, float x, float y, NcPush push, void *user);

/* ---------------- fps ---------------- */
typedef struct { double window_start; int frames; float fps; } NcFps;
/* call once per frame with the current time in seconds; returns the current fps value */
float nc_fps_frame(NcFps *f, double now);

#ifdef __cplusplus
}
#endif
#endif
