<p align="center">
    InStart<br>
    一个轻量·开源·国产的我的世界注入外挂实现
</p>

<p align="center">
    <img src="https://img.shields.io/badge/C%2B%2B-17-blue?labelColor=555&logo=cplusplus" alt="C++">
    <img alt="CMake 3.14+" src="https://img.shields.io/badge/CMake-3.14%2B-064F8C?logo=cmake&logoColor=white">
    <img src="https://img.shields.io/badge/Python-3.14-green?labelColor=555&logo=Python" alt="Python">
    <img src="https://img.shields.io/badge/Lua-XMake-green?labelColor=555&logo=lua" alt="Lua">
    <img alt="OpenGL / Vulkan" src="https://img.shields.io/badge/OpenGL%20%2F%20Vulkan-rendering-5586A4?logo=vulkan&logoColor=white">
    <a href="https://github.com/LaoDay-114/InStart/releases"><img alt="Release" src="https://img.shields.io/github/v/release/LaoDay-114/InStart?include_prereleases&sort=semver"></a>
    <a href="https://github.com/LaoDay-114/InStart/stargazers"><img alt="GitHub stars" src="https://img.shields.io/github/stars/LaoDay-114/InStart?style=flat"></a>
</p>

### 构建要求
- CMake 3.14+
- C++17编译器：MSVC 19.29+（Visual Studio 2019 16.11+）、GCC/MinGW-w64 12+，或Clang 14+
- XMake
- Jdk 17+
- git

> [!WARNING]
> 本项目仅支持 Windows
> 
> 推荐使用 Windows8.1+

#### 构建 dll 与注入器
推荐使用 `xmake` 构建
```bash
xmake f - m release
xmake - j4
```
这将会构建 `InStart.dll` 与 `InStartInjector.exe`

### 构建更新器与启动器
这次不一样了，需要使用 `Git` 和 `CMake`
```bash
git submodule update --init --recursive 
cmake - S . - B build - cmake - G "MinGW Makefiles" - DCMAKE_BUILD_TYPE = Release 
cmake --build build-cmake -j4
```
将会拉取 `EUI-NEO` 项目并构建 `InStart.exe` 与 `InStartUpdateManager.exe`

### 项目结构
```tree
third_party/          ImGui与其他依赖
mappings/             对应游戏版本的yarn映射
injector/             注入器代码
3rd/                  EUI-NEO项目
launcher/             启动器代码
src/                  InStart.dll代码
update/               更新器代码
```

### 许可
你懂的，依旧是GPLv3
