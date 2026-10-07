#include <jni.h>
#include <windows.h>
#include <cstring>
#include <cstdio>

#include "mappings.h"
#include "../config.h"
#include "../nofall.h"

static JavaVM*  g_vm  = nullptr;
static JNIEnv*  g_env = nullptr;
static const McVerMap* g_map = nullptr;

static jclass c_MinecraftClient, c_GameOptions, c_SimpleOption,
              c_PlayerAbilities, c_PlayerEntity, c_Double,
              c_ClientWorld, c_LivingEntity,
              c_InteractionManager, c_PlayerInventory, c_ItemStack,
              c_Items, c_SlotActionType,
              c_MinecraftServer, c_PlayerManager, c_AttributeContainer,
              c_AttributeInstance, c_EntityAttributes, c_MobEntity;

static jmethodID m_getInstance;
static jmethodID m_sendAbilitiesUpdate;
static jmethodID m_getX, m_getY, m_getZ;
static jmethodID m_setValue;
static jmethodID m_dblValueOf;
static jmethodID m_getEntities;
static jmethodID m_setGlowing;
static jmethodID m_it_iterator, m_it_hasNext, m_it_next;
static jmethodID m_attackEntity;
static jmethodID m_clickSlot;
static jmethodID m_getInventory;
static jmethodID m_getOffHandStack;
static jmethodID m_deadOrDying;
static jmethodID m_stackIsEmpty;
static jmethodID m_stackGetItem;
static jmethodID m_list_size, m_list_get;
static jmethodID m_getServer;
static jmethodID m_getPlayerManager;
static jmethodID m_getPlayerList;
static jmethodID m_getAttributes;
static jmethodID m_attrGet;
static jmethodID m_setBaseValue;
static jmethodID m_isSprinting;

static jfieldID f_mc_player;
static jfieldID f_mc_world;
static jfieldID f_mc_options;
static jfieldID f_mc_interactionManager;
static jfieldID f_mc_currentScreen;
static jfieldID f_player_abilities;
static jfieldID f_ab_allowFlying, f_ab_flying, f_ab_flySpeed, f_ab_walkSpeed;
static jfieldID f_go_gamma;
static jfieldID f_inv_main;

static jobject g_totemItem = nullptr;
static jobject g_swapAction = nullptr;
static jobject g_moveSpeedAttr = nullptr;

static bool  prevFly = false, prevSpeed = false, prevFb = false;
static float prevFlySp = -1.f, prevSpMult = -1.f;

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

static jclass find_class_any(JNIEnv* env, const char* const* variants) {
    for (int i = 0; variants[i]; ++i) {
        jclass c = find_class(env, variants[i]);
        if (c) return c;
    }
    return nullptr;
}

static jmethodID get_static_mid_any(JNIEnv* env, jclass cls,
                                    const char* const* names, const char* sig) {
    for (int i = 0; names[i]; ++i) {
        jmethodID m = env->GetStaticMethodID(cls, names[i], sig);
        if (m) return m;
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    return nullptr;
}

static jmethodID get_mid_any(JNIEnv* env, jclass cls,
                             const char* const* names, const char* sig) {
    for (int i = 0; names[i]; ++i) {
        jmethodID m = env->GetMethodID(cls, names[i], sig);
        if (m) return m;
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    return nullptr;
}

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

static bool pick_mappings(const char* version) {
    for (int i = 0; i < g_mcVerMapsCount; ++i) {
        if (strcmp(g_mcVerMaps[i].version, version) == 0) {
            g_map = &g_mcVerMaps[i];
            return true;
        }
    }
    if (g_mcVerMapsCount > 0) {
        g_map = &g_mcVerMaps[g_mcVerMapsCount - 1];
        return true;
    }
    return false;
}

static bool extract_base_version(const char* ver, char* out, size_t outsz) {
    if (!ver || ver[0] < '0' || ver[0] > '9') return false;
    size_t n = 0;
    while (ver[n] && ((ver[n] >= '0' && ver[n] <= '9') || ver[n] == '.')) ++n;
    while (n > 0 && ver[n - 1] == '.') --n;
    if (n == 0 || n >= outsz) return false;

    const char* rest = ver + n;
    if (*rest) {
        while (*rest == '-' || *rest == '_' || *rest == ' ') ++rest;
        char buf[8] = {};
        for (int i = 0; i < 6; ++i) {
            char c = rest[i];
            if (!c) return false;
            buf[i] = (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
        }
        if (strcmp(buf, "fabric") != 0) return false;
        rest += 6;
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
    #define INIT_FAIL() do { if (env->ExceptionCheck()) env->ExceptionClear(); return false; } while (0)

    if (!g_vm) {
        using GetCreatedVMs_t = jint (*)(JavaVM**, jsize, jsize*);
        auto fn = (GetCreatedVMs_t)GetProcAddress(GetModuleHandleA("jvm.dll"), "JNI_GetCreatedJavaVMs");
        if (!fn) return false;
        JavaVM* vms[4] = {};
        jsize n = 0;
        if (fn(vms, 4, &n) != JNI_OK || n == 0) return false;
        g_vm = vms[0];
    }

    JNIEnv* env = nullptr;
    if (g_vm->GetEnv((void**)&env, JNI_VERSION_1_8) == JNI_EDETACHED)
        g_vm->AttachCurrentThread((void**)&env, nullptr);
    if (!env) return false;

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

    char base[64] = {};
    const char* key = extract_base_version(ver, base, sizeof base) ? base : ver;
    if (!pick_mappings(key)) return false;

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

    char sig[64];
    snprintf(sig, sizeof sig, "L%s;", M.clsClientPlayerEntity);
    RESOLVE_FIELD(f_mc_player, c_MinecraftClient, M.fMcPlayer, sig);
    snprintf(sig, sizeof sig, "L%s;", M.clsClientWorld);
    RESOLVE_FIELD(f_mc_world, c_MinecraftClient, M.fMcWorld, sig);
    snprintf(sig, sizeof sig, "L%s;", M.clsGameOptions);
    RESOLVE_FIELD(f_mc_options, c_MinecraftClient, M.fMcOptions, sig);
    snprintf(sig, sizeof sig, "L%s;", M.clsInteractionManager);
    RESOLVE_FIELD(f_mc_interactionManager, c_MinecraftClient, M.fMcInteractionManager, sig);
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
    snprintf(sig, sizeof sig, "L%s;", M.clsSimpleOption);
    RESOLVE_FIELD(f_go_gamma, c_GameOptions, M.fGamma, sig);

    snprintf(sig, sizeof sig, "L%s;", M.clsDefaultedList);
    RESOLVE_FIELD(f_inv_main, c_PlayerInventory, M.fInvMain, sig);

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

    nofall_init(g_vm, *g_map, env);
    return true;
}

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

        if (g_cfg.speed) {
            env->SetFloatField(abilities, f_ab_walkSpeed, 0.1f * g_cfg.speedMult);
            if (!prevSpeed || prevSpMult != g_cfg.speedMult) needSync = true;
        } else if (prevSpeed) {
            env->SetFloatField(abilities, f_ab_walkSpeed, 0.1f);
            needSync = true;
        }

        if (needSync) env->CallVoidMethod(player, m_sendAbilitiesUpdate);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(abilities);
    }
    prevFly = g_cfg.fly;  prevFlySp = g_cfg.flySpeed;
    prevSpeed = g_cfg.speed; prevSpMult = g_cfg.speedMult;

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

    if (g_cfg.hud) {
        g_state.px = env->CallDoubleMethod(player, m_getX);
        g_state.py = env->CallDoubleMethod(player, m_getY);
        g_state.pz = env->CallDoubleMethod(player, m_getZ);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

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
                    if (d2 < bestD2) {
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

    static int totemTick = 0;
    if (g_cfg.autoTotem && (++totemTick % 10 == 0)) {
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
                            int slot = (found < 9) ? (36 + found) : found;
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
