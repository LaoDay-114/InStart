#pragma once
// ============================================================
// InStart 全局配置与状态（菜单 <-> JNI 功能层共享）
// ============================================================

// 按键绑定项索引（menu/hooks 共用）
enum BindAction {
    BIND_MENU = 0,   // 呼出/隐藏菜单
    BIND_FLY,        // 飞行
    BIND_SPEED,      // 地面加速
    BIND_FULLBRIGHT, // 全亮
    BIND_ESP,        // 实体透视
    BIND_NOFALL,     // 无摔落
    BIND_HUD,        // 坐标 HUD
    BIND_COUNT
};

// 用户可调配置（菜单控制）
struct InStartConfig {
    bool  showMenu   = false; // 菜单显示
    bool  hud        = true;  // 坐标 HUD
    // 移动
    bool  fly        = false; // 飞行
    float flySpeed   = 2.0f;  // 飞行速度倍率（基础 0.05）
    bool  speed      = false; // 地面加速
    float speedMult  = 2.0f;  // 速度倍率（基础 0.1）
    // 视觉
    bool  fullbright = false; // 全亮（gamma=16）
    bool  esp        = false; // 实体透视（发光轮廓，穿墙可见）
    bool  espMobsOnly= false; // 仅生物（忽略掉落物/矿车等）
    // 保护
    bool  noFall     = false; // 无摔落伤害
    // 按键绑定（Windows VK 码，0 表示未绑定）
    int   bind[7]    = { 0xA5 /*VK_RMENU*/, 'F', 'G', 'B', 'V', 'N', 'H' };
    // 版本兼容状态
    bool  versionOk  = true;  // false = 检测到不兼容版本，功能全部停用
};

// 运行时状态（JNI 层回填）
struct InStartState {
    bool   jniReady     = false; // JNI 解析完成
    bool   inGame       = false; // 已进入世界（player != null）
    bool   injectShown  = false; // 已显示"注入完成"提示
    double px = 0, py = 0, pz = 0;
    char   version[64]  = {};    // 检测到的游戏版本号
};

extern InStartConfig g_cfg;
extern InStartState  g_state;
