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
              c_ClientWorld, c_LivingEntity,
              c_InteractionManager, c_PlayerInventory, c_ItemStack,
              c_Items, c_SlotActionType;

// ---- 缓存的方法 ----
static jmethodID m_getInstance;          // MinecraftClient.getInstance()
static jmethodID m_sendAbilitiesUpdate;  // PlayerEntity.sendAbilitiesUpdate()
static jmethodID m_getX, m_getY, m_getZ; // Entity.getX/getY/getZ
static jmethodID m_setValue;             // SimpleOption.setValue(Object)
static jmethodID m_dblValueOf;           // Double.valueOf(double)
static jmethodID m_getEntities;          // ClientWorld.getEntities()
static jmethodID m_setGlowing;           // Entity.setGlowing(boolean)
static jmethodID m_it_iterator, m_it_hasNext, m_it_next; // Iterable/Iterator
static jmethodID m_attackEntity;         // InteractionManager.attackEntity
static jmethodID m_clickSlot;            // InteractionManager.clickSlot
static jmethodID m_getInventory;         // PlayerEntity.getInventory()
static jmethodID m_getOffHandStack;      // LivingEntity.getOffHandStack()
static jmethodID m_deadOrDying;          // LivingEntity.isDeadOrDying()（1.21 为 isDead）
static jmethodID m_stackIsEmpty;         // ItemStack.isEmpty()
static jmethodID m_stackGetItem;         // ItemStack.getItem()
static jmethodID m_list_size, m_list_get; // java.util.List.size/get

// ---- 缓存的字段 ----
static jfieldID f_mc_player;             // MinecraftClient.player
static jfieldID f_mc_world;              // MinecraftClient.world
static jfieldID f_mc_options;            // MinecraftClient.options
static jfieldID f_mc_interactionManager; // MinecraftClient.interactionManager
static jfieldID f_mc_currentScreen;      // MinecraftClient.currentScreen（null = 未打开 GUI）
static jfieldID f_player_abilities;      // PlayerEntity.abilities
static jfieldID f_ab_allowFlying, f_ab_flying, f_ab_flySpeed, f_ab_walkSpeed;
static jfieldID f_ent_fallDistance;      // Entity.fallDistance（签名随版本 F/D）
static jfieldID f_go_gamma;              // GameOptions.gamma (SimpleOption)
static jfieldID f_inv_main;              // PlayerInventory.main (List<ItemStack>)
static jfieldID f_player_screenHandler;  // PlayerEntity.playerScreenHandler（syncId 恒为 0，用于判背包界面打开）

// ---- 静态对象（图腾物品 / SWAP 枚举）----
static jobject g_totemItem = nullptr;
static jobject g_swapAction = nullptr;

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
    RESOLVE_CLASS(c_InteractionManager, M.clsInteractionManager);
    RESOLVE_CLASS(c_PlayerInventory, M.clsPlayerInventory);
    RESOLVE_CLASS(c_ItemStack,       M.clsItemStack);
    RESOLVE_CLASS(c_Items,           M.clsItems);
    RESOLVE_CLASS(c_SlotActionType,  M.clsSlotActionType);
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
    {
        jclass list = find_class(env, "java/util/List");
        if (!list) return false;
        m_list_size = env->GetMethodID(list, "size", "()I");
        m_list_get  = env->GetMethodID(list, "get", "(I)Ljava/lang/Object;");
        if (!m_list_size || !m_list_get) return false;
    }
    // 杀戮光环/自动图腾相关（签名中的类名按版本动态拼接）
    {
        char msig[160];
        snprintf(msig, sizeof msig, "(L%s;L%s;)V", M.clsPlayerEntity, M.clsEntity);
        RESOLVE_METHOD(m_attackEntity, c_InteractionManager, M.mAttackEntity, msig);
        snprintf(msig, sizeof msig, "(IIIL%s;L%s;)V", M.clsSlotActionType, M.clsPlayerEntity);
        RESOLVE_METHOD(m_clickSlot, c_InteractionManager, M.mClickSlot, msig);
        snprintf(msig, sizeof msig, "()L%s;", M.clsPlayerInventory);
        RESOLVE_METHOD(m_getInventory, c_PlayerEntity, M.mGetInventory, msig);
        snprintf(msig, sizeof msig, "()L%s;", M.clsItemStack);
        RESOLVE_METHOD(m_getOffHandStack, c_PlayerEntity, M.mGetOffHandStack, msig);
        RESOLVE_METHOD(m_deadOrDying, c_LivingEntity, M.mDeadOrDying, "()Z");
        RESOLVE_METHOD(m_stackIsEmpty, c_ItemStack, M.mStackIsEmpty, "()Z");
        snprintf(msig, sizeof msig, "()L%s;", M.clsItem);
        RESOLVE_METHOD(m_stackGetItem, c_ItemStack, M.mStackGetItem, msig);
    }

    // 字段
    char sig[64];
    snprintf(sig, sizeof sig, "L%s;", M.clsClientPlayerEntity);
    RESOLVE_FIELD(f_mc_player, c_MinecraftClient, M.fMcPlayer, sig);
    snprintf(sig, sizeof sig, "L%s;", M.clsClientWorld);
    RESOLVE_FIELD(f_mc_world, c_MinecraftClient, M.fMcWorld, sig);
    snprintf(sig, sizeof sig, "L%s;", M.clsGameOptions);
    RESOLVE_FIELD(f_mc_options, c_MinecraftClient, M.fMcOptions, sig);
    snprintf(sig, sizeof sig, "L%s;", M.clsInteractionManager);
    RESOLVE_FIELD(f_mc_interactionManager, c_MinecraftClient, M.fMcInteractionManager, sig);
    // currentScreen 为 Screen 类型（可能为 null），描述符用当前版本 Screen 类
    {
        jclass scrCls = find_class(env, M.clsScreen);
        if (!scrCls) return false;
        snprintf(sig, sizeof sig, "L%s;", M.clsScreen);
        RESOLVE_FIELD(f_mc_currentScreen, c_MinecraftClient, M.fMcCurrentScreen, sig);
    }
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

    // PlayerInventory.main（声明类型为 DefaultedList，按版本动态）
    snprintf(sig, sizeof sig, "L%s;", M.clsDefaultedList);
    RESOLVE_FIELD(f_inv_main, c_PlayerInventory, M.fInvMain, sig);

    // PlayerEntity.playerScreenHandler（背包界面，syncId=0）
    snprintf(sig, sizeof sig, "L%s;", M.clsPlayerScreenHandler);
    RESOLVE_FIELD(f_player_screenHandler, c_PlayerEntity, M.fPlayerScreenHandler, sig);

    // 静态字段：Items.TOTEM_OF_UNDYING / SlotActionType.SWAP
    {
        snprintf(sig, sizeof sig, "L%s;", M.clsItem);
        jfieldID sf = env->GetStaticFieldID(c_Items, M.fItemsTotem, sig);
        if (!sf) return false;
        jobject totem = env->GetStaticObjectField(c_Items, sf);
        if (!totem) return false;
        g_totemItem = env->NewGlobalRef(totem);

        snprintf(sig, sizeof sig, "L%s;", M.clsSlotActionType);
        sf = env->GetStaticFieldID(c_SlotActionType, M.fSlotSwap, sig);
        if (!sf) return false;
        jobject swap = env->GetStaticObjectField(c_SlotActionType, sf);
        if (!swap) return false;
        g_swapAction = env->NewGlobalRef(swap);
    }

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

    // ---- 杀戮光环：每 10 帧攻击范围内最近的存活生物 ----
    static int kaTick = 0;
    if (g_cfg.killaura && (++kaTick % 10 == 0)) {
        jobject world = env->GetObjectField(mc, f_mc_world);
        jobject im    = env->GetObjectField(mc, f_mc_interactionManager);
        if (world && im) {
            double px = env->CallDoubleMethod(player, m_getX);
            double py = env->CallDoubleMethod(player, m_getY);
            double pz = env->CallDoubleMethod(player, m_getZ);
            double bestD2 = (double)g_cfg.auraRange * g_cfg.auraRange;
            jobject best = nullptr;

            jobject entities = env->CallObjectMethod(world, m_getEntities);
            jobject iter = entities ? env->CallObjectMethod(entities, m_it_iterator) : nullptr;
            while (iter && env->CallBooleanMethod(iter, m_it_hasNext)) {
                jobject ent = env->CallObjectMethod(iter, m_it_next);
                if (!ent) break;
                if (!env->IsSameObject(ent, player) &&
                    env->IsInstanceOf(ent, c_LivingEntity) &&
                    !env->CallBooleanMethod(ent, m_deadOrDying)) {
                    double dx = env->CallDoubleMethod(ent, m_getX) - px;
                    double dy = env->CallDoubleMethod(ent, m_getY) - py;
                    double dz = env->CallDoubleMethod(ent, m_getZ) - pz;
                    double d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 < bestD2) { // 平方比较，免开方
                        bestD2 = d2;
                        if (best) env->DeleteLocalRef(best);
                        best = ent;
                        continue;
                    }
                }
                env->DeleteLocalRef(ent);
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (iter) env->DeleteLocalRef(iter);
            if (entities) env->DeleteLocalRef(entities);

            if (best) {
                env->CallVoidMethod(im, m_attackEntity, player, best);
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(best);
            }
        }
        if (world) env->DeleteLocalRef(world);
        if (im) env->DeleteLocalRef(im);
    }

    // ---- 自动图腾：副手无图腾时从背包 SWAP 一个到副手（每 10 帧检查）----
    static int totemTick = 0;
    if (g_cfg.autoTotem && (++totemTick % 10 == 0)) {
        // 打开任意 GUI（背包/箱子等）时不动物品，避免干扰玩家操作
        jobject curScreen = env->GetObjectField(mc, f_mc_currentScreen);
        if (!curScreen && !env->ExceptionCheck()) {
            bool hasTotem = false;
            jobject offhand = env->CallObjectMethod(player, m_getOffHandStack);
            if (offhand) {
                if (!env->CallBooleanMethod(offhand, m_stackIsEmpty)) {
                    jobject item = env->CallObjectMethod(offhand, m_stackGetItem);
                    if (item) {
                        hasTotem = env->IsSameObject(item, g_totemItem);
                        env->DeleteLocalRef(item);
                    }
                }
                env->DeleteLocalRef(offhand);
            }
            if (!hasTotem) {
                jobject inv = env->CallObjectMethod(player, m_getInventory);
                jobject mainList = inv ? env->GetObjectField(inv, f_inv_main) : nullptr;
                if (mainList) {
                    int n = env->CallIntMethod(mainList, m_list_size);
                    int found = -1;
                    for (int i = 0; i < n; ++i) {
                        jobject st = env->CallObjectMethod(mainList, m_list_get, i);
                        if (!st) continue;
                        bool isTotem = false;
                        if (!env->CallBooleanMethod(st, m_stackIsEmpty)) {
                            jobject item = env->CallObjectMethod(st, m_stackGetItem);
                            if (item) {
                                isTotem = env->IsSameObject(item, g_totemItem);
                                env->DeleteLocalRef(item);
                            }
                        }
                        env->DeleteLocalRef(st);
                        if (isTotem) { found = i; break; }
                    }
                    if (found >= 0) {
                        jobject im = env->GetObjectField(mc, f_mc_interactionManager);
                        if (im) {
                            // PlayerInventory.main 索引 -> 玩家背包界面槽位：
                            // 0~8 快捷栏对应槽位 36~44，9~35 背包格槽位号相同
                            int slot = (found < 9) ? (36 + found) : found;
                            // SWAP + button=40：与副手交换
                            env->CallVoidMethod(im, m_clickSlot, 0, slot, 40, g_swapAction, player);
                            if (env->ExceptionCheck()) env->ExceptionClear();
                            env->DeleteLocalRef(im);
                        }
                    }
                    env->DeleteLocalRef(mainList);
                }
                if (inv) env->DeleteLocalRef(inv);
            }
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (curScreen) env->DeleteLocalRef(curScreen);
    }

    env->DeleteLocalRef(player);
    env->DeleteLocalRef(mc);
}
