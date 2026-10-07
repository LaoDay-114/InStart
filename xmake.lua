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
    add_shflags("-static", "-static-libgcc", "-static-libstdc++", {force = true})

target("InStartInjector")
    set_kind("binary")
    set_targetdir(".")
    set_basename("InStartInjector")
    add_files("injector/injector.cpp")
    add_defines("UNICODE", "_UNICODE")
    add_syslinks("user32")
    add_ldflags("-static", "-static-libgcc", "-static-libstdc++", {force = true})
