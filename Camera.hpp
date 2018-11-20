#pragma once

#include "math.hpp"

struct WinEvent;

struct Camera3d
{
    Camera3d();
    void captureMouse(); // off on start
    void processEvent(const WinEvent& event);
    // use after processing events
    void update(float dt);
    void imgui();

    bool forwardXZonly = false; // disable flying with W and S controls
    vec3 up = { 0.f, 1.f, 0.f }; // will be normalized in update()
    float speed = 600.f;
    float sensitivity = 0.1f; // degrees / screen coordinates
    // from GLFW docs - screen coordinates are not always pixels

    // get after update()
    mat4 view;
    vec3 dir;
    vec3 pos = { 0.f, 0.2f, 1.2f };
    // degrees
    float pitch = 0.f;
    float yaw = 0.f;

private:
    enum
    {
        Forward,
        Back,
        Left,
        Right,
        Up,
        Down,
        ToggleMouseCapture,
        NumControls
    };

    int controls_[NumControls];

    struct
    {
        bool pressed[NumControls] = {};
        bool held[NumControls] = {};
    } keys_;

    vec2 cursorPos_;
    bool mouseCapture_ = false;
    bool firstCursorEvent_;

    bool cActive(int control) const
    { return keys_.pressed[control] || keys_.held[control]; }
};
