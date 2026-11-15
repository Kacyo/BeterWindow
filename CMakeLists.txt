cmake_minimum_required(VERSION 3.20)
project(BeterWindow LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---- BeterWindowUI：可复用的窗口 UI 库 ----
add_library(BeterWindowUI SHARED
    library/src/painter.cpp
    library/src/theme.cpp
    library/src/widgets.cpp
    library/src/window.cpp
)

set_target_properties(BeterWindowUI PROPERTIES
    OUTPUT_NAME "Kacyo.BeterWindow.UI"
    PREFIX ""
)

target_compile_definitions(BeterWindowUI PRIVATE BWUI_BUILD UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(BeterWindowUI PUBLIC library/include)
target_compile_options(BeterWindowUI PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(BeterWindowUI PRIVATE d2d1 dwrite dwmapi gdi32 user32 ole32 uuid)

if(MINGW)
    target_link_options(BeterWindowUI PRIVATE -static-libgcc -static-libstdc++)
endif()

# ---- BeterWindow：主程序 ----
add_executable(BeterWindow WIN32
    src/main.cpp
    src/app.cpp
    src/common.cpp
    src/config.cpp
    src/effects.cpp
    src/winenum.cpp
    res/app.rc
)

target_compile_definitions(BeterWindow PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(BeterWindow PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_link_libraries(BeterWindow PRIVATE
    BeterWindowUI d2d1 dwrite dwmapi uxtheme shell32 ole32 uuid gdi32 user32
)

if(MINGW)
    target_link_options(BeterWindow PRIVATE -static -static-libgcc -static-libstdc++)
endif()
