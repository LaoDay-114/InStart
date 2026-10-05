// ============================================================
// InStart JNI 层：附着 JVM -> 检测版本 -> 按版本映射表定位类 ->
// 每帧把功能开关写入游戏对象（飞行/加速/全亮/无摔落/坐标/ESP）
// 支持版本：1.21 ~ 1.21.11（不兼容版本会停用全部功能）
// ============================================================
#include <jni.h>
#include <windows.h>
#include <cstring>
#include <cstdio>

#include "mappings.h"
#include "../config.h"

static JavaVM*  g_vm  = nullptr;
static JNIEnv*  g_env = nullptr;
static const McVerMap* g_map = nullptr; // 当前版本的映射表

// ---- 缓存的类 ----
static jclass c_MinecraftClient, c_GameOptions, c_SimpleOption,
              c_PlayerAbilities, c_PlayerEntity, c_Double,
              c_ClientWorld, c_LivingEntity;

// ---- 缓存的方法 ----
static jmethodID m_getInstance;          // MinecraftClient.getInstance()
static jmethodID m_sendAbilitiesUpdate;  // PlayerEntity.sendAbilitiesUpdate()
static jmethodID m_getX, m_getY, m_getZ; // Entity.getX/getY/getZ
static jmethodID m_setValue;             // SimpleOption.setValue(Object)
static jmethodID m_dblValueOf;           // Double.valueOf(double)
static jmethodID m_getEntities;          // ClientWorld.getEntities()
static jmethodID m_setGlowing;           // Entity.setGlowing(boolean)
static jmethodID m_it_iterator, m_it_hasNext, m_it_next; // Iterable/Iterator

// ---- 缓存的字段 ----
static jfieldID f_mc_player;             // MinecraftClient.player
static jfieldID f_mc_world;              // MinecraftClient.world
static jfieldID f_mc_options;            // MinecraftClient.options
static jfieldID f_player_abilities;      // PlayerEntity.abilities
static jfieldID f_ab_allowFlying, f_ab_flying, f_ab_flySpeed, f_ab_walkSpeed;
static jfieldID f_ent_fallDistance;      // Entity.fallDistance（签名随版本 F/D）
static jfieldID f_go_gamma;              // GameOptions.gamma (SimpleOption)

// ---- 边沿触发的上次状态 ----
static bool  prevFly = false, prevSpeed = false, prevFb = false, prevEsp = false;
static float prevFlySp = -1.f, prevSpMult = -1.f;
static int   espTick = 0;

// 类查找：官方启动器（系统类加载器）用 FindClass 即可；
// Fabric/Knot 环境下退化为用当前线程上下文类加载器 Class.forName
static jclass find_class(JNIEnv* env, const char* name) {
    jclass c = env->FindClass(name);
    if (c) return c;
    if (env->ExceptionCheck()) env->ExceptionClear();

    jclass threadCls = env->FindClass("java/lang/Thread");
    if (!threadCls) { if (env->ExceptionCheck()) env->ExceptionClear(); return nullptr; }
    jmethodID cur = env->GetStaticMethodID(threadCls, "currentThread", "()Ljava/lang/Thread;");
    jmethodID gcl = env->GetMethodID(threadCls, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
    jobject self = env->CallStaticObjectMethod(threadCls, cur);
    if (!self || env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
    jobject loader = env->CallObjectMethod(self, gcl);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
    if (!loader) return nullptr;

    jclass clCls = env->FindClass("java/lang/ClassLoader");
    jmethodID forName = env->GetStaticMethodID(
        clCls, "forName", "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;");
    jstring js = env->NewStringUTF(name);
    jobject clsObj = env->CallStaticObjectMethod(clCls, forName, js, JNI_FALSE, loader);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
    return (jclass)clsObj;
}

// 从变体列表中逐个尝试查找类
static jclass find_class_any(JNIEnv* env, const char* const* variants) {
    for (int i = 0; variants[i]; ++i) {
        jclass c = find_class(env, variants[i]);
        if (c) return c;
    }
    return nullptr;
}

// 从变体列表中逐个尝试获取静态方法 ID
static jmethodID get_static_mid_any(JNIEnv* env, jclass cls,
                                    const char* const* names, const char* sig) {
    for (int i = 0; names[i]; ++i) {
        jmethodID m = env->GetStaticMethodID(cls, names[i], sig);
        if (m) return m;
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    return nullptr;
}

// 从变体列表中逐个尝试获取实例方法 ID
static jmethodID get_mid_any(JNIEnv* env, jclass cls,
                             const char* const* names, const char* sig) {
    for (int i = 0; names[i]; ++i) {
        jmethodID m = env->GetMethodID(cls, names[i], sig);
        if (m) return m;
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    return nullptr;
}

// 调用 getGameVersion 获取版本字符串
static bool detect_version(JNIEnv* env, jclass mcCls, jobject mc, char out[64]) {
    jmethodID mVer = get_mid_any(env, mcCls, g_bootMGetGameVersion,
                                 "()Ljava/lang/String;");
    if (!mVer) return false;
    jobject s = env->CallObjectMethod(mc, mVer);
    if (!s || env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    const char* u8 = env->GetStringUTFChars((jstring)s, nullptr);
    strncpy(out, u8, 63);
    out[63] = '\0';
    env->ReleaseStringUTFChars((jstring)s, u8);
    env->DeleteLocalRef(s);
    return true;
}

// 按版本号查映射表；找到返回 true 并设置 g_map
static bool pick_mappings(const char* version) {
    for (int i = 0; i < g_mcVerMapsCount; ++i) {
        if (strcmp(g_mcVerMaps[i].version, version) == 0) {
            g_map = &g_mcVerMaps[i];
            return true;
        }
    }
    return false;
}

#define RESOLVE_CLASS(var, name) do { \
    jclass _c = find_class(env, name); \
    if (!_c) return false; \
    var = (jclass)env->NewGlobalRef(_c); \
} while (0)

#define RESOLVE_FIELD(var, cls, name, sig) do { \
    var = env->GetFieldID(cls, name, sig); \
    if (!var) return false; \
} while (0)

#define RESOLVE_METHOD(var, cls, name, sig) do { \
    var = env->GetMethodID(cls, name, sig); \
    if (!var) return false; \
} while (0)

bool mc_jni_init() {
    if (g_env) return true;
    if (!g_cfg.versionOk) return false; // 已判定不兼容，不再重试

    // 1. 取进程内已存在的 JVM（jvm.dll 已随 javaw.exe 加载）
    if (!g_vm) {
        using GetCreatedVMs_t = jint (*)(JavaVM**, jsize, jsize*);
        auto fn = (GetCreatedVMs_t)GetProcAddress(GetModuleHandleA("jvm.dll"), "JNI_GetCreatedJavaVMs");
        if (!fn) return false;
        JavaVM* vms[4] = {};
        jsize n = 0;
        if (fn(vms, 4, &n) != JNI_OK || n == 0) return false;
        g_vm = vms[0];
    }

    // 2. 获取 JNIEnv（渲染线程本身是 Java 线程，一般已附着）
    JNIEnv* env = nullptr;
    if (g_vm->GetEnv((void**)&env, JNI_VERSION_1_8) == JNI_EDETACHED)
        g_vm->AttachCurrentThread((void**)&env, nullptr);
    if (!env) return false;

    // 3. 引导：找 MinecraftClient 类 -> getInstance -> getGameVersion
    jclass bootMcCls = find_class_any(env, g_bootClsMc);
    if (!bootMcCls) return false;
    jmethodID bootGetInst = get_static_mid_any(env, bootMcCls, g_bootMGetInstance,
                                               "()Lnet/minecraft/class_310;");
    if (!bootGetInst) return false;
    jobject bootMc = env->CallStaticObjectMethod(bootMcCls, bootGetInst);
    if (!bootMc || env->ExceptionCheck()) { env->ExceptionClear(); return false; }

    char ver[64] = {};
    if (!detect_version(env, bootMcCls, bootMc, ver)) {
        env->DeleteLocalRef(bootMc);
        return false;
    }
    strncpy(g_state.version, ver, sizeof g_state.version - 1);
    env->DeleteLocalRef(bootMc);

    // 4. 按版本选映射；不兼容则停用功能并提示
    if (!pick_mappings(ver)) {
        g_cfg.versionOk = false;
        return false;
    }

    // 5. 用当前版本映射表解析全部类/方法/字段
    const McVerMap& M = *g_map;
    RESOLVE_CLASS(c_MinecraftClient, M.clsMinecraftClient);
    RESOLVE_CLASS(c_GameOptions,     M.clsGameOptions);
    RESOLVE_CLASS(c_SimpleOption,    M.clsSimpleOption);
    RESOLVE_CLASS(c_PlayerAbilities, M.clsPlayerAbilities);
    RESOLVE_CLASS(c_PlayerEntity,    M.clsPlayerEntity);
    RESOLVE_CLASS(c_ClientWorld,     M.clsClientWorld);
    RESOLVE_CLASS(c_LivingEntity,    M.clsLivingEntity);
    jclass dbl = find_class(env, "java/lang/Double");
    if (!dbl) return false;
    c_Double = (jclass)env->NewGlobalRef(dbl);

    // 方法
    m_getInstance = env->GetStaticMethodID(c_MinecraftClient, M.mGetInstance,
                                           "()Lnet/minecraft/class_310;");
    if (!m_getInstance) return false;
    RESOLVE_METHOD(m_sendAbilitiesUpdate, c_PlayerEntity, M.mSendAbilitiesUpdate, "()V");
    RESOLVE_METHOD(m_getX, c_PlayerEntity, M.mGetX, "()D");
    RESOLVE_METHOD(m_getY, c_PlayerEntity, M.mGetY, "()D");
    RESOLVE_METHOD(m_getZ, c_PlayerEntity, M.mGetZ, "()D");
    RESOLVE_METHOD(m_setValue, c_SimpleOption, M.mSetValue, "(Ljava/lang/Object;)V");
    m_dblValueOf = env->GetStaticMethodID(c_Double, "valueOf", "(D)Ljava/lang/Double;");
    if (!m_dblValueOf) return false;
    RESOLVE_METHOD(m_getEntities, c_ClientWorld, M.mGetEntities, "()Ljava/lang/Iterable;");
    RESOLVE_METHOD(m_setGlowing, c_PlayerEntity, M.mSetGlowing, "(Z)V");
    {
        jclass iterable = find_class(env, "java/lang/Iterable");
        jclass iterator = find_class(env, "java/util/Iterator");
        if (!iterable || !iterator) return false;
        m_it_iterator = env->GetMethodID(iterable, "iterator", "()Ljava/util/Iterator;");
        m_it_hasNext  = env->GetMethodID(iterator, "hasNext", "()Z");
        m_it_next     = env->GetMethodID(iterator, "next", "()Ljava/lang/Object;");
        if (!m_it_iterator || !m_it_hasNext || !m_it_next) return false;
    }

    // 字段
    char sig[64];
    snprintf(sig, sizeof sig, "L%s;", M.clsClientPlayerEntity);
    RESOLVE_FIELD(f_mc_player, c_MinecraftClient, M.fMcPlayer, sig);
    snprintf(sig, sizeof sig, "L%s;", M.clsClientWorld);
    RESOLVE_FIELD(f_mc_world, c_MinecraftClient, M.fMcWorld, sig);
    snprintf(sig, sizeof sig, "L%s;", M.clsGameOptions);
    RESOLVE_FIELD(f_mc_options, c_MinecraftClient, M.fMcOptions, sig);
    snprintf(sig, sizeof sig, "L%s;", M.clsPlayerAbilities);
    RESOLVE_FIELD(f_player_abilities, c_PlayerEntity, M.fPlayerAbilities, sig);
    RESOLVE_FIELD(f_ab_allowFlying, c_PlayerAbilities, M.fAllowFlying, "Z");
    RESOLVE_FIELD(f_ab_flying,      c_PlayerAbilities, M.fFlying, "Z");
    RESOLVE_FIELD(f_ab_flySpeed,    c_PlayerAbilities, M.fFlySpeed, "F");
    RESOLVE_FIELD(f_ab_walkSpeed,   c_PlayerAbilities, M.fWalkSpeed, "F");
    // fallDistance：1.21.8 及以前是 float(F)，1.21.9+ 是 double(D)
    RESOLVE_FIELD(f_ent_fallDistance, c_PlayerEntity, M.fFallDistance,
                  M.fallDistType == 'D' ? "D" : "F");
    snprintf(sig, sizeof sig, "L%s;", M.clsSimpleOption);
    RESOLVE_FIELD(f_go_gamma, c_GameOptions, M.fGamma, sig);

    g_env = env;
    g_state.jniReady = true;
    return true;
}

void mc_apply_features() {
    if (!g_cfg.versionOk) return;      // 版本不兼容，全部停用
    if (!g_env && !mc_jni_init()) return;
    JNIEnv* env = g_env;
    if (env->ExceptionCheck()) env->ExceptionClear();

    jobject mc = env->CallStaticObjectMethod(c_MinecraftClient, m_getInstance);
    if (!mc) return;
    jobject player = env->GetObjectField(mc, f_mc_player);
    if (!player) {
        g_state.inGame = false;
        env->DeleteLocalRef(mc);
        return;
    }
    g_state.inGame = true;

    jobject abilities = env->GetObjectField(player, f_player_abilities);
    if (abilities) {
        bool needSync = false;

        // ---- 飞行 ----
        if (g_cfg.fly) {
            env->SetBooleanField(abilities, f_ab_allowFlying, JNI_TRUE);
            env->SetBooleanField(abilities, f_ab_flying, JNI_TRUE);
            env->SetFloatField(abilities, f_ab_flySpeed, 0.05f * g_cfg.flySpeed);
            if (!prevFly || prevFlySp != g_cfg.flySpeed) needSync = true;
        } else if (prevFly) {
            env->SetBooleanField(abilities, f_ab_allowFlying, JNI_FALSE);
            env->SetBooleanField(abilities, f_ab_flying, JNI_FALSE);
            env->SetFloatField(abilities, f_ab_flySpeed, 0.05f);
            needSync = true;
        }

        // ---- 地面加速 ----
        if (g_cfg.speed) {
            env->SetFloatField(abilities, f_ab_walkSpeed, 0.1f * g_cfg.speedMult);
            if (!prevSpeed || prevSpMult != g_cfg.speedMult) needSync = true;
        } else if (prevSpeed) {
            env->SetFloatField(abilities, f_ab_walkSpeed, 0.1f);
            needSync = true;
        }

        // 仅在状态变化时同步能力包（避免每帧发包）
        if (needSync) env->CallVoidMethod(player, m_sendAbilitiesUpdate);
    }
    prevFly = g_cfg.fly;  prevFlySp = g_cfg.flySpeed;
    prevSpeed = g_cfg.speed; prevSpMult = g_cfg.speedMult;
    if (abilities) env->DeleteLocalRef(abilities);

    // ---- 全亮：gamma SimpleOption 设为 16.0 ----
    if (g_cfg.fullbright != prevFb) {
        jobject options = env->GetObjectField(mc, f_mc_options);
        if (options) {
            jobject gamma = env->GetObjectField(options, f_go_gamma);
            if (gamma) {
                jobject d = env->CallStaticObjectMethod(c_Double, m_dblValueOf,
                                                        g_cfg.fullbright ? 16.0 : 1.0);
                env->CallVoidMethod(gamma, m_setValue, d);
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(d);
                env->DeleteLocalRef(gamma);
            }
            env->DeleteLocalRef(options);
        }
        prevFb = g_cfg.fullbright;
    }

    // ---- ESP：实体发光透视（穿墙可见），每 10 帧遍历一次 ----
    bool espOffEdge = !g_cfg.esp && prevEsp; // 从开变关的边沿
    if ((g_cfg.esp && (++espTick % 10 == 0)) || espOffEdge) {
        jobject world = env->GetObjectField(mc, f_mc_world);
        if (world) {
            jobject iter = nullptr;
            jobject entities = env->CallObjectMethod(world, m_getEntities);
            if (entities) iter = env->CallObjectMethod(entities, m_it_iterator);
            while (iter && env->CallBooleanMethod(iter, m_it_hasNext)) {
                jobject ent = env->CallObjectMethod(iter, m_it_next);
                if (!ent) break;
                // 跳过玩家自己
                if (env->IsSameObject(ent, player)) { env->DeleteLocalRef(ent); continue; }
                if (g_cfg.esp) {
                    bool match = !g_cfg.espMobsOnly ||
                                 env->IsInstanceOf(ent, c_LivingEntity);
                    if (match) env->CallVoidMethod(ent, m_setGlowing, JNI_TRUE);
                } else {
                    env->CallVoidMethod(ent, m_setGlowing, JNI_FALSE);
                }
                env->DeleteLocalRef(ent);
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (iter) env->DeleteLocalRef(iter);
            if (entities) env->DeleteLocalRef(entities);
            env->DeleteLocalRef(world);
        }
    }
    prevEsp = g_cfg.esp;

    // ---- 无摔落伤害：每帧清零摔落距离（签名随版本不同）----
    if (g_cfg.noFall) {
        if (g_map->fallDistType == 'D')
            env->SetDoubleField(player, f_ent_fallDistance, 0.0);
        else
            env->SetFloatField(player, f_ent_fallDistance, 0.0f);
    }

    // ---- HUD 坐标 ----
    if (g_cfg.hud) {
        g_state.px = env->CallDoubleMethod(player, m_getX);
        g_state.py = env->CallDoubleMethod(player, m_getY);
        g_state.pz = env->CallDoubleMethod(player, m_getZ);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    env->DeleteLocalRef(player);
    env->DeleteLocalRef(mc);
}
