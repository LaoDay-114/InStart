#pragma once
// 全局配置与状态（菜单 <-> JNI 功能层共享）

// 按键绑定项索引
enum BindAction {
    BIND_MENU = 0,   // 呼出/隐藏菜单
    BIND_FLY,        // 飞行
    BIND_SPEED,      // 地面加速
    BIND_FULLBRIGHT, // 全亮
    BIND_ESP,        // 实体透视
    BIND_NOFALL,     // 无摔落
    BIND_HUD,        // 坐标 HUD
    BIND_KILLAURA,   // 杀戮光环
    BIND_TOTEM,      // 自动图腾
    BIND_COUNT
};

// 用户可调配置（菜单控制）。增删/改序字段后递增 config.cpp 的 CFG_VER。
struct InStartConfig {
    bool  showMenu   = false;
    bool  hud        = true;
    // 移动
    bool  fly        = false;
    float flySpeed   = 2.0f;  // 飞行速度倍率（基础 0.05）
    bool  speed      = false;
    float speedMult  = 2.0f;  // 速度倍率（基础 0.1）
    // 视觉
    bool  fullbright = false; // gamma=16
    bool  esp        = false; // 发光轮廓，穿墙可见
    bool  espMobsOnly= false; // 仅生物，忽略掉落物/矿车
    // 保护
    bool  noFall     = false;
    // 战斗
    bool  killaura   = false;
    float auraRange  = 3.0f;
    bool  kaExcludePlayers = false;
    bool  kaExcludeMobs    = false;
    bool  autoTotem  = false;

    // Windows VK 码，0 = 未绑定。默认仅菜单=右 Alt
    int   bind[BIND_COUNT] = { 0xA5 /*VK_RMENU*/ };
};

// 运行时状态（JNI 层回填）
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
