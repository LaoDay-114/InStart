#pragma once
#include <windows.h>

// 初始化渲染/输入 Hook（在独立线程调用）
void hooks_init(HMODULE self);

// 每帧（wglSwapBuffers 内）：应用功能 + 绘制 UI
void hooks_frame();

// 按键绑定等待状态（menu.cpp 调用）
void hooks_set_bind_waiting(int idx); // idx >= 0 进入等待，-1 取消
int  hooks_get_bind_waiting();        // 返回 -1 表示不在等待

// VK 码 -> 可读名字（buf 至少 16 字节）
const char* hooks_vk_name(int vk, char* buf, size_t len);
