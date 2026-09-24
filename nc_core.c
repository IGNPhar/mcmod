#include "nc_core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---------------- config ---------------- */
typedef struct { const char *key; int is_float; size_t off; float mn, mx, def; } Field;
#define NC_ISF_I 0
#define NC_ISF_F 1
#define NC_ROW(n, t, d, mn, mx) { #n, NC_ISF_##t, offsetof(NcConfig, n), (float)(mn), (float)(mx), (float)(d) },
static const Field FIELDS[] = { NC_FIELDS(NC_ROW) };
#define NC_NFIELDS ((int)(sizeof(FIELDS) / sizeof(FIELDS[0])))

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void set_field(NcConfig *c, const Field *f, float v) {
    v = clampf(v, f->mn, f->mx);
    if (f->is_float) *(float *)((char *)c + f->off) = v;
    else             *(int   *)((char *)c + f->off) = (int)(v + (v < 0 ? -0.5f : 0.5f));
}

void nc_cfg_defaults(NcConfig *c) {
    memset(c, 0, sizeof *c);
    for (int i = 0; i < NC_NFIELDS; i++) set_field(c, &FIELDS[i], FIELDS[i].def);
    strcpy(c->zoom_text, "Z");
    strcpy(c->persp_text, "F5");
    strcpy(c->drop_text, "Q");
    strcpy(c->n_text, "N");
}

int nc_cfg_load(NcConfig *c, const char *path) {
    FILE *f = fopen(path, "r");
    char line[160];
    if (!f) return 0;

    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = 0;

        for (int i = 0; i < NC_NFIELDS; i++) {
            if (!strcmp(line, FIELDS[i].key)) {
                if (FIELDS[i].is_float)
                    set_field(c, &FIELDS[i], (float)strtod(eq + 1, NULL));
                else if (strstr(FIELDS[i].key, "_col") || strstr(FIELDS[i].key, "_bg_col"))
                    set_field(c, &FIELDS[i], (float)strtoul(eq + 1, NULL, 0));
                else
                    set_field(c, &FIELDS[i], (float)strtol(eq + 1, NULL, 0));
                goto loaded_line;
            }
        }

        if (!strcmp(line, "zoom_text")) {
            char *v = eq + 1;
            size_t n = strcspn(v, "\r\n");
            v[n] = 0;
            /* Never overwrite a valid default/custom label with an empty line.
             * This repairs configs written by the broken keyboard build. */
            if (n > 0) {
                strncpy(c->zoom_text, v, sizeof(c->zoom_text) - 1);
                c->zoom_text[sizeof(c->zoom_text) - 1] = 0;
            }
            goto loaded_line;
        }
        if (!strcmp(line, "persp_text")) {
            char *v = eq + 1;
            size_t n = strcspn(v, "\r\n");
            v[n] = 0;
            /* Never overwrite a valid default/custom label with an empty line.
             * This repairs configs written by the broken keyboard build. */
            if (n > 0) {
                strncpy(c->persp_text, v, sizeof(c->persp_text) - 1);
                c->persp_text[sizeof(c->persp_text) - 1] = 0;
            }
            goto loaded_line;
        }
        if (!strcmp(line, "drop_text")) {
            char *v = eq + 1;
            size_t n = strcspn(v, "\r\n");
            v[n] = 0;
            /* Never overwrite a valid default/custom label with an empty line.
             * This repairs configs written by the broken keyboard build. */
            if (n > 0) {
                strncpy(c->drop_text, v, sizeof(c->drop_text) - 1);
                c->drop_text[sizeof(c->drop_text) - 1] = 0;
            }
            goto loaded_line;
        }
        if (!strcmp(line, "n_text")) {
            char *v = eq + 1;
            size_t n = strcspn(v, "\r\n");
            v[n] = 0;
            /* Never overwrite a valid default/custom label with an empty line.
             * This repairs configs written by the broken keyboard build. */
            if (n > 0) {
                strncpy(c->n_text, v, sizeof(c->n_text) - 1);
                c->n_text[sizeof(c->n_text) - 1] = 0;
            }
            goto loaded_line;
        }

loaded_line:
        ;
    }

    fclose(f);
    if (!c->zoom_text[0]) strcpy(c->zoom_text, "Z");
    if (!c->persp_text[0]) strcpy(c->persp_text, "F5");
    if (!c->drop_text[0]) strcpy(c->drop_text, "Q");
    if (!c->n_text[0]) strcpy(c->n_text, "N");
    return 1;
}

int nc_cfg_save(const NcConfig *c, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return 0;

    fprintf(f, "# Night Client settings (edited automatically by the in-game menu)\n");
    for (int i = 0; i < NC_NFIELDS; i++) {
        if (FIELDS[i].is_float)
            fprintf(f, "%s=%.4f\n", FIELDS[i].key, *(const float *)((const char *)c + FIELDS[i].off));
        else {
            int iv = *(const int *)((const char *)c + FIELDS[i].off);
            if (strstr(FIELDS[i].key, "_col") || strstr(FIELDS[i].key, "_bg_col"))
                fprintf(f, "%s=0x%06X\n", FIELDS[i].key, iv & 0xFFFFFF);
            else
                fprintf(f, "%s=%d\n", FIELDS[i].key, iv);
        }
    }

    fprintf(f, "zoom_text=%s\n", c->zoom_text);
    fprintf(f, "persp_text=%s\n", c->persp_text);
    fprintf(f, "drop_text=%s\n", c->drop_text);
    fprintf(f, "n_text=%s\n", c->n_text);

    fclose(f);
    return 1;
}

int nc_cfg_equal(const NcConfig *a, const NcConfig *b) {
    return memcmp(a, b, sizeof *a) == 0;
}

/* ---------------- touch capture ---------------- */
static int hit(const NcRect *r, float x, float y) {
    return r->visible && x >= r->x && x <= r->x + r->w && y >= r->y && y <= r->y + r->h;
}

void nc_touch_init(NcTouch *t) {
    memset(t, 0, sizeof *t);
    t->cap_id = -1;
}

static int hit_any(const NcTouch *t, float x, float y) {
    if (hit(&t->n, x, y)) return 1;
    for (int i = 0; i < NC_MAX_HUD; i++)
        if (hit(&t->hud[i], x, y)) return 1;
    return 0;
}

int nc_touch_event(NcTouch *t, const NcMotion *m, NcPush push, void *user) {
    int modal = t->win.visible;
    int i;

    switch (m->action) {
    case 0:
    case 5:
        i = m->idx;
        if (i < 0 || i >= m->count) return modal;
        if (t->cap_id >= 0) return modal;
        if (modal || hit_any(t, m->x[i], m->y[i])) {
            t->cap_id = m->id[i];
            push(user, NC_EV_DOWN, m->x[i], m->y[i]);
            return 1;
        }
        return 0;

    case 2:
        if (t->cap_id < 0) return modal;
        for (i = 0; i < m->count; i++) {
            if (m->id[i] == t->cap_id) {
                push(user, NC_EV_MOVE, m->x[i], m->y[i]);
                return (modal || m->count == 1) ? 1 : 0;
            }
        }
        return modal;

    case 1:
    case 6:
        i = m->idx;
        if (t->cap_id >= 0 && i >= 0 && i < m->count && m->id[i] == t->cap_id) {
            push(user, NC_EV_UP, m->x[i], m->y[i]);
            t->cap_id = -1;
            return 1;
        }
        return modal;

    case 3:
        if (t->cap_id >= 0) {
            push(user, NC_EV_UP, 0, 0);
            t->cap_id = -1;
        }
        return modal;

    default:
        return modal;
    }
}

/* ---------------- fps ---------------- */
float nc_fps_frame(NcFps *f, double now) {
    if (f->window_start == 0.0) {
        f->window_start = now;
        f->frames = 0;
    }

    f->frames++;
    double dt = now - f->window_start;

    if (dt >= 0.5) {
        f->fps = (float)(f->frames / dt);
        f->frames = 0;
        f->window_start = now;
    }

    return f->fps;
}
