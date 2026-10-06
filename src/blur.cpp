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
__declspec(dllimport) void __stdcall glFrontFace(GLenum mode);
__declspec(dllimport) void __stdcall glColorMask(
    GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
__declspec(dllimport) void __stdcall glGetBooleanv(GLenum pname, GLboolean* params);
}

// glCopyTexSubImage2D 是 GL 1.2：Windows opengl32.dll 不保证导出，走扩展指针
using PFNGLCOPYTEXSUBIMAGE2D = void (APIENTRY*)(
    GLenum target, GLint level, GLint x, GLint y, GLint x2, GLint y2,
    GLsizei width, GLsizei height);
static PFNGLCOPYTEXSUBIMAGE2D pglCopyTexSubImage2D = nullptr;

// 加载器没有的 FBO / 状态常量
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER                0x8D40
#define GL_READ_FRAMEBUFFER           0x8CA8
#define GL_DRAW_FRAMEBUFFER           0x8CA9
#define GL_READ_FRAMEBUFFER_BINDING   0x8CAA
#define GL_DRAW_FRAMEBUFFER_BINDING   0x8CA6
#endif
#ifndef GL_READ_BUFFER
#define GL_READ_BUFFER                0x0C02
#endif
#ifndef GL_CULL_FACE
#define GL_CULL_FACE                  0x0B44
#endif
#ifndef GL_FRONT_FACE
#define GL_FRONT_FACE                 0x0B46
#endif
#ifndef GL_STENCIL_TEST
#define GL_STENCIL_TEST               0x0B90
#endif
#ifndef GL_COLOR_WRITEMASK
#define GL_COLOR_WRITEMASK            0x0CA2
#endif
#ifndef GL_RGBA8
#define GL_RGBA8                      0x8058
#endif
#ifndef GL_CCW
#define GL_CCW                        0x0901
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
    pglCopyTexSubImage2D =
        (PFNGLCOPYTEXSUBIMAGE2D)wglGetProcAddress("glCopyTexSubImage2D");
    if (!pglCopyTexSubImage2D) return false;

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

// 分配/重分配纹理存储（显式分配，避免首帧采样到不完整纹理）
static void alloc_texture(int w, int h) {
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    g_texW = w; g_texH = h;
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
    if (w != g_texW || h != g_texH) { // 窗口尺寸变化：重分配并丢弃旧帧
        alloc_texture(w, h);
        g_prevValid = false;
    }

    // ---- 保存游戏 GL 状态 ----
    GLint prevProg = 0, prevVao = 0;
    GLint prevBlendEq = 0, prevFrontFace = 0;
    GLint prevBlendSrc = 0, prevBlendDst = 0;
    GLint prevReadFbo = 0, prevDrawFbo = 0, prevReadBuf = 0;
    GLint prevVp[4] = {};
    GLboolean prevBlend   = glIsEnabled(GL_BLEND);
    GLboolean prevDepth   = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevCull    = glIsEnabled(GL_CULL_FACE);
    GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean prevMask[4] = {};
    // 保存纹理单元 0 的 2D 绑定（我们固定在单元 0 上操作）
    GLint prevActiveTex = 0, prevTex0 = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex0);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &prevBlendEq);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDst);
    glGetIntegerv(GL_FRONT_FACE, &prevFrontFace);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_READ_BUFFER, &prevReadBuf);
    glGetIntegerv(GL_VIEWPORT, prevVp);
    glGetBooleanv(GL_COLOR_WRITEMASK, prevMask);

    // 游戏画面在 SwapBuffers 前已合成到默认帧缓冲（FBO 0），
    // 绘制与捕获都必须针对 FBO 0 + GL_BACK，否则会捕获到灰色/空内容
    pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glViewport(0, 0, w, h);

    // 关闭一切可能"静默丢弃片元/裁剪几何"的状态：
    //   scissor/深度不再赘述；
    //   cull —— 游戏可能残留 GL_CW 正面朝向，全屏三角形会被整体剔除（无拖影也不报错）；
    //   stencil —— 实体/附魔轮廓渲染残留的模板 mask 会拒绝全部片元；
    //   colorMask —— 保证 RGB 可写
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glFrontFace(GL_CCW);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // ---- 1. 把上帧纹理按 α 混合画到当前帧上 ----
    glUseProgram(g_prog);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glUniform1i(g_uTex, 0);
    pglUniform1f(g_uAlpha, g_prevValid ? g_cfg.blurAmount : 0.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // ---- 2. 捕获混合后的帧，作为下一帧的"上帧"（反馈累积）----
    pglCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
    g_prevValid = true;

    // ---- 恢复游戏 GL 状态 ----
    // 顺序关键：必须先恢复 FBO 绑定，再恢复 read buffer —— 游戏原 FBO 的
    // read buffer 通常是 GL_COLOR_ATTACHMENT0，在默认 FBO 0 上设置该值会
    // 产生 GL_INVALID_ENUM（默认帧缓冲只接受 GL_BACK/GL_NONE）
    pglBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevReadFbo);
    pglBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDrawFbo);
    glReadBuffer((GLenum)prevReadBuf);

    glBindVertexArray((GLuint)prevVao);
    glUseProgram((GLuint)prevProg);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex0);
    glBlendEquation((GLenum)prevBlendEq);
    glBlendFunc((GLenum)prevBlendSrc, (GLenum)prevBlendDst);
    glViewport((GLint)prevVp[0], (GLint)prevVp[1],
               (GLint)prevVp[2], (GLint)prevVp[3]);
    glColorMask(prevMask[0], prevMask[1], prevMask[2], prevMask[3]);
    glFrontFace((GLenum)prevFrontFace);
    if (prevBlend)   glEnable(GL_BLEND);   else glDisable(GL_BLEND);
    if (prevDepth)   glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (prevCull)    glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (prevStencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    glActiveTexture((GLenum)prevActiveTex);
}
