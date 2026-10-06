-- ============================================================
-- InStart xmake 构建脚本（MinGW-w64 x64，静态链接运行时）
-- 用法: xmake f -p mingw --mingw=E:\mingw64 -m release -y && xmake
-- ============================================================
set_project("InStart")
set_languages("c99", "c++17")
set_arch("x64")
set_runtimes("static")

local jdk = os.getenv("JAVA_HOME")
if not (jdk and os.isdir(jdk)) then
    raise("未找到 JAVA_HOME，请安装 JDK")
end
local IMGUI = "third_party/imgui-1.91.8"
local MH    = "third_party/minhook-1.3.3"

-- ---------- InStart.dll：注入进 Minecraft(Java版) 的功能模块 ----------
target("InStart")
    set_kind("shared")
    set_targetdir(".")
    set_prefixname("")
    set_basename("InStart")
    set_extension(".dll")
    add_rules("utils.symbols.export_all")

    add_files(
        "src/dllmain.cpp",
        "src/hooks.cpp",
        "src/menu.cpp",
        "src/config.cpp",
        "src/jni/mc.cpp",
        IMGUI .. "/imgui.cpp",
        IMGUI .. "/imgui_draw.cpp",
        IMGUI .. "/imgui_tables.cpp",
        IMGUI .. "/imgui_widgets.cpp",
        IMGUI .. "/backends/imgui_impl_opengl3.cpp",
        IMGUI .. "/backends/imgui_impl_win32.cpp",
        MH .. "/src/hook.c",
        MH .. "/src/buffer.c",
        MH .. "/src/trampoline.c",
        MH .. "/src/hde/hde64.c")

    add_includedirs(
        "src",
        "src/jni",
        IMGUI,
        IMGUI .. "/backends",
        MH .. "/include",
        jdk .. "/include",
        jdk .. "/include/win32")

    add_defines("UNICODE", "_UNICODE")
    add_syslinks("opengl32", "gdi32", "user32", "kernel32", "dwmapi")
    -- 静态链接 MinGW 运行时：目标机器无需 libgcc/libstdc++（否则 LoadLibraryW 返回 0）
    -- 注意：DLL 是 shared 目标，必须用 shflags（ldflags 只对 exe 生效）
    add_shflags("-static", "-static-libgcc", "-static-libstdc++", {force = true})

-- ---------- InStartInjector.exe：注入器 ----------
target("InStartInjector")
    set_kind("binary")
    set_targetdir(".")
    set_basename("InStartInjector")
    add_files("injector/injector.cpp")
    add_defines("UNICODE", "_UNICODE")
    add_syslinks("user32")
    add_ldflags("-static", "-static-libgcc", "-static-libstdc++", {force = true})

-- ---------- InStart.exe：启动器（现代 UI，调用注入器完成注入） ----------
target("InStartLauncher")
    set_kind("binary")
    set_targetdir(".")
    set_basename("InStart")
    add_files(
        "launcher/main.cpp",
        IMGUI .. "/imgui.cpp",
        IMGUI .. "/imgui_draw.cpp",
        IMGUI .. "/imgui_tables.cpp",
        IMGUI .. "/imgui_widgets.cpp",
        IMGUI .. "/backends/imgui_impl_win32.cpp",
        IMGUI .. "/backends/imgui_impl_dx11.cpp")
    add_includedirs(IMGUI, IMGUI .. "/backends")
    add_defines("UNICODE", "_UNICODE")
    add_syslinks("d3d11", "dxgi", "d3dcompiler", "dwmapi", "user32", "gdi32")
    add_ldflags("-static", "-static-libgcc", "-static-libstdc++", {force = true})
