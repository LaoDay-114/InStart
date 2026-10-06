#pragma once
#include <jni.h>

bool nofall_init(JavaVM* vm, const struct McVerMap& M, JNIEnv* env);
