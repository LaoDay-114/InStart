cmake_minimum_required(VERSION 3.14)
project(InStart)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

execute_process(
    COMMAND git rev-list --count HEAD
    OUTPUT_VARIABLE GIT_COUNT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
execute_process(
    COMMAND git rev-parse --short HEAD
    OUTPUT_VARIABLE GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(GIT_COUNT AND GIT_SHA)
    set(INST_VERSION "build-${GIT_COUNT}-${GIT_SHA}")
else()
    set(INST_VERSION "dev")
endif()
add_compile_definitions(INST_VERSION="${INST_VERSION}")

set(EUI_BUILD_APPS OFF CACHE BOOL "" FORCE)
set(EUI_BUILD_USER_APPS OFF CACHE BOOL "" FORCE)
set(EUI_BUILD_TEST_FIXTURES OFF CACHE BOOL "" FORCE)
set(EUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(EUI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(EUI_DEFAULT_RENDER_BACKEND "opengl" CACHE STRING "" FORCE)
add_subdirectory(3rd/EUI-NEO)

get_property(EUI_APP_MAIN_SOURCE GLOBAL PROPERTY EUI_NEO_APP_MAIN_SOURCE)
get_property(EUI_APP_ICON_RESOURCE GLOBAL PROPERTY EUI_NEO_APP_ICON_RESOURCE)

add_executable(InStart launcher/main.cpp)
target_include_directories(InStart PRIVATE injector)
target_compile_definitions(InStart PRIVATE UNICODE _UNICODE)
eui_neo_configure_app(InStart)
target_link_libraries(InStart PRIVATE dwmapi)
set_target_properties(InStart PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(InStartUpdateManager update/main.cpp update/update_check.cpp)
target_include_directories(InStartUpdateManager PRIVATE update injector)
target_compile_definitions(InStartUpdateManager PRIVATE UNICODE _UNICODE EUI_APP_RUNNER_LIBRARY=1)
target_sources(InStartUpdateManager PRIVATE ${EUI_APP_MAIN_SOURCE} ${EUI_APP_ICON_RESOURCE})
target_link_libraries(InStartUpdateManager PRIVATE eui::neo winhttp shell32 dwmapi)
eui_apply_compile_options(InStartUpdateManager)
eui_apply_app_link_options(InStartUpdateManager)
eui_neo_copy_assets(InStartUpdateManager)
eui_neo_copy_runtime_dependencies(InStartUpdateManager)
set_target_properties(InStartUpdateManager PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
