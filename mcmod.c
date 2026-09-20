/*
 * McMod for Toolbox (Minecraft PE 1.1.x, 32-bit ARM)
 *
 * v0.1: Autosprint - you sprint automatically while moving forward.
 *
 * Settings file (created on first run): games/com.mojang/mcmod.cfg
 *   autosprint=1   (1 = on, 0 = off)
 * Debug log: games/com.mojang/mcmod_log.txt
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MCMOD_DIR
#define MCMOD_DIR "/sdcard/games/com.mojang/"
#endif

/* ---- game functions (found in libminecraftpe.so when the mod loads) ---- */
extern _Bool mih_isMovingForward(void *self) __asm__("_ZNK16MoveInputHandler15isMovingForwardEv");
extern _Bool mob_isSneaking(void *self)      __asm__("_ZNK3Mob10isSneakingEv");
extern _Bool mob_isSprinting(void *self)     __asm__("_ZNK3Mob11isSprintingEv");
extern _Bool player_isUsingItem(void *self)  __asm__("_ZNK6Player11isUsingItemEv");
extern void  lp_setSprinting(void *self, _Bool on) __asm__("_ZN11LocalPlayer12setSprintingEb");

/* ---- Toolbox mod loader: hook registration (libmodloader.so) ---- */
extern void tml_registerHook(const char *symbol, void *hook, void **original)
    __asm__("_ZN3tml17StaticHookManager12registerHookEPKcPvPS3_");

static int g_autosprint = 1;

static void log_line(const char *msg) {
    FILE *f = fopen(MCMOD_DIR "mcmod_log.txt", "a");
    if (f) { fputs(msg, f); fputc('\n', f); fclose(f); }
}

static void load_config(void) {
    char line[128];
    FILE *f = fopen(MCMOD_DIR "mcmod.cfg", "r");
    if (!f) {
        f = fopen(MCMOD_DIR "mcmod.cfg", "w");
        if (f) {
            fputs("# McMod settings (1 = on, 0 = off)\nautosprint=1\n", f);
            fclose(f);
        }
        return;
    }
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "autosprint=", 11) == 0) g_autosprint = atoi(line + 11) != 0;
    }
    fclose(f);
}

/* ---- MoveInputHandler::tick(LocalPlayer&) hook ---- */
typedef void (*tick_fn)(void *self, void *player);
static tick_fn g_orig_tick = 0;
static int g_seen_tick = 0, g_seen_sprint = 0;

static void hook_tick(void *self, void *player) {
    if (g_orig_tick) g_orig_tick(self, player);

    if (!g_seen_tick) { g_seen_tick = 1; log_line("hook is running (MoveInputHandler::tick)"); }
    if (!g_autosprint || !player) return;

    if (!mih_isMovingForward(self)) return;
    if (mob_isSneaking(player) || player_isUsingItem(player)) return;

    if (!mob_isSprinting(player)) {
        lp_setSprinting(player, 1);
        if (!g_seen_sprint) { g_seen_sprint = 1; log_line("autosprint applied"); }
    }
}

/* Runs when the game loads this library. Toolbox's loader needs hooks to be
 * registered right here, during load. */
__attribute__((constructor))
static void mcmod_init(void) {
    FILE *f = fopen(MCMOD_DIR "mcmod_log.txt", "w");
    if (f) { fputs("McMod 0.1 loaded\n", f); fclose(f); }
    load_config();
    tml_registerHook("_ZN16MoveInputHandler4tickER11LocalPlayer", (void *)hook_tick, (void **)&g_orig_tick);
    log_line("hook registered");
}

/* Entry point the Toolbox loader looks for; nothing to do. */
void tml_init(void) { }
