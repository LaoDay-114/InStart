// ============================================================
// InStart 动态模糊：反馈式帧累积
//   当前帧 = 新帧 * (1-α) + 上帧混合结果 * α  →  运动拖影
// 纯 OpenGL 实现（复用 imgui_impl_opengl3 已加载的 GL 函数指针），
// 不涉及 JNI/映射，与游戏版本无关。
// ============================================================
#include <windows.h>
#include "backends/imgui_impl_opengl3_loader.h"

#include "blur.h"
#include "config.h"

// GL 1.1 函数：imgui 的 GL 加载器未声明，由 opengl32.dll 直接导出
extern "C" {
__declspec(dllimport) void __stdcall glCopyTexImage2D(
    GLenum target, GLint level, GLenum internalformat,
    GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
__declspec(dllimport) void __stdcall glDrawArrays(GLenum mode, GLint first, GLsizei count);
__declspec(dllimport) void __stdcall glBlendFunc(GLenum sfactor, GLenum dfactor);
__declspec(dllimport) void __stdcall glReadBuffer(GLenum mode);
}

// 加载器没有的 FBO 常量
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER                0x8D40
#define GL_READ_FRAMEBUFFER           0x8CA8
#define GL_DRAW_FRAMEBUFFER           0x8CA9
#define GL_READ_FRAMEBUFFER_BINDING   0x8CAA
#define GL_DRAW_FRAMEBUFFER_BINDING   0x8CA6
#define GL_READ_BUFFER                0x0C02
#endif

static GLuint g_prog = 0, g_tex = 0, g_vao = 0;
static GLint  g_uTex = -1, g_uAlpha = -1;
static int    g_texW = 0, g_texH = 0;
static bool   g_prevValid  = false; // 上帧纹理是否有效（首帧/刚开启时无效）
static bool   g_initFailed = false; // 初始化失败则永久停用，避免每帧重试

// imgui 加载器只提供 glUniform1i，1f 变体与 FBO 函数自行取地址
using PFNGLUNIFORM1F = void (APIENTRY*)(GLint, GLfloat);
using PFNGLBINDFRAMEBUFFER = void (APIENTRY*)(GLenum, GLuint);
static PFNGLUNIFORM1F pglUniform1f = nullptr;
static PFNGLBINDFRAMEBUFFER pglBindFramebuffer = nullptr;

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(s); return 0; }
    return s;
}

static bool blur_init() {
    if (g_prog) return true;

    // 全屏三角形：无顶点缓冲，gl_VertexID 生成（core profile 兼容）
    static const char* VS =
        "#version 150 core\n"
        "out vec2 uv;\n"
        "void main() {\n"
        "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
        "    uv = p;\n"
        "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
        "}\n";
    static const char* FS =
        "#version 150 core\n"
        "uniform sampler2D tex;\n"
        "uniform float alpha;\n"
        "in vec2 uv;\n"
        "out vec4 frag;\n"
        "void main() {\n"
        "    frag = vec4(texture(tex, uv).rgb, alpha);\n"
        "}\n";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, VS);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FS);
    if (!vs || !fs) return false;

    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(g_prog); g_prog = 0; return false; }

    g_uTex   = glGetUniformLocation(g_prog, "tex");
    g_uAlpha = glGetUniformLocation(g_prog, "alpha");
    pglUniform1f = (PFNGLUNIFORM1F)wglGetProcAddress("glUniform1f");
    if (!pglUniform1f) return false;
    pglBindFramebuffer = (PFNGLBINDFRAMEBUFFER)wglGetProcAddress("glBindFramebuffer");
    if (!pglBindFramebuffer) return false;

    glGenVertexArrays(1, &g_vao);

    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void blur_apply() {
    if (!g_cfg.motionBlur) { g_prevValid = false; return; }
    if (g_initFailed) return;
    if (!blur_init()) { g_initFailed = true; return; }

    // 当前帧缓冲尺寸
    GLint vp[4] = {};
    glGetIntegerv(GL_VIEWPORT, vp);
    int w = vp[2], h = vp[3];
    if (w <= 8 || h <= 8) return;
    if (w != g_texW || h != g_texH) { // 窗口尺寸变化：丢弃旧帧
        g_texW = w; g_texH = h;
        g_prevValid = false;
    }

    // ---- 保存游戏 GL 状态 ----
    GLint prevProg = 0, prevVao = 0, prevTex = 0;
    GLint prevBlendSrc = 0, prevBlendDst = 0, prevBlendEq = 0;
    GLint prevReadFbo = 0, prevDrawFbo = 0, prevReadBuf = 0, prevActiveTex = 0;
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDst);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prevBlendEq);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_READ_BUFFER, &prevReadBuf);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);

    // 游戏画面在 SwapBuffers 前已合成到默认帧缓冲（FBO 0），
    // 绘制与捕获都必须针对 FBO 0 + GL_BACK，否则会捕获到灰色/空内容
    pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glDisable(GL_SCISSOR_TEST); // 游戏可能残留剪刀区域，会裁掉全屏三角形

    // ---- 1. 把上帧纹理按 α 混合画到当前帧上 ----
    glUseProgram(g_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glUniform1i(g_uTex, 0);
    pglUniform1f(g_uAlpha, g_prevValid ? g_cfg.blurAmount : 0.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // ---- 2. 捕获混合后的帧，作为下一帧的"上帧"（反馈累积）----
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, w, h, 0);
    g_prevValid = true;

    // ---- 恢复游戏 GL 状态 ----
    glBindVertexArray((GLuint)prevVao);
    glUseProgram((GLuint)prevProg);
    glActiveTexture((GLenum)prevActiveTex);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    glBlendFunc((GLenum)prevBlendSrc, (GLenum)prevBlendDst);
    glBlendEquation((GLenum)prevBlendEq);
    glReadBuffer((GLenum)prevReadBuf);
    pglBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevReadFbo);
    pglBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDrawFbo);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
}
