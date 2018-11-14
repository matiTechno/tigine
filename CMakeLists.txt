cmake_minimum_required(VERSION 3.0)
project(tigine)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Wall -Wextra -pedantic -fno-rtti \
    -fno-exceptions -Wno-class-memaccess")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(tigine
    main.cpp
    Texture.cpp
    Shader.cpp
    Camera.cpp
    render.cpp
    glad.c
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_impl_glfw_gl3.cpp
    )

target_link_libraries(tigine -lassimp -ldl -lglfw)
