#pragma once

// 在游戏渲染线程（Render thread）上调用；
// 首次调用时附着 JVM 并解析全部类/成员（失败则下一帧自动重试）
bool mc_jni_init();

// 每帧调用：把 g_cfg 中的开关写入游戏内部对象，并回填 g_state
void mc_apply_features();
