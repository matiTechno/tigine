#pragma once

#include "Array.hpp"
#include "math.hpp"

struct WinEvent
{
    enum Type
    {
        Nil,
        Key,
        Cursor,
        MouseButton,
        Scroll
    };

    Type type;

    union
    {
        struct
        {
            // glfw values
            int key;
            int action;
            int mods;
        } key;

        struct
        {
            vec2 pos;
        } cursor;

        struct
        {
            int button;
            int action;
            int mods;
        } mouseButton;

        struct
        {
            vec2 offset;
        } scroll;
    };
};

struct Frame
{
    ivec2 bufferSize;
    float dt;
    bool quit = 0;
    bool showGui = true;
    Array<WinEvent> winEvents;
};

struct GLFWwindow;

extern GLFWwindow* _window;

void log(const char* fmt, ...);

void renderExecuteFrame(const Frame& frame);

// use on plain C arrays
template<typename T, int N>
constexpr int getSize(T(&)[N]) {return N;}
