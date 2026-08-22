cmake_minimum_required(VERSION 3.20)

project(WindowLayoutManager LANGUAGES CXX)

add_executable(WindowLayoutManager WIN32
    src/main.cpp
)

target_compile_features(WindowLayoutManager PRIVATE cxx_std_17)
target_compile_definitions(WindowLayoutManager PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
target_link_libraries(WindowLayoutManager PRIVATE user32 gdi32 dwmapi)

if(MSVC)
    set_property(TARGET WindowLayoutManager PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
