#include "Camera.hpp"
#include "api.hpp"
#include "imgui/imgui.h"

#include <GLFW/glfw3.h>

Camera3d::Camera3d()
{
    controls_[Forward] = GLFW_KEY_W;
    controls_[Back] = GLFW_KEY_S;
    controls_[Left] = GLFW_KEY_A;
    controls_[Right] = GLFW_KEY_D;
    controls_[Up] = GLFW_KEY_SPACE;
    controls_[Down] = GLFW_KEY_LEFT_SHIFT;
    controls_[ToggleMouseCapture] = GLFW_KEY_ESCAPE;
}

void Camera3d::captureMouse()
{
    glfwSetInputMode(_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    firstCursorEvent_ = true;
    mouseCapture_ = true;
}

void Camera3d::processEvent(const WinEvent& event)
{
    if(event.type == WinEvent::Key)
    {
        int idx = -1;
        for(int i = 0; i < NumControls; ++i)
        {
            if(event.key.key == controls_[i])
            {
                idx = i;
                break;
            }
        }

        if(idx == -1)
            return;

        if(event.key.action == GLFW_PRESS)
        {
            keys_.pressed[idx] = true;
            keys_.held[idx] = true;

            if(idx == ToggleMouseCapture)
            {
                if(!mouseCapture_)
                    captureMouse();
                else
                {
                    mouseCapture_ = false;
                    glfwSetInputMode(_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                }
            }
        }
        else if(event.key.action == GLFW_RELEASE)
        {
            keys_.held[idx] = false;
        }
    }
    else if(event.type == WinEvent::Cursor && mouseCapture_)
    {
        const vec2 offset = event.cursor.pos - cursorPos_;
        cursorPos_ = event.cursor.pos;

        if(!firstCursorEvent_)
        {
            pitch -= offset.y * sensitivity;
            pitch = min(89.f, pitch);
            pitch = max(-89.f, pitch);
            yaw = fmodf(yaw - offset.x * sensitivity, 360.f);
        }
        else
            firstCursorEvent_ = false;
    }
}

void Camera3d::update(const float dt)
{
    up = normalize(up);

    dir = normalize(vec3(
                        cosf(toRadians(pitch)) * sinf(toRadians(yaw)) * -1.f,
                        sinf(toRadians(pitch)),
                        cosf(toRadians(pitch)) * cosf(toRadians(yaw)) * -1.f));
    
    vec3 moveDir(0.f);

    {
        const vec3 forwardDir = forwardXZonly ? normalize(vec3(dir.x, 0.f, dir.z)) : dir;
        if(cActive(Forward)) moveDir += forwardDir;
        if(cActive(Back)) moveDir -= forwardDir;
    }
    {
        const vec3 right = normalize(cross(dir, up));
        if(cActive(Left)) moveDir -= right;
        if(cActive(Right)) moveDir += right;
    }

    if(cActive(Up)) moveDir += up;
    if(cActive(Down)) moveDir -= up;

    if(length(moveDir) != 0.f)
        normalize(moveDir);

    pos += moveDir * speed * dt;
    view = lookAt(pos, pos + dir, up);

    for(bool& key: keys_.pressed)
        key = false;
}

void Camera3d::imgui()
{
    ImGui::Text("CAMERA");
    ImGui::Text("enable / disable mouse capture - Esc");
    ImGui::Checkbox("disable flying with WS", &forwardXZonly);
    ImGui::Text("pitch / yaw - mouse");
    ImGui::Text("move - wsad, space (up), lshift (down)");
    ImGui::Text("pos: x: %.3f, y: %.3f, z: %.3f", pos.x, pos.y, pos.z);
    ImGui::Text("pitch: %.3f, yaw: %.3f", pitch, yaw);
}
