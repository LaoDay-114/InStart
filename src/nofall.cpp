// ============================================================
// InStart 无摔落（参考 MeteorClient NoFall Packet 模式）
//
// 旧方案直接清零实体 fallDistance / 伪装 onGround 字段，但：
//   1. 单机伤害由内置服务端在「收到移动包」时判定；
//   2. 服务端在同一 tick 内先累加 fallDistance 再判定，外部线程清零必被覆盖；
//   3. 客户端 onGround 字段会被同 tick 的 move() 在发包前覆盖。
//
// Meteor 的做法：在 sendPacket 入口拦截 PlayerMoveC2SPacket，当玩家
// 下落速度 y <= -0.5（且不在滑翔）时把包内 onGround 改为 true，服务端
// 据此不产生摔落伤害。我们用 JVMTI 在方法入口下断点命中同一时刻。
// ============================================================
#include <jni.h>
#include <jvmti.h>
#include <windows.h>
#include <cstdio>

#include "nofall.h"
#include "config.h"
#include "jni/mappings.h"

static jvmtiEnv* g_jvmti = nullptr;
static bool      g_inited = false;

// 缓存的全局引用
static jclass    g_mcCls = nullptr;       // MinecraftClient
static jclass    g_movePacketCls = nullptr; // PlayerMoveC2SPacket
static jmethodID m_getInstance = nullptr;
static jfieldID  f_mcPlayer = nullptr;
static jmethodID m_getVelocity = nullptr;  // Entity.getVelocity() -> Vec3d
static jmethodID m_isFallFlying = nullptr; // isFallFlying/isGliding
static jfieldID  f_pktOnGround = nullptr;  // PlayerMoveC2SPacket.onGround
static jfieldID  f_vecY = nullptr;         // Vec3d.y

// Fabric/Knot 下 FindClass 退化为上下文类加载器 Class.forName（与 mc.cpp 同）
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

// 断点回调：线程停在 ClientCommonNetworkHandler.sendPacket(Packet) 入口
static void JNICALL on_breakpoint(jvmtiEnv* /*jvmti*/, JNIEnv* jni,
                                  jthread thread, jmethodID /*method*/, jlocation /*loc*/) {
    if (jni->ExceptionCheck()) jni->ExceptionClear();
    if (!g_cfg.noFall) return; // 功能未启用：放行（断点保留，开销可忽略）

    jobject packet = nullptr;
    // depth 0 = sendPacket 帧；slot 0 = this，slot 1 = Packet 参数（单个对象参数）
    if (g_jvmti->GetLocalObject(thread, 0, 1, &packet) != JVMTI_ERROR_NONE || !packet)
        return;

    if (jni->IsInstanceOf(packet, g_movePacketCls)) {
        bool flip = false;
        jobject mc = jni->CallStaticObjectMethod(g_mcCls, m_getInstance);
        jobject player = mc ? jni->GetObjectField(mc, f_mcPlayer) : nullptr;

        if (player) {
            if (g_cfg.fly) {
                flip = true; // 飞行中：无条件翻为 onGround（同 Meteor）
            } else if (!jni->CallBooleanMethod(player, m_isFallFlying)) {
                jobject vel = jni->CallObjectMethod(player, m_getVelocity);
                if (vel) {
                    jdouble vy = jni->GetDoubleField(vel, f_vecY);
                    if (vy <= -0.5) flip = true; // 下落速度足够大才改，避免影响正常走位/跳跃
                    jni->DeleteLocalRef(vel);
                }
            }
        }
        if (flip) jni->SetBooleanField(packet, f_pktOnGround, JNI_TRUE);

        if (player) jni->DeleteLocalRef(player);
        if (mc) jni->DeleteLocalRef(mc);
    }

    if (jni->ExceptionCheck()) jni->ExceptionClear();
    jni->DeleteLocalRef(packet);
}

bool nofall_init(JavaVM* vm, const McVerMap& M, JNIEnv* env) {
    if (g_inited) return true;

    if (vm->GetEnv((void**)&g_jvmti, JVMTI_VERSION_1_2) != JNI_OK || !g_jvmti)
        return false;

    // ---- 类（全局引用）----
    jclass mc = find_class(env, M.clsMinecraftClient);
    jclass mp = find_class(env, M.clsMovePacket);
    jclass nh = find_class(env, M.clsCommonNetHandler);
    jclass en = find_class(env, M.clsEntity);
    jclass le = find_class(env, M.clsLivingEntity);
    jclass v3 = find_class(env, "net/minecraft/class_243");
    if (!mc || !mp || !nh || !en || !le || !v3) return false;
    g_mcCls         = (jclass)env->NewGlobalRef(mc);
    g_movePacketCls = (jclass)env->NewGlobalRef(mp);
    jclass nhG = (jclass)env->NewGlobalRef(nh);
    jclass enG = (jclass)env->NewGlobalRef(en);
    jclass leG = (jclass)env->NewGlobalRef(le);
    jclass v3G = (jclass)env->NewGlobalRef(v3);

    auto fail = [&]() -> bool {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return false;
    };

    // ---- MinecraftClient.getInstance() / player ----
    m_getInstance = env->GetStaticMethodID(g_mcCls, M.mGetInstance,
                                           "()Lnet/minecraft/class_310;");
    if (!m_getInstance) return fail();
    {
        char sig[128];
        snprintf(sig, sizeof sig, "L%s;", M.clsClientPlayerEntity);
        f_mcPlayer = env->GetFieldID(g_mcCls, M.fMcPlayer, sig);
        if (!f_mcPlayer) return fail();
    }

    // ---- 移动包 onGround ----
    f_pktOnGround = env->GetFieldID(g_movePacketCls, M.fPktOnGround, "Z");
    if (!f_pktOnGround) return fail();

    // ---- Entity.getVelocity() -> Vec3d / Vec3d.y ----
    m_getVelocity = env->GetMethodID(enG, M.mGetVelocity,
                                     "()Lnet/minecraft/class_243;");
    if (!m_getVelocity) return fail();
    f_vecY = env->GetFieldID(v3G, M.fVecY, "D");
    if (!f_vecY) return fail();

    // ---- isFallFlying/isGliding：声明位置跨版本漂移，LivingEntity 优先 ----
    m_isFallFlying = env->GetMethodID(leG, M.mIsFallFlying, "()Z");
    if (!m_isFallFlying) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        m_isFallFlying = env->GetMethodID(enG, M.mIsFallFlying, "()Z");
        if (!m_isFallFlying) return fail();
    }

    // ---- sendPacket 方法：断点目标 ----
    jmethodID sendPacket = env->GetMethodID(
        nhG, M.mSendPacket, "(Lnet/minecraft/class_2596;)V");
    if (!sendPacket) return fail();

    // ---- JVMTI 能力 / 回调 / 断点 ----
    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof caps);
    caps.can_generate_breakpoint_events = 1;
    if (g_jvmti->AddCapabilities(&caps) != JVMTI_ERROR_NONE) return false;

    jvmtiEventCallbacks cb;
    memset(&cb, 0, sizeof cb);
    cb.Breakpoint = &on_breakpoint;
    if (g_jvmti->SetEventCallbacks(&cb, sizeof cb) != JVMTI_ERROR_NONE) return false;
    if (g_jvmti->SetBreakpoint(sendPacket, 0) != JVMTI_ERROR_NONE) return false;
    if (g_jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_BREAKPOINT, nullptr)
        != JVMTI_ERROR_NONE) return false;

    g_inited = true;
    return true;
}
