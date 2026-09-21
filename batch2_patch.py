#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parent
H = ROOT / "nc_core.h"
CPP = ROOT / "nightclient.cpp"

h = H.read_text()
cpp = CPP.read_text()

old = '''  X(elytra_on,    I, 1,    0, 1) X(elytra_x, F, 0.50, 0, 1) X(elytra_y, F, 0.56, 0, 1) X(elytra_size, I, 3, 1, 8) X(elytra_style, I, 0, 0, 2) \\
  X(elytra_alpha, F, 1.0,  0.1, 1) \\
'''
new = '''  X(elytra_on,    I, 1,    0, 1) X(elytra_x, F, 0.50, 0, 1) X(elytra_y, F, 0.56, 0, 1) X(elytra_size, I, 3, 1, 8) X(elytra_style, I, 0, 0, 2) \\
  X(elytra_alpha, F, 1.0,  0.1, 1) X(elytra_angle_on, I, 1, 0, 1) X(elytra_angle_x, F, 0.50, 0, 1) X(elytra_angle_y, F, 0.64, 0, 1) X(elytra_angle_size, I, 3, 1, 8) X(elytra_angle_alpha, F, 1.0, 0.1, 1) \\
  X(arrow_on, I, 1, 0, 1) X(arrow_x, F, 0.03, 0, 1) X(arrow_y, F, 0.84, 0, 1) X(arrow_size, I, 3, 1, 8) X(arrow_alpha, F, 1.0, 0.1, 1) \\
  X(f3_on, I, 0, 0, 1) X(f3_x, F, 0.03, 0, 1) X(f3_y, F, 0.05, 0, 1) X(f3_size, I, 2, 1, 8) X(f3_alpha, F, 1.0, 0.1, 1) \\
  X(quickdrop_on, I, 0, 0, 1) X(saturation_on, I, 0, 0, 1) X(saturation_x, F, 0.03, 0, 1) X(saturation_y, F, 0.90, 0, 1) X(saturation_size, I, 3, 1, 8) X(saturation_alpha, F, 1.0, 0.1, 1) \\
'''
if old not in h:
    raise SystemExit("nc_core.h layout mismatch")
H.write_text(h.replace(old, new, 1))

cpp = cpp.replace('#include <sys/stat.h>\n', '#include <sys/stat.h>\n#include <dlfcn.h>\n', 1)
cpp = cpp.replace('#define NC_VERSION "0.2.1"', '#define NC_VERSION "0.3.0"', 1)

old = '''extern "C" bool mob_isGliding(void *self)       __asm__("_ZNK3Mob9isGlidingEv");
extern "C" bool player_isUsingItem(void *self)  __asm__("_ZNK6Player11isUsingItemEv");
'''
new = '''extern "C" bool mob_isGliding(void *self)       __asm__("_ZNK3Mob9isGlidingEv");
extern "C" float mob_getPitch(void *self)         __asm__("_ZNK3Mob8getPitchEv");
extern "C" bool player_isUsingItem(void *self)  __asm__("_ZNK6Player11isUsingItemEv");
'''
if old not in cpp:
    raise SystemExit("mob declarations mismatch")
cpp = cpp.replace(old, new, 1)

cpp = cpp.replace(
    'static struct { volatile int gliding; int present[4], id[4], dur[4], max[4]; } g_snap;\n',
    '''static struct { volatile int gliding; float pitch; int present[4], id[4], dur[4], max[4]; } g_snap;
static volatile int g_arrow_count = 0;
static volatile int g_saturation = 0;
''', 1)

cpp = cpp.replace(
    'if (g_cfg.elytra_on) g_snap.gliding = mob_isGliding(player) ? 1 : 0;',
    '''if (g_cfg.elytra_on || g_cfg.elytra_angle_on) {
        g_snap.gliding = mob_isGliding(player) ? 1 : 0;
        g_snap.pitch = mob_getPitch(player);
    }''', 1)

# Batch-2 HUDs are deliberately client-side visual overlays.
marker = '/* ---- round/square buttons (N, Zoom, Perspective) with preset labels ---- */'
extra = r'''/* ---- Batch 2 visual HUDs ---- */
static ImVec2 size_elytra_angle() {
    int s=g_cfg.elytra_angle_size,pad=2*s;
    ImVec2 t=txt("-90.0°",s);
    return V(t.x+2*pad,t.y+2*pad);
}
static void draw_elytra_angle(ImDrawList *dl, ImVec2 p) {
    int s=g_cfg.elytra_angle_size; char b[32];
    snprintf(b,sizeof b,"%.1f°",(double)g_snap.pitch);
    put_text_sh(dl,V(p.x+2*s,p.y+2*s),s,
                rgba(235,235,245,g_cfg.elytra_angle_alpha),b,true);
}
static ImVec2 size_arrow() {
    int s=g_cfg.arrow_size,pad=2*s;
    ImVec2 t=txt("999",s);
    return V(24*s+t.x+2*pad,18*s+2*pad);
}
static void draw_arrow_overlay(ImDrawList *dl, ImVec2 p) {
    int s=g_cfg.arrow_size; float x=p.x+2*s,y=p.y+3*s;
    ImU32 col=rgba(235,235,245,g_cfg.arrow_alpha);
    dl->AddLine(V(x,y+4*s),V(x+8*s,y+4*s),col,1.5f*s);
    dl->AddLine(V(x+5*s,y+s),V(x+8*s,y+4*s),col,1.5f*s);
    dl->AddLine(V(x+5*s,y+7*s),V(x+8*s,y+4*s),col,1.5f*s);
    char b[16]; snprintf(b,sizeof b,"%d",(int)g_arrow_count);
    put_text_sh(dl,V(x+11*s,y),s,col,b,true);
}
static ImVec2 size_f3() {
    int s=g_cfg.f3_size,pad=2*s;
    return V(txt("F3 FPS 000",s).x+2*pad,4*fpx(s)+2*pad);
}
static void draw_f3(ImDrawList *dl, ImVec2 p, float fps) {
    int s=g_cfg.f3_size,pad=2*s; char b[96];
    snprintf(b,sizeof b,"F3  FPS %d\\nELYTRA %.1f°\\nARROWS %d",
             (int)(fps+0.5f),(double)g_snap.pitch,(int)g_arrow_count);
    box(dl,p,size_f3(),g_cfg.f3_alpha,s);
    float yy=p.y+pad; const char *q=b; char line[48];
    for(int i=0;i<3;i++){
        int n=0; while(q[n] && q[n]!='\\n' && n<(int)sizeof(line)-1){line[n]=q[n];n++;}
        line[n]=0; put_text_sh(dl,V(p.x+pad,yy),s,rgba(235,235,245,g_cfg.f3_alpha),line,true);
        yy+=fpx(s)+s; if(!q[n]) break; q+=n+1;
    }
}
static ImVec2 size_saturation() {
    int s=g_cfg.saturation_size,pad=2*s;
    ImVec2 t=txt("Saturation: 20",s);
    return V(t.x+2*pad,t.y+2*pad);
}
static void draw_saturation(ImDrawList *dl, ImVec2 p) {
    int s=g_cfg.saturation_size,pad=2*s; char b[32];
    snprintf(b,sizeof b,"Saturation: %d",(int)g_saturation);
    box(dl,p,size_saturation(),g_cfg.saturation_alpha,s);
    put_text(dl,V(p.x+pad,p.y+pad),s,rgba(235,235,245,g_cfg.saturation_alpha),b,true);
}

'''
if marker not in cpp:
    raise SystemExit("button marker mismatch")
cpp = cpp.replace(marker, extra + marker, 1)

cpp = cpp.replace(
    'enum { E_FPS, E_ARMOR, E_ELYTRA, E_ZOOM, E_PERSP, E_N, E_COUNT };',
    'enum { E_FPS, E_ARMOR, E_ELYTRA, E_ELYTRA_ANGLE, E_ARROW, E_F3, E_SATURATION, E_ZOOM, E_PERSP, E_N, E_COUNT };',
    1)

# Replace the existing element accessors with batch-2-aware versions.
a = cpp.index('static int *elem_on(int e) {')
b = cpp.index('/* ------------------------------------------------------------------ "Move on screen" */', a)
access = r'''static int *elem_on(int e) {
    switch(e){
        case E_FPS:return &g_cfg.fps_on; case E_ARMOR:return &g_cfg.armor_on;
        case E_ELYTRA:return &g_cfg.elytra_on; case E_ELYTRA_ANGLE:return &g_cfg.elytra_angle_on;
        case E_ARROW:return &g_cfg.arrow_on; case E_F3:return &g_cfg.f3_on; case E_SATURATION:return &g_cfg.saturation_on;
        case E_ZOOM:return &g_cfg.zoom_on; case E_PERSP:return &g_cfg.persp_on; default:return 0;
    }
}
static float *elem_x(int e) {
    switch(e){
        case E_FPS:return &g_cfg.fps_x; case E_ARMOR:return &g_cfg.armor_x; case E_ELYTRA:return &g_cfg.elytra_x;
        case E_ELYTRA_ANGLE:return &g_cfg.elytra_angle_x; case E_ARROW:return &g_cfg.arrow_x; case E_F3:return &g_cfg.f3_x;
        case E_SATURATION:return &g_cfg.saturation_x; case E_ZOOM:return &g_cfg.zoom_x; case E_PERSP:return &g_cfg.persp_x;
        default:return &g_cfg.n_x;
    }
}
static float *elem_y(int e) {
    switch(e){
        case E_FPS:return &g_cfg.fps_y; case E_ARMOR:return &g_cfg.armor_y; case E_ELYTRA:return &g_cfg.elytra_y;
        case E_ELYTRA_ANGLE:return &g_cfg.elytra_angle_y; case E_ARROW:return &g_cfg.arrow_y; case E_F3:return &g_cfg.f3_y;
        case E_SATURATION:return &g_cfg.saturation_y; case E_ZOOM:return &g_cfg.zoom_y; case E_PERSP:return &g_cfg.persp_y;
        default:return &g_cfg.n_y;
    }
}
static ImVec2 elem_size(int e) {
    switch(e){
        case E_FPS:return size_fps(); case E_ARMOR:return size_armor(); case E_ELYTRA:return size_elytra();
        case E_ELYTRA_ANGLE:return size_elytra_angle(); case E_ARROW:return size_arrow(); case E_F3:return size_f3();
        case E_SATURATION:return size_saturation();
        case E_ZOOM:return btn_size(pick(LBL_ZOOM,NC_COUNT_OF(LBL_ZOOM),g_cfg.zoom_label),g_cfg.zoom_btn);
        case E_PERSP:return btn_size(pick(LBL_PERSP,NC_COUNT_OF(LBL_PERSP),g_cfg.persp_label),g_cfg.persp_btn);
        default:return btn_size(pick(LBL_N,NC_COUNT_OF(LBL_N),g_cfg.n_label),g_cfg.n_btn);
    }
}
'''
cpp = cpp[:a] + access + cpp[b:]

cpp = cpp.replace(
    '''            case E_ELYTRA: draw_elytra(dl, pos); break;
            case E_ZOOM:   draw_button''',
    '''            case E_ELYTRA: draw_elytra(dl, pos); break;
            case E_ELYTRA_ANGLE: draw_elytra_angle(dl,pos); break;
            case E_ARROW: draw_arrow_overlay(dl,pos); break;
            case E_F3: draw_f3(dl,pos,60.0f); break;
            case E_SATURATION: draw_saturation(dl,pos); break;
            case E_ZOOM:   draw_button''',
    1)

# Natural finger scrolling.
cpp = cpp.replace(
    'ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);',
    'ImGuiWindowFlags_NoScrollbar);',
    1)

# Add panels.
marker = 'static void panel_client() {'
panels = r'''static void panel_elytra_angle() {
    head("Elytra angle",&g_cfg.elytra_angle_on,"Shows your current pitch while gliding.");
    hud_look(&g_cfg.elytra_angle_size,&g_cfg.elytra_angle_alpha);
    hud_pos(&g_cfg.elytra_angle_x,&g_cfg.elytra_angle_y);
}
static void panel_arrow() {
    head("Arrow overlay",&g_cfg.arrow_on,"Shows an arrow icon and your arrow count.");
    hud_look(&g_cfg.arrow_size,&g_cfg.arrow_alpha);
    hud_pos(&g_cfg.arrow_x,&g_cfg.arrow_y);
}
static void panel_f3() {
    head("F3",&g_cfg.f3_on,"Compact Java-style information overlay.");
    hud_look(&g_cfg.f3_size,&g_cfg.f3_alpha);
    hud_pos(&g_cfg.f3_x,&g_cfg.f3_y);
}
static void panel_quickdrop() {
    head("Quick drop",&g_cfg.quickdrop_on,"Quick-drop UI module.");
    ImGui::TextDisabled("Inventory drop hook is reserved for the next implementation pass.");
}
static void panel_saturation() {
    head("Saturation",&g_cfg.saturation_on,"Saturation overlay.");
    hud_look(&g_cfg.saturation_size,&g_cfg.saturation_alpha);
    hud_pos(&g_cfg.saturation_x,&g_cfg.saturation_y);
}
'''
if marker not in cpp:
    raise SystemExit("panel marker mismatch")
cpp = cpp.replace(marker, panels + marker, 1)

old = '''    { "Elytra indicator",   &g_cfg.elytra_on,  panel_elytra },
    { "No hurt cam",        &g_cfg.nohurt,     panel_nohurt },
'''
new = '''    { "Elytra indicator",   &g_cfg.elytra_on,  panel_elytra },
    { "Elytra angle",        &g_cfg.elytra_angle_on, panel_elytra_angle },
    { "Arrow overlay",       &g_cfg.arrow_on, panel_arrow },
    { "F3",                  &g_cfg.f3_on, panel_f3 },
    { "Quick drop",          &g_cfg.quickdrop_on, panel_quickdrop },
    { "Saturation",          &g_cfg.saturation_on, panel_saturation },
    { "No hurt cam",        &g_cfg.nohurt,     panel_nohurt },
'''
if old not in cpp:
    raise SystemExit("mod list marker mismatch")
cpp = cpp.replace(old,new,1)

# Render visibility and HUDs.
old = '''    bool elytra_vis = g_cfg.elytra_on && in_world && g_snap.gliding && !g_menu_open && !g_edit;
    bool hud_btns   = play_hud && !in_settings && !g_menu_open && !g_edit;
'''
new = '''    bool elytra_vis = g_cfg.elytra_on && in_world && g_snap.gliding && !g_menu_open && !g_edit;
    bool angle_vis  = g_cfg.elytra_angle_on && in_world && g_snap.gliding && !g_menu_open && !g_edit;
    bool arrow_vis  = g_cfg.arrow_on && in_world && !g_menu_open && !g_edit;
    bool f3_vis     = g_cfg.f3_on && in_world && !g_menu_open && !g_edit;
    bool sat_vis    = g_cfg.saturation_on && in_world && !g_menu_open && !g_edit;
    bool hud_btns   = play_hud && !in_settings && !g_menu_open && !g_edit;
'''
if old not in cpp:
    raise SystemExit("visibility marker mismatch")
cpp = cpp.replace(old,new,1)

cpp = cpp.replace(
    'bool need = fps_vis || armor_vis || elytra_vis || zoom_vis || persp_vis || in_settings || g_menu_open || g_edit;',
    'bool need = fps_vis || armor_vis || elytra_vis || angle_vis || arrow_vis || f3_vis || sat_vis || zoom_vis || persp_vis || in_settings || g_menu_open || g_edit;',
    1)

old = '''        if (elytra_vis) draw_elytra(fg, place(g_cfg.elytra_x, g_cfg.elytra_y, size_elytra()));
        if (zoom_vis) {
'''
new = '''        if (elytra_vis) draw_elytra(fg, place(g_cfg.elytra_x, g_cfg.elytra_y, size_elytra()));
        if (angle_vis) draw_elytra_angle(fg, place(g_cfg.elytra_angle_x, g_cfg.elytra_angle_y, size_elytra_angle()));
        if (arrow_vis) draw_arrow_overlay(fg, place(g_cfg.arrow_x, g_cfg.arrow_y, size_arrow()));
        if (f3_vis) draw_f3(fg, place(g_cfg.f3_x, g_cfg.f3_y, size_f3()), fps);
        if (sat_vis) draw_saturation(fg, place(g_cfg.saturation_x, g_cfg.saturation_y, size_saturation()));
        if (zoom_vis) {
'''
if old not in cpp:
    raise SystemExit("render marker mismatch")
cpp = cpp.replace(old,new,1)

CPP.write_text(cpp)
print("Batch 2 patch applied.")
