-- MinGW-w64 x64，静态链接运行时。
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

-- 版本与 CI 标签保持一致：build-<提交计数>-<短SHA>。
-- 根作用域不能执行外部命令，放到规则的 on_config 里取 git 信息。
rule("inst_version")
    on_config(function (target)
        local ver = "dev"
        try {
            function ()
                local count = os.iorun("git rev-list --count HEAD"):trim()
                local sha   = os.iorun("git rev-parse --short HEAD"):trim()
                ver = "build-" .. count .. "-" .. sha
            end
        }
        target:add("defines", 'INST_VERSION="' .. ver .. '"', {force = true})
    end)

-- 注入进 Minecraft(Java版) 的功能模块
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
        "src/nofall.cpp",
        "src/jni/mc.cpp",
        "update/update_check.cpp",
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
        "update",
        IMGUI,
        IMGUI .. "/backends",
        MH .. "/include",
        jdk .. "/include",
        jdk .. "/include/win32")

    add_defines("UNICODE", "_UNICODE")
    add_syslinks("opengl32", "gdi32", "user32", "kernel32", "dwmapi", "winhttp", "shell32")
    add_rules("inst_version")
    -- DLL 是 shared 目标，静态运行时必须走 shflags（ldflags 只对 exe 生效）
    add_shflags("-static", "-static-libgcc", "-static-libstdc++", {force = true})

target("InStartInjector")
    set_kind("binary")
    set_targetdir(".")
    set_basename("InStartInjector")
    add_files("injector/injector.cpp")
    add_defines("UNICODE", "_UNICODE")
    add_syslinks("user32")
    add_ldflags("-static", "-static-libgcc", "-static-libstdc++", {force = true})

-- 启动器与更新器已迁移到 EUI-NEO + CMake（见根目录 CMakeLists.txt），
-- xmake 只负责游戏内 DLL 与注入器。
