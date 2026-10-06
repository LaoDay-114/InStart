// NoFall：拦截 ClientCommonNetworkHandler.sendPacket，把移动包的 onGround
// 改成 true（参考 Meteor 的 Packet 模式）。改字段的做法无效——服务端同 tick
// 内会覆盖 fallDistance，客户端 onGround 在发包前会被 move() 覆盖。
#include <jni.h>
#include <jvmti.h>
#include <windows.h>
#include <cstdio>

#include "nofall.h"
#include "config.h"
#include "jni/mappings.h"

static jvmtiEnv* g_jvmti = nullptr;
static bool      g_inited = false;

static jclass    g_mcCls = nullptr;
static jclass    g_movePacketCls = nullptr;
static jmethodID m_getInstance = nullptr;
static jfieldID  f_mcPlayer = nullptr;
static jmethodID m_getVelocity = nullptr;
static jmethodID m_isFallFlying = nullptr;
static jfieldID  f_pktOnGround = nullptr;
static jfieldID  f_vecY = nullptr;

// Knot 环境下用上下文类加载器找类
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

static void JNICALL on_breakpoint(jvmtiEnv*, JNIEnv* jni,
                                  jthread thread, jmethodID, jlocation) {
    if (jni->ExceptionCheck()) jni->ExceptionClear();
    if (!g_cfg.noFall) return;

    jobject packet = nullptr;
    // depth 0 是 sendPacket，slot 1 是 Packet 参数
    if (g_jvmti->GetLocalObject(thread, 0, 1, &packet) != JVMTI_ERROR_NONE || !packet)
        return;

    if (jni->IsInstanceOf(packet, g_movePacketCls)) {
        bool flip = false;
        jobject mc = jni->CallStaticObjectMethod(g_mcCls, m_getInstance);
        jobject player = mc ? jni->GetObjectField(mc, f_mcPlayer) : nullptr;

        if (player) {
            if (g_cfg.fly) {
                flip = true;
            } else if (!jni->CallBooleanMethod(player, m_isFallFlying)) {
                jobject vel = jni->CallObjectMethod(player, m_getVelocity);
                if (vel) {
                    if (jni->GetDoubleField(vel, f_vecY) <= -0.5) flip = true;
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

    m_getInstance = env->GetStaticMethodID(g_mcCls, M.mGetInstance,
                                           "()Lnet/minecraft/class_310;");
    if (!m_getInstance) return fail();
    {
        char sig[128];
        snprintf(sig, sizeof sig, "L%s;", M.clsClientPlayerEntity);
        f_mcPlayer = env->GetFieldID(g_mcCls, M.fMcPlayer, sig);
        if (!f_mcPlayer) return fail();
    }

    f_pktOnGround = env->GetFieldID(g_movePacketCls, M.fPktOnGround, "Z");
    if (!f_pktOnGround) return fail();

    m_getVelocity = env->GetMethodID(enG, M.mGetVelocity,
                                     "()Lnet/minecraft/class_243;");
    if (!m_getVelocity) return fail();
    f_vecY = env->GetFieldID(v3G, M.fVecY, "D");
    if (!f_vecY) return fail();

    m_isFallFlying = env->GetMethodID(leG, M.mIsFallFlying, "()Z");
    if (!m_isFallFlying) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        m_isFallFlying = env->GetMethodID(enG, M.mIsFallFlying, "()Z");
        if (!m_isFallFlying) return fail();
    }

    jmethodID sendPacket = env->GetMethodID(
        nhG, M.mSendPacket, "(Lnet/minecraft/class_2596;)V");
    if (!sendPacket) return fail();

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
