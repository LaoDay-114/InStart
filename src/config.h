#pragma once

enum BindAction {
    BIND_MENU = 0,
    BIND_FLY,
    BIND_SPEED,
    BIND_FULLBRIGHT,
    BIND_ESP,
    BIND_NOFALL,
    BIND_HUD,
    BIND_KILLAURA,
    BIND_TOTEM,
    BIND_COUNT
};

struct InStartConfig {
    bool  showMenu   = false;
    bool  hud        = true;
    bool  fly        = false;
    float flySpeed   = 2.0f;
    bool  speed      = false;
    float speedMult  = 2.0f;
    bool  fullbright = false;
    bool  esp        = false;
    bool  espMobsOnly= false;
    bool  noFall     = false;
    bool  killaura   = false;
    float auraRange  = 3.0f;
    bool  kaExcludePlayers = false;
    bool  kaExcludeMobs    = false;
    bool  autoTotem  = false;

    int   bind[BIND_COUNT] = { 0xA5   };
};

struct InStartState {
    bool   jniReady     = false;
    bool   inGame       = false;
    bool   injectShown  = false;
    double px = 0, py = 0, pz = 0;
    char   version[64]  = {};
};

extern InStartConfig g_cfg;
extern InStartState  g_state;

void config_init_path(void* moduleHandle);
void config_load();
void config_save();
void config_sanitize();
