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
              c_Items, c_SlotActionType,
              c_MinecraftServer, c_PlayerManager, c_AttributeContainer,
              c_AttributeInstance, c_EntityAttributes, c_MobEntity;

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
// 内置服务端 / 属性
static jmethodID m_getServer;            // MinecraftClient.getServer()
static jmethodID m_getPlayerManager;     // MinecraftServer.getPlayerManager()
static jmethodID m_getPlayerList;        // PlayerManager.getPlayerList()
static jmethodID m_getAttributes;        // LivingEntity.getAttributes()
static jmethodID m_attrGet;              // AttributeContainer.get/getCustomInstance
static jmethodID m_setBaseValue;         // AttributeInstance.setBaseValue(double)
static jmethodID m_isSprinting;          // Entity.isSprinting()

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
static jfieldID f_onGround;              // Entity.onGround（FakeGround 用）

// ---- 静态对象（图腾物品 / SWAP 枚举 / 移动速度属性）----
static jobject g_totemItem = nullptr;
static jobject g_swapAction = nullptr;
static jobject g_moveSpeedAttr = nullptr; // EntityAttributes.MOVEMENT_SPEED（RegistryEntry）

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

// 按版本号查映射表；找到返回 true 并设置 g_map。
// 无精确匹配时回退到映射表中不高于该版本的最新一条（JNI 解析失败会自动停用，不会崩溃）
static bool pick_mappings(const char* version) {
    for (int i = 0; i < g_mcVerMapsCount; ++i) {
        if (strcmp(g_mcVerMaps[i].version, version) == 0) {
            g_map = &g_mcVerMaps[i];
            return true;
        }
    }
    if (g_mcVerMapsCount > 0) { // 回退：最新映射
        g_map = &g_mcVerMaps[g_mcVerMapsCount - 1];
        return true;
    }
    return false;
}

// 从版本串提取基础版本号（前缀 \d+(\.\d+)* 部分）。
// Fabric Loader 会把 getGameVersion() 改写成 "1.21.11-Fabric_0.19.2" 之类的
// 加载器后缀形式，须剥离后再查表；后缀只允许 Fabric 标识 + 加载器版本号，
// 避免把 "1.21.11-pre1-Fabric_x" 这类预发布误判成正式版。
static bool extract_base_version(const char* ver, char* out, size_t outsz) {
    if (!ver || ver[0] < '0' || ver[0] > '9') return false; // 快照 24w14a 不以纯版本开头
    size_t n = 0;
    while (ver[n] && ((ver[n] >= '0' && ver[n] <= '9') || ver[n] == '.')) ++n;
    while (n > 0 && ver[n - 1] == '.') --n; // 防御性：去掉末尾孤立 '.'
    if (n == 0 || n >= outsz) return false;

    const char* rest = ver + n;
    if (*rest) { // 有后缀：必须整体是 "-Fabric_0.19.2" 形式
        while (*rest == '-' || *rest == '_' || *rest == ' ') ++rest;
        // 大小写不敏感匹配 "fabric"
        char buf[8] = {};
        for (int i = 0; i < 6; ++i) {
            char c = rest[i];
            if (!c) return false;
            buf[i] = (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
        }
        if (strcmp(buf, "fabric") != 0) return false; // pre/rc 等一律拒绝
        rest += 6;
        // 剩余只能由分隔符/数字/'.'组成（加载器版本号）
        for (const char* p = rest; *p; ++p) {
            char c = *p;
            if (!(c == '-' || c == '_' || c == ' ' ||
                  (c >= '0' && c <= '9') || c == '.')) return false;
        }
    }
    memcpy(out, ver, n);
    out[n] = '\0';
    return true;
}

#define RESOLVE_CLASS(var, name) do { \
    jclass _c = find_class(env, name); \
    if (!_c) return false; \
    var = (jclass)env->NewGlobalRef(_c); \
} while (0)

#define RESOLVE_FIELD(var, cls, name, sig) do { \
    var = env->GetFieldID(cls, name, sig); \
    if (!var) { env->ExceptionClear(); return false; } \
} while (0)

#define RESOLVE_METHOD(var, cls, name, sig) do { \
    var = env->GetMethodID(cls, name, sig); \
    if (!var) { env->ExceptionClear(); return false; } \
} while (0)

bool mc_jni_init() {
    if (g_env) return true;
    // 失败路径统一出口：JNI 查询失败会留下 pending 异常，若不清理，
    // 异常会在回到 Java 边界时抛出（NoSuchMethodError 等）并导致游戏崩溃
    #define INIT_FAIL() do { if (env->ExceptionCheck()) env->ExceptionClear(); return false; } while (0)

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

    // 4. 剥离 Fabric 加载器后缀后按版本选映射；无法剥离时直接用原串查表，
    //    无精确匹配则回退最新映射（解析失败会自动停用功能，不做版本阻断）
    char base[64] = {};
    const char* key = extract_base_version(ver, base, sizeof base) ? base : ver;
    if (!pick_mappings(key)) return false;

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
    RESOLVE_CLASS(c_MinecraftServer, M.clsMinecraftServer);
    RESOLVE_CLASS(c_PlayerManager,   M.clsPlayerManager);
    RESOLVE_CLASS(c_AttributeContainer, M.clsAttributeContainer);
    RESOLVE_CLASS(c_AttributeInstance,  M.clsAttributeInstance);
    RESOLVE_CLASS(c_EntityAttributes,   M.clsEntityAttributes);
    RESOLVE_CLASS(c_MobEntity,          M.clsMobEntity);
    jclass dbl = find_class(env, "java/lang/Double");
    if (!dbl) return false;
    c_Double = (jclass)env->NewGlobalRef(dbl);

    // 方法
    m_getInstance = env->GetStaticMethodID(c_MinecraftClient, M.mGetInstance,
                                           "()Lnet/minecraft/class_310;");
    if (!m_getInstance) INIT_FAIL();
    RESOLVE_METHOD(m_sendAbilitiesUpdate, c_PlayerEntity, M.mSendAbilitiesUpdate, "()V");
    RESOLVE_METHOD(m_getX, c_PlayerEntity, M.mGetX, "()D");
    RESOLVE_METHOD(m_getY, c_PlayerEntity, M.mGetY, "()D");
    RESOLVE_METHOD(m_getZ, c_PlayerEntity, M.mGetZ, "()D");
    RESOLVE_METHOD(m_setValue, c_SimpleOption, M.mSetValue, "(Ljava/lang/Object;)V");
    m_dblValueOf = env->GetStaticMethodID(c_Double, "valueOf", "(D)Ljava/lang/Double;");
    if (!m_dblValueOf) INIT_FAIL();
    RESOLVE_METHOD(m_getEntities, c_ClientWorld, M.mGetEntities, "()Ljava/lang/Iterable;");
    RESOLVE_METHOD(m_setGlowing, c_PlayerEntity, M.mSetGlowing, "(Z)V");
    {
        jclass iterable = find_class(env, "java/lang/Iterable");
        jclass iterator = find_class(env, "java/util/Iterator");
        if (!iterable || !iterator) INIT_FAIL();
        m_it_iterator = env->GetMethodID(iterable, "iterator", "()Ljava/util/Iterator;");
        m_it_hasNext  = env->GetMethodID(iterator, "hasNext", "()Z");
        m_it_next     = env->GetMethodID(iterator, "next", "()Ljava/lang/Object;");
        if (!m_it_iterator || !m_it_hasNext || !m_it_next) INIT_FAIL();
    }
    {
        jclass list = find_class(env, "java/util/List");
        if (!list) INIT_FAIL();
        m_list_size = env->GetMethodID(list, "size", "()I");
        m_list_get  = env->GetMethodID(list, "get", "(I)Ljava/lang/Object;");
        if (!m_list_size || !m_list_get) INIT_FAIL();
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

    // 内置服务端链路 / 属性（签名按版本动态拼接）
    {
        char msig[192];
        snprintf(msig, sizeof msig, "()L%s;", M.clsMinecraftServer);
        RESOLVE_METHOD(m_getServer, c_MinecraftClient, M.mGetServer, msig);
        snprintf(msig, sizeof msig, "()L%s;", M.clsPlayerManager);
        RESOLVE_METHOD(m_getPlayerManager, c_MinecraftServer, M.mGetPlayerManager, msig);
        RESOLVE_METHOD(m_getPlayerList, c_PlayerManager, M.mGetPlayerList,
                       "()Ljava/util/List;");
        snprintf(msig, sizeof msig, "()L%s;", M.clsAttributeContainer);
        RESOLVE_METHOD(m_getAttributes, c_LivingEntity, M.mGetAttributes, msig);
        snprintf(msig, sizeof msig, "(L%s;)L%s;", M.clsRegistryEntry, M.clsAttributeInstance);
        RESOLVE_METHOD(m_attrGet, c_AttributeContainer, M.mAttrGet, msig);
        RESOLVE_METHOD(m_setBaseValue, c_AttributeInstance, M.mSetBaseValue, "(D)V");
        RESOLVE_METHOD(m_isSprinting, c_PlayerEntity, M.mIsSprinting, "()Z");
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
    RESOLVE_FIELD(f_onGround, c_PlayerEntity, M.fOnGround, "Z");
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
        if (!sf) INIT_FAIL();
        jobject totem = env->GetStaticObjectField(c_Items, sf);
        if (!totem) INIT_FAIL();
        g_totemItem = env->NewGlobalRef(totem);

        snprintf(sig, sizeof sig, "L%s;", M.clsSlotActionType);
        sf = env->GetStaticFieldID(c_SlotActionType, M.fSlotSwap, sig);
        if (!sf) INIT_FAIL();
        jobject swap = env->GetStaticObjectField(c_SlotActionType, sf);
        if (!swap) INIT_FAIL();
        g_swapAction = env->NewGlobalRef(swap);
    }

    // EntityAttributes.MOVEMENT_SPEED（旧名 GENERIC_MOVEMENT_SPEED），类型为 RegistryEntry
    {
        char msig[128];
        snprintf(msig, sizeof msig, "L%s;", M.clsRegistryEntry);
        jfieldID sf = env->GetStaticFieldID(c_EntityAttributes, M.fMovementSpeed, msig);
        if (!sf) INIT_FAIL();
        jobject attr = env->GetStaticObjectField(c_EntityAttributes, sf);
        if (!attr) INIT_FAIL();
        g_moveSpeedAttr = env->NewGlobalRef(attr);
    }

    g_env = env;
    g_state.jniReady = true;
    return true;
}

// 取内置服务端玩家对象（单机世界只有一个）；返回局部引用，调用方负责释放
static jobject get_server_player(JNIEnv* env, jobject mc) {
    jobject server = env->CallObjectMethod(mc, m_getServer);
    if (env->ExceptionCheck() || !server) { env->ExceptionClear(); return nullptr; }
    jobject mgr = env->CallObjectMethod(server, m_getPlayerManager);
    env->DeleteLocalRef(server);
    if (env->ExceptionCheck() || !mgr) { env->ExceptionClear(); return nullptr; }
    jobject list = env->CallObjectMethod(mgr, m_getPlayerList);
    env->DeleteLocalRef(mgr);
    if (env->ExceptionCheck() || !list) { env->ExceptionClear(); return nullptr; }
    jobject sp = nullptr;
    if (env->CallIntMethod(list, m_list_size) > 0)
        sp = env->CallObjectMethod(list, m_list_get, 0);
    env->DeleteLocalRef(list);
    if (env->ExceptionCheck()) env->ExceptionClear();
    return sp;
}

// 直接设置实体的移动速度属性基础值（绕过 abilities 同步的不确定性）
static void set_move_speed_attr(JNIEnv* env, jobject ent, double base) {
    jobject cont = env->CallObjectMethod(ent, m_getAttributes);
    if (!cont || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    jobject inst = env->CallObjectMethod(cont, m_attrGet, g_moveSpeedAttr);
    if (inst && !env->ExceptionCheck()) {
        env->CallVoidMethod(inst, m_setBaseValue, base);
        env->DeleteLocalRef(inst);
    }
    env->ExceptionClear();
    env->DeleteLocalRef(cont);
}

void mc_apply_features() {
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

        // ---- 地面加速：walkSpeed 字段（游戏自身 updateAttributes 会消费它）----
        if (g_cfg.speed) {
            env->SetFloatField(abilities, f_ab_walkSpeed, 0.1f * g_cfg.speedMult);
            if (!prevSpeed || prevSpMult != g_cfg.speedMult) needSync = true;
        } else if (prevSpeed) {
            env->SetFloatField(abilities, f_ab_walkSpeed, 0.1f);
            needSync = true;
        }

        // 仅在状态变化时同步能力包（避免每帧发包）
        if (needSync) env->CallVoidMethod(player, m_sendAbilitiesUpdate);
        if (env->ExceptionCheck()) env->ExceptionClear(); // 同步异常不允许外溢
        env->DeleteLocalRef(abilities);
    }
    prevFly = g_cfg.fly;  prevFlySp = g_cfg.flySpeed;
    prevSpeed = g_cfg.speed; prevSpMult = g_cfg.speedMult;

    // ---- 地面加速：直接写移动速度属性（客户端+内置服务端玩家）----
    //   字段同步是间接路径，直接写属性确保最终消费点被命中；保留冲刺 1.3 倍率
    if (g_cfg.speed || prevSpeed) {
        float ws = g_cfg.speed ? 0.1f * g_cfg.speedMult : 0.1f;
        bool sprint = env->CallBooleanMethod(player, m_isSprinting) == JNI_TRUE;
        if (env->ExceptionCheck()) env->ExceptionClear();
        set_move_speed_attr(env, player, (double)ws * (sprint ? 1.3 : 1.0));

        jobject srvp = get_server_player(env, mc);
        if (srvp) {
            bool sSprint = env->CallBooleanMethod(srvp, m_isSprinting) == JNI_TRUE;
            if (env->ExceptionCheck()) env->ExceptionClear();
            set_move_speed_attr(env, srvp, (double)ws * (sSprint ? 1.3 : 1.0));
            env->DeleteLocalRef(srvp);
        }
    }

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

    // ---- ESP：实体发光透视（穿墙可见），每帧遍历 ----
    //   开时强制 glowing=true，关时强制 false（同时负责恢复），
    //   段内异常立即清除，避免后续实体调用被挂起异常静默掐断
    {
        jobject world = env->GetObjectField(mc, f_mc_world);
        if (world && !env->ExceptionCheck()) {
            jobject entities = env->CallObjectMethod(world, m_getEntities);
            jobject iter = entities ? env->CallObjectMethod(entities, m_it_iterator) : nullptr;
            while (iter && env->CallBooleanMethod(iter, m_it_hasNext)) {
                jobject ent = env->CallObjectMethod(iter, m_it_next);
                if (!ent) break;
                if (!env->IsSameObject(ent, player)) {
                    if (g_cfg.esp) {
                        bool match = !g_cfg.espMobsOnly ||
                                     env->IsInstanceOf(ent, c_LivingEntity);
                        if (match) env->CallVoidMethod(ent, m_setGlowing, JNI_TRUE);
                    } else {
                        env->CallVoidMethod(ent, m_setGlowing, JNI_FALSE);
                    }
                }
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(ent);
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (iter) env->DeleteLocalRef(iter);
            if (entities) env->DeleteLocalRef(entities);
            env->DeleteLocalRef(world);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    // ---- 无摔落：NoGround=清零两端 fallDistance；FakeGround=额外伪装 onGround=true ----
    //   单机伤害由内置服务端用服务端玩家判定，只清客户端无效
    if (g_cfg.noFall) {
        jobject srvp = get_server_player(env, mc);
        if (g_map->fallDistType == 'D') {
            env->SetDoubleField(player, f_ent_fallDistance, 0.0);
            if (srvp) env->SetDoubleField(srvp, f_ent_fallDistance, 0.0);
        } else {
            env->SetFloatField(player, f_ent_fallDistance, 0.0f);
            if (srvp) env->SetFloatField(srvp, f_ent_fallDistance, 0.0f);
        }
        if (g_cfg.noFallMode == 1) { // FakeGround
            env->SetBooleanField(player, f_onGround, JNI_TRUE);
            if (srvp) env->SetBooleanField(srvp, f_onGround, JNI_TRUE);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (srvp) env->DeleteLocalRef(srvp);
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
                if (env->ExceptionCheck()) env->ExceptionClear();
                // 过滤：自己 / 排除玩家 / 排除生物 / 非 LivingEntity / 已死亡
                bool ok = !env->IsSameObject(ent, player)
                       && !(g_cfg.kaExcludePlayers && env->IsInstanceOf(ent, c_PlayerEntity))
                       && !(g_cfg.kaExcludeMobs && env->IsInstanceOf(ent, c_MobEntity))
                       && env->IsInstanceOf(ent, c_LivingEntity)
                       && !env->CallBooleanMethod(ent, m_deadOrDying);
                if (env->ExceptionCheck()) env->ExceptionClear();
                if (ok) {
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
