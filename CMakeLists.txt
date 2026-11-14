cmake_minimum_required(VERSION 3.20)
project(BeterWindow LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(BeterWindow WIN32
    src/main.cpp
    src/app.cpp
    src/common.cpp
    src/config.cpp
    src/effects.cpp
    src/painter.cpp
    src/winenum.cpp
    res/app.rc
)

target_compile_definitions(BeterWindow PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(BeterWindow PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_link_libraries(BeterWindow PRIVATE
    d2d1 dwrite dwmapi uxtheme shell32 ole32 uuid gdi32 user32
)

if(MINGW)
    target_link_options(BeterWindow PRIVATE -static -static-libgcc -static-libstdc++)
endif()
