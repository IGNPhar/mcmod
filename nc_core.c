#include "nc_core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ---------------- config ---------------- */
void nc_cfg_defaults(NcConfig *c) {
    memset(c, 0, sizeof *c);
    c->autosprint = 1;
    c->fps_on = 0;
    c->fps_x = 0.02f; c->fps_y = 0.02f;
    c->fps_scale = 1.0f;
    c->fps_bg = 1;
    c->ui_scale = 1.0f;
    c->n_x = 0.92f; c->n_y = 0.80f;
    c->n_always = 0;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

int nc_cfg_load(NcConfig *c, const char *path) {
    FILE *f = fopen(path, "r");
    char line[160];
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
        if      (!strcmp(k, "autosprint")) c->autosprint = atoi(v) != 0;
        else if (!strcmp(k, "fps_on"))     c->fps_on = atoi(v) != 0;
        else if (!strcmp(k, "fps_x"))      c->fps_x = clampf((float)atof(v), 0.f, 1.f);
        else if (!strcmp(k, "fps_y"))      c->fps_y = clampf((float)atof(v), 0.f, 1.f);
        else if (!strcmp(k, "fps_scale"))  c->fps_scale = clampf((float)atof(v), 0.5f, 3.f);
        else if (!strcmp(k, "fps_bg"))     c->fps_bg = atoi(v) != 0;
        else if (!strcmp(k, "ui_scale"))   c->ui_scale = clampf((float)atof(v), 0.6f, 1.6f);
        else if (!strcmp(k, "n_x"))        c->n_x = clampf((float)atof(v), 0.f, 1.f);
        else if (!strcmp(k, "n_y"))        c->n_y = clampf((float)atof(v), 0.f, 1.f);
        else if (!strcmp(k, "n_always"))   c->n_always = atoi(v) != 0;
    }
    fclose(f);
    return 1;
}

int nc_cfg_save(const NcConfig *c, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "# Night Client settings (edited automatically by the in-game menu)\n");
    fprintf(f, "autosprint=%d\n", c->autosprint);
    fprintf(f, "fps_on=%d\n", c->fps_on);
    fprintf(f, "fps_x=%.4f\nfps_y=%.4f\nfps_scale=%.3f\nfps_bg=%d\n", c->fps_x, c->fps_y, c->fps_scale, c->fps_bg);
    fprintf(f, "ui_scale=%.3f\n", c->ui_scale);
    fprintf(f, "n_x=%.4f\nn_y=%.4f\nn_always=%d\n", c->n_x, c->n_y, c->n_always);
    fclose(f);
    return 1;
}

int nc_cfg_equal(const NcConfig *a, const NcConfig *b) { return memcmp(a, b, sizeof *a) == 0; }

/* ---------------- touch capture ---------------- */
static int hit(const NcRect *r, float x, float y) {
    return r->visible && x >= r->x && x <= r->x + r->w && y >= r->y && y <= r->y + r->h;
}

int nc_touch_event(NcTouch *t, int action, float x, float y, NcPush push, void *user) {
    switch (action) {
    case 0: /* DOWN: decide once whether this finger belongs to us */
        t->captured = t->win.visible ? 1 : hit(&t->n, x, y);   /* menu open = modal */
        if (t->captured) { push(user, NC_EV_DOWN, x, y); return 1; }
        return 0;
    case 2: /* MOVE */
        if (t->captured) { push(user, NC_EV_MOVE, x, y); return 1; }
        return 0;
    case 1: case 3: /* UP / CANCEL */
        if (t->captured) { push(user, NC_EV_UP, x, y); t->captured = 0; return 1; }
        return 0;
    case 5: case 6: /* extra fingers: swallow only while our finger is active */
        return t->captured;
    default:
        return t->captured;
    }
}

/* ---------------- fps ---------------- */
float nc_fps_frame(NcFps *f, double now) {
    if (f->window_start == 0.0) { f->window_start = now; f->frames = 0; }
    f->frames++;
    double dt = now - f->window_start;
    if (dt >= 0.5) {
        f->fps = (float)(f->frames / dt);
        f->frames = 0;
        f->window_start = now;
    }
    return f->fps;
}
