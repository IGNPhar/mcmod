/*
 * Night Client 0.1 - Toolbox mod for Minecraft PE 1.1.5 (32-bit ARM)
 *
 *  - Autosprint
 *  - FPS counter (movable, resizable)
 *  - Dark in-game menu, opened with the "N" button. The N button is only drawn while a
 *    Settings screen is open (from the main menu or from the pause menu).
 *  - Everything is saved to games/com.mojang/NightClient/config.txt
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <float.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>

#include <EGL/egl.h>
#include <android/input.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

extern "C" {
#include "nc_core.h"
#include "gothook.h"
}

#define NC_DIR "/sdcard/games/com.mojang/NightClient/"
#define NC_CFG NC_DIR "config.txt"
#define NC_LOG NC_DIR "log.txt"

/* ---- game functions (found in libminecraftpe.so when the mod loads) ---- */
extern "C" bool mih_isMovingForward(void *self) __asm__("_ZNK16MoveInputHandler15isMovingForwardEv");
extern "C" bool mob_isSneaking(void *self)      __asm__("_ZNK3Mob10isSneakingEv");
extern "C" bool mob_isSprinting(void *self)     __asm__("_ZNK3Mob11isSprintingEv");
extern "C" bool player_isUsingItem(void *self)  __asm__("_ZNK6Player11isUsingItemEv");
extern "C" void lp_setSprinting(void *self, bool on) __asm__("_ZN11LocalPlayer12setSprintingEb");

/* ---- Toolbox mod loader: hook registration (libmodloader.so) ---- */
extern "C" void tml_registerHook(const char *symbol, void *hook, void **original)
    __asm__("_ZN3tml17StaticHookManager12registerHookEPKcPvPS3_");

/* ------------------------------------------------------------------ state */
static NcConfig g_cfg, g_saved;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;   /* guards g_touch + event queue */
static NcTouch g_touch;
static struct { int type; float x, y; } g_q[256];
static int g_qn = 0;

static void *volatile g_settings_this = 0;   /* the open SettingsScreenController, if any */
static bool  g_menu_open = false;
static bool  g_imgui_ready = false, g_imgui_failed = false;
static float g_base = 2.0f;                  /* pixel scale chosen from the screen height */
static NcFps g_fps;
static int   g_log_lines = 0;

/* ------------------------------------------------------------------ log */
static void nclog(const char *fmt, ...) {
    if (g_log_lines > 300) return;
    g_log_lines++;
    FILE *f = fopen(NC_LOG, "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static double now_s() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ------------------------------------------------------------------ game hooks */
typedef void (*fn_this)(void *);
typedef void (*fn_tick)(void *, void *);
static fn_this g_orig_onOpen = 0, g_orig_dtor = 0;
static fn_tick g_orig_tick = 0;

static void hook_settings_open(void *self) {
    if (g_orig_onOpen) g_orig_onOpen(self);
    g_settings_this = self;
    nclog("settings screen opened");
}

static void hook_settings_dtor(void *self) {
    if (self == g_settings_this) { g_settings_this = 0; nclog("settings screen closed"); }
    if (g_orig_dtor) g_orig_dtor(self);
}

/* MoveInputHandler::tick(LocalPlayer&): autosprint */
static void hook_tick(void *self, void *player) {
    if (g_orig_tick) g_orig_tick(self, player);
    if (!g_cfg.autosprint || !player) return;
    if (!mih_isMovingForward(self)) return;
    if (mob_isSneaking(player) || player_isUsingItem(player)) return;
    if (!mob_isSprinting(player)) lp_setSprinting(player, true);
}

/* ------------------------------------------------------------------ touch input */
typedef int32_t (*getEvent_fn)(AInputQueue *, AInputEvent **);
static getEvent_fn g_orig_getEvent = 0;

static void push_ev(void *, int type, float x, float y) {
    if (g_qn < 256) { g_q[g_qn].type = type; g_q[g_qn].x = x; g_q[g_qn].y = y; g_qn++; }
}

static int32_t hook_getEvent(AInputQueue *q, AInputEvent **out) {
    for (;;) {
        int32_t r = g_orig_getEvent(q, out);
        if (r < 0 || !out || !*out) return r;
        AInputEvent *ev = *out;
        if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_MOTION) {
            int action = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
            float x = AMotionEvent_getX(ev, 0), y = AMotionEvent_getY(ev, 0);
            pthread_mutex_lock(&g_mu);
            int swallow = nc_touch_event(&g_touch, action, x, y, push_ev, 0);
            pthread_mutex_unlock(&g_mu);
            if (swallow) { AInputQueue_finishEvent(q, ev, 1); continue; }
        }
        return r;
    }
}

/* ------------------------------------------------------------------ ImGui setup + theme */
static void apply_theme() {
    ImGuiStyle &s = ImGui::GetStyle();
    s.WindowRounding = 12; s.FrameRounding = 9; s.GrabRounding = 9; s.TabRounding = 9;
    s.ScrollbarRounding = 9; s.ChildRounding = 9; s.PopupRounding = 9;
    s.WindowBorderSize = 0; s.FrameBorderSize = 0;
    s.WindowPadding = ImVec2(14, 12); s.FramePadding = ImVec2(12, 9);
    s.ItemSpacing = ImVec2(12, 12); s.ItemInnerSpacing = ImVec2(10, 8);
    s.ScrollbarSize = 14; s.GrabMinSize = 22;

    const ImVec4 accent(0.50f, 0.42f, 1.00f, 1.00f);
    const ImVec4 accentDim(0.30f, 0.26f, 0.62f, 1.00f);
    ImVec4 *c = s.Colors;
    c[ImGuiCol_Text]             = ImVec4(0.92f, 0.92f, 0.96f, 1.00f);
    c[ImGuiCol_TextDisabled]     = ImVec4(0.52f, 0.52f, 0.62f, 1.00f);
    c[ImGuiCol_WindowBg]         = ImVec4(0.045f, 0.045f, 0.065f, 0.97f);
    c[ImGuiCol_TitleBg]          = ImVec4(0.07f, 0.07f, 0.10f, 1.00f);
    c[ImGuiCol_TitleBgActive]    = ImVec4(0.09f, 0.09f, 0.14f, 1.00f);
    c[ImGuiCol_FrameBg]          = ImVec4(0.11f, 0.11f, 0.16f, 1.00f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.15f, 0.15f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(0.18f, 0.17f, 0.28f, 1.00f);
    c[ImGuiCol_CheckMark]        = accent;
    c[ImGuiCol_SliderGrab]       = accent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.62f, 0.55f, 1.00f, 1.00f);
    c[ImGuiCol_Button]           = accentDim;
    c[ImGuiCol_ButtonHovered]    = ImVec4(0.38f, 0.33f, 0.75f, 1.00f);
    c[ImGuiCol_ButtonActive]     = accent;
    c[ImGuiCol_Header]           = accentDim;
    c[ImGuiCol_HeaderHovered]    = ImVec4(0.38f, 0.33f, 0.75f, 1.00f);
    c[ImGuiCol_HeaderActive]     = accent;
    c[ImGuiCol_Separator]        = ImVec4(0.20f, 0.20f, 0.30f, 1.00f);
    c[ImGuiCol_Tab]              = ImVec4(0.10f, 0.10f, 0.15f, 1.00f);
    c[ImGuiCol_TabHovered]       = ImVec4(0.38f, 0.33f, 0.75f, 1.00f);
    c[ImGuiCol_TabActive]        = accentDim;
    c[ImGuiCol_ScrollbarBg]      = ImVec4(0.03f, 0.03f, 0.05f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]    = accentDim;
}

static bool init_imgui(int w, int h) {
    (void)w;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;

    g_base = (float)h / 540.0f;
    if (g_base < 1.4f) g_base = 1.4f;

    ImFontConfig fc;
    fc.SizePixels = floorf(13.0f * g_base);
    io.Fonts->AddFontDefault(&fc);

    ImGui::StyleColorsDark();
    apply_theme();
    ImGui::GetStyle().ScaleAllSizes(g_base);

    if (!ImGui_ImplOpenGL3_Init("#version 100")) { nclog("ImGui GL backend init FAILED"); return false; }
    nclog("ImGui ready: screen height %d, scale %.2f, font %.0fpx", h, g_base, fc.SizePixels);
    return true;
}

/* ------------------------------------------------------------------ UI */
static void ui_tab_movement() {
    bool b = g_cfg.autosprint != 0;
    if (ImGui::Checkbox("Autosprint", &b)) g_cfg.autosprint = b ? 1 : 0;
    ImGui::TextDisabled("Sprint automatically while you move forward.");
}

static void ui_tab_hud() {
    bool b = g_cfg.fps_on != 0;
    if (ImGui::Checkbox("FPS counter", &b)) g_cfg.fps_on = b ? 1 : 0;
    if (g_cfg.fps_on) {
        ImGui::SliderFloat("X position", &g_cfg.fps_x, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Y position", &g_cfg.fps_y, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Text size", &g_cfg.fps_scale, 0.5f, 3.0f, "%.2f");
        bool bg = g_cfg.fps_bg != 0;
        if (ImGui::Checkbox("Dark background", &bg)) g_cfg.fps_bg = bg ? 1 : 0;
    }
}

static void ui_tab_client() {
    ImGui::SliderFloat("Menu size", &g_cfg.ui_scale, 0.6f, 1.6f, "%.2f");
    ImGui::SliderFloat("N button X", &g_cfg.n_x, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("N button Y", &g_cfg.n_y, 0.0f, 1.0f, "%.2f");
    bool a = g_cfg.n_always != 0;
    if (ImGui::Checkbox("Show N button everywhere (debug)", &a)) g_cfg.n_always = a ? 1 : 0;
    ImGui::Spacing();
    ImGui::TextDisabled("Saved in games/com.mojang/NightClient/config.txt");
}

static void draw_fps(float w, float h, float fps) {
    char buf[32];
    snprintf(buf, sizeof buf, "FPS: %d", (int)(fps + 0.5f));
    ImFont *font = ImGui::GetFont();
    float fs = font->FontSize * g_cfg.fps_scale;
    ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, buf);
    float pad = 6.0f * g_base * g_cfg.fps_scale;
    float x = g_cfg.fps_x * (w - sz.x - 2 * pad);
    float y = g_cfg.fps_y * (h - sz.y - 2 * pad);
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    if (g_cfg.fps_bg)
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + sz.x + 2 * pad, y + sz.y + 2 * pad), IM_COL32(8, 8, 14, 190), 6.0f * g_base);
    dl->AddText(font, fs, ImVec2(x + pad, y + pad), IM_COL32(235, 235, 245, 255), buf);
}

/* builds all windows for this frame and reports where the touchable ones are */
static void build_ui(float w, float h, float fps, bool in_settings, NcRect *n_rect, NcRect *win_rect) {
    n_rect->visible = 0; win_rect->visible = 0;
    ImGuiIO &io = ImGui::GetIO();

    if (g_cfg.fps_on) draw_fps(w, h, fps);

    /* ---- N button: only while a Settings screen is open ---- */
    if (in_settings && !g_menu_open) {
        float sz = h * 0.11f;
        ImGui::SetNextWindowPos(ImVec2(g_cfg.n_x * (w - sz), g_cfg.n_y * (h - sz)));
        ImGui::SetNextWindowSize(ImVec2(sz, sz));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::Begin("##night_n", NULL,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground);
        ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
        n_rect->visible = 1; n_rect->x = p.x; n_rect->y = p.y; n_rect->w = s.x; n_rect->h = s.y;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, sz * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.19f, 0.48f, 0.96f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.33f, 0.75f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.42f, 1.0f, 1.0f));
        ImGui::SetWindowFontScale(1.7f);
        if (ImGui::Button("N", ImVec2(sz, sz))) { g_menu_open = true; nclog("menu opened"); }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    /* ---- main window ---- */
    if (g_menu_open) {
        ImGui::SetNextWindowPos(ImVec2(w * 0.5f, h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(w * 0.72f, h * 0.84f), ImGuiCond_Always);
        bool open = true;
        ImGui::Begin("Night Client", &open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);
        ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
        win_rect->visible = 1; win_rect->x = p.x; win_rect->y = p.y; win_rect->w = s.x; win_rect->h = s.y;

        /* drag on empty space to scroll (touch) */
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(0))
            ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);

        ImGui::TextColored(ImVec4(0.62f, 0.55f, 1.0f, 1.0f), "Night Client 0.1   -   Minecraft PE 1.1.5");
        ImGui::Separator();
        if (ImGui::BeginTabBar("nc_tabs")) {
            if (ImGui::BeginTabItem("Movement")) { ui_tab_movement(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("HUD"))      { ui_tab_hud();      ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Client"))   { ui_tab_client();   ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(-FLT_MIN, 0))) open = false;
        ImGui::End();
        if (!open) { g_menu_open = false; nclog("menu closed"); }
    }
}

/* ------------------------------------------------------------------ per-frame entry (eglSwapBuffers) */
static void nc_frame(EGLDisplay d, EGLSurface s) {
    if (g_imgui_failed) return;

    EGLint w = 0, h = 0;
    eglQuerySurface(d, s, EGL_WIDTH, &w);
    eglQuerySurface(d, s, EGL_HEIGHT, &h);
    if (w <= 0 || h <= 0) return;

    double now = now_s();
    float fps = nc_fps_frame(&g_fps, now);

    bool in_settings = (g_settings_this != 0) || g_cfg.n_always;
    if (!in_settings) g_menu_open = false;
    bool need = g_cfg.fps_on || in_settings || g_menu_open;

    if (!need) {                                    /* nothing to draw: hand all touches to the game */
        pthread_mutex_lock(&g_mu);
        g_touch.n.visible = 0; g_touch.win.visible = 0; g_qn = 0;
        pthread_mutex_unlock(&g_mu);
        return;
    }

    if (!g_imgui_ready) {
        if (!init_imgui(w, h)) { g_imgui_failed = true; return; }
        g_imgui_ready = true;
    }

    ImGuiIO &io = ImGui::GetIO();

    /* touch events collected by the input hook */
    struct { int type; float x, y; } evs[256];
    int n;
    pthread_mutex_lock(&g_mu);
    n = g_qn; memcpy(evs, g_q, sizeof(evs[0]) * n); g_qn = 0;
    pthread_mutex_unlock(&g_mu);
    if (n) io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    for (int i = 0; i < n; i++) {
        io.AddMousePosEvent(evs[i].x, evs[i].y);
        if (evs[i].type == NC_EV_DOWN) io.AddMouseButtonEvent(0, true);
        if (evs[i].type == NC_EV_UP) { io.AddMouseButtonEvent(0, false); io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); }
    }

    static double last = 0.0;
    float dt = last > 0.0 ? (float)(now - last) : (1.0f / 60.0f);
    last = now;
    if (dt <= 0.0f) dt = 1.0f / 60.0f;
    if (dt > 0.25f) dt = 0.25f;

    ImGui_ImplOpenGL3_NewFrame();
    io.DisplaySize = ImVec2((float)w, (float)h);
    io.DeltaTime = dt;
    io.FontGlobalScale = g_cfg.ui_scale;
    ImGui::NewFrame();

    NcRect nr, wr;
    build_ui((float)w, (float)h, fps, in_settings, &nr, &wr);

    if (!nc_cfg_equal(&g_cfg, &g_saved) && !ImGui::IsMouseDown(0)) {
        if (nc_cfg_save(&g_cfg, NC_CFG)) g_saved = g_cfg;
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    pthread_mutex_lock(&g_mu);
    g_touch.n = nr; g_touch.win = wr;
    pthread_mutex_unlock(&g_mu);
}

typedef EGLBoolean (*swap_fn)(EGLDisplay, EGLSurface);
static swap_fn g_orig_swap = 0;
static EGLBoolean hook_swap(EGLDisplay d, EGLSurface s) {
    nc_frame(d, s);
    return g_orig_swap(d, s);
}

/* ------------------------------------------------------------------ startup */
/* The two "GOT hooks" need the game library to be fully loaded, so they are installed
 * from a helper thread a moment after startup (retrying for up to 40 seconds). */
static void *installer(void *) {
    int swap_done = 0, input_done = 0;
    for (int i = 0; i < 40 && !(swap_done && input_done); i++) {
        sleep(1);
        if (!swap_done) {
            int n = nc_got_hook("libminecraftpe.so", "eglSwapBuffers", (void *)hook_swap, (void **)&g_orig_swap);
            if (n > 0 || i == 0) nclog("hook eglSwapBuffers: %d slot(s)", n);
            swap_done = n > 0;
        }
        if (!input_done) {
            int n = nc_got_hook("libminecraftpe.so", "AInputQueue_getEvent", (void *)hook_getEvent, (void **)&g_orig_getEvent);
            if (n > 0 || i == 0) nclog("hook AInputQueue_getEvent: %d slot(s)", n);
            input_done = n > 0;
        }
    }
    nclog("installer finished: draw=%d input=%d", swap_done, input_done);
    return 0;
}

__attribute__((constructor))
static void nc_init(void) {
    mkdir("/sdcard/games/com.mojang/NightClient", 0777);
    FILE *f = fopen(NC_LOG, "w");
    if (f) { fputs("Night Client 0.1 loaded\n", f); fclose(f); }

    nc_cfg_defaults(&g_cfg);
    int had = nc_cfg_load(&g_cfg, NC_CFG);
    if (!had) nc_cfg_save(&g_cfg, NC_CFG);
    g_saved = g_cfg;
    nclog("config %s (autosprint=%d fps=%d)", had ? "loaded" : "created", g_cfg.autosprint, g_cfg.fps_on);

    tml_registerHook("_ZN16MoveInputHandler4tickER11LocalPlayer", (void *)hook_tick, (void **)&g_orig_tick);
    tml_registerHook("_ZN24SettingsScreenController6onOpenEv", (void *)hook_settings_open, (void **)&g_orig_onOpen);
    tml_registerHook("_ZN24SettingsScreenControllerD1Ev", (void *)hook_settings_dtor, (void **)&g_orig_dtor);
    nclog("game hooks registered");

    pthread_t t;
    if (pthread_create(&t, 0, installer, 0) == 0) pthread_detach(t);
}

extern "C" __attribute__((visibility("default"))) void tml_init(void) {}
