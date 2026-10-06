#pragma once
#include <jni.h>

// 在 JNI 解析完成后调用一次：挂 JVMTI 断点，拦截移动包并改 onGround
// （参考 MeteorClient NoFall 的 Packet 模式）。失败不影响其它功能。
bool nofall_init(JavaVM* vm, const struct McVerMap& M, JNIEnv* env);
