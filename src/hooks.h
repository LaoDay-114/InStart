#pragma once
#include <windows.h>

void hooks_init(HMODULE self);

void hooks_frame();

void hooks_set_bind_waiting(int idx);
int  hooks_get_bind_waiting();

const char* hooks_vk_name(int vk, char* buf, size_t len);
