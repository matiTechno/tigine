#include "api.hpp"
#include "Array.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw_gl3.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "glad.h"
#include <GLFW/glfw3.h>
#ifndef GLFW_TRUE // WA for older ubuntu systems
#define GLFW_TRUE 1
#endif

GLFWwindow* _window; // used by Camera
static Array<WinEvent>* _winEvents; // used by glfw callbacks
static Array<char> _logBuf; // used by log()


static void errorCallback(const int error, const char* const description);
static void keyCallback(GLFWwindow* const window, const int key, const int scancode,
                             const int action, const int mods);
static void cursorPosCallback(GLFWwindow*, const double xpos, const double ypos);
static void mouseButtonCallback(GLFWwindow* const window, const int button,
        const int action, const int mods);
static void scrollCallback(GLFWwindow* const window, const double xoffset,
                           const double yoffset);
static void charCallback(GLFWwindow* const window, const unsigned int codepoint);

int main()
{
    _logBuf.pushBack('\0');

    glfwSetErrorCallback(errorCallback);

    if(!glfwInit())
        return EXIT_FAILURE;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

    GLFWwindow* window;
    {
        GLFWmonitor* const monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        window = glfwCreateWindow(mode->width, mode->height, "tigine", monitor, nullptr);
    }

    if(!window)
    {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);

    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        printf("gladLoadGLLoader() failed\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    log("tigine says hello!");
    log("gl version:  %d.%d", GLVersion.major, GLVersion.minor);
    log("gl vendor:   %s", glGetString(GL_VENDOR));
    log("gl renderer: %s", glGetString(GL_RENDERER));

    _window = window;

    glfwSwapInterval(1);

    glfwSetKeyCallback(window, keyCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetCharCallback(window, charCallback);

    ImGui::CreateContext();
    ImGui_ImplGlfwGL3_Init(window, false);
    ImGui::StyleColorsDark();

    Frame frame;
    frame.winEvents.reserve(50);
    _winEvents = &frame.winEvents;

    struct FpsPlot
    {
        enum {SAMPLE_COUNT = 1000};
        float frameTimes[SAMPLE_COUNT] = {}; // ms
    } fpsPlot; 
    double time = glfwGetTime();

    while(!frame.quit)
    {
        frame.quit = frame.quit || glfwWindowShouldClose(window);

        double newTime = glfwGetTime();
        frame.dt = newTime - time;
        time = newTime;

        memmove(fpsPlot.frameTimes, fpsPlot.frameTimes + 1,
                sizeof(fpsPlot.frameTimes) - sizeof(float));

        fpsPlot.frameTimes[getSize(fpsPlot.frameTimes) - 1] = frame.dt * 1000.f;

        frame.winEvents.clear();
        glfwPollEvents();
        ImGui_ImplGlfwGL3_NewFrame();

        const bool imguiWantMouse = ImGui::GetIO().WantCaptureMouse;
        const bool imguiWantKeyboard = ImGui::GetIO().WantCaptureKeyboard;

        for(WinEvent& e: frame.winEvents)
        {
            if(imguiWantMouse && (
                        (e.type == WinEvent::MouseButton &&
                         e.mouseButton.action != GLFW_RELEASE) ||
                        e.type == WinEvent::Cursor ||
                        e.type == WinEvent::Scroll) )
                e.type = WinEvent::Nil;

            if(imguiWantKeyboard && (e.type == WinEvent::Key &&
                                     e.key.action != GLFW_RELEASE))
                e.type = WinEvent::Nil;
        }

        glfwGetFramebufferSize(window, &frame.bufferSize.x, &frame.bufferSize.y);

        for(WinEvent& e: frame.winEvents)
        {
            if(e.type == WinEvent::Key && e.key.action == GLFW_PRESS &&
                    e.key.key == GLFW_KEY_TAB)
            {
                frame.showGui = !frame.showGui;
            }
        }

        if(frame.showGui)
        {
            ImGui::Begin("main");
            {
                ImGui::Text("press TAB to hide all gui");

                if(ImGui::Button("quit"))
                    frame.quit = true;

                ImGui::Spacing();
                ImGui::Text("framebuffer size: %d x %d", frame.bufferSize.x,
                        frame.bufferSize.y);

                ImGui::Spacing();
                ImGui::Text("vsync");
                ImGui::SameLine();

                if(ImGui::Button("on "))
                    glfwSwapInterval(1);
                
                ImGui::SameLine();

                if(ImGui::Button("off"))
                    glfwSwapInterval(0);

                ImGui::End();
            }

            ImGui::Begin("fps");
            {
                float maxTime = 0.f;
                float sum = 0.f;

                for(const float t: fpsPlot.frameTimes)
                {
                    sum += t;
                    maxTime = max(maxTime, t);
                }

                const float avg = sum / getSize(fpsPlot.frameTimes);

                ImGui::Text("frame time ms");
                ImGui::PushStyleColor(ImGuiCol_Text, {0.f, 0.85f, 0.f, 1.f});
                ImGui::Text("avg   %.3f (%d)", avg, int(1.f / avg * 1000.f + 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Text, {0.9f, 0.f, 0.f, 1.f});
                ImGui::Text("max   %.3f", maxTime);
                ImGui::PopStyleColor(2);

                ImGui::Spacing();

                ImVec4 color = ImGuiStyle().Colors[ImGuiCol_WindowBg];
                color.w = 0.5f;
                ImGui::PushStyleColor(ImGuiCol_FrameBg,color);

                ImGui::PlotLines("", fpsPlot.frameTimes, getSize(fpsPlot.frameTimes), 0,
                        nullptr, 0.f, 20.f, {FpsPlot::SAMPLE_COUNT, 80});

                ImGui::PopStyleColor(1);

                ImGui::End();
            }

            ImGui::Begin("log");
            {
                ImGui::TextUnformatted(_logBuf.begin());
                ImGui::End();
            }
        }

        renderExecuteFrame(frame);

        ImGui::Render();
        ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplGlfwGL3_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
    return EXIT_SUCCESS;
}

void log(const char* fmt, ...)
{
    static char tempBuf[10000];

    if(_logBuf.size() > 20000)
        _logBuf.resize(20000);

    va_list args;
    va_start(args, fmt);
    vsnprintf(tempBuf, sizeof tempBuf, fmt, args);
    va_end(args);

    int len = strlen(tempBuf);
    tempBuf[len++] = '\n';

    const int prevSize = _logBuf.size();
    _logBuf.resize(len + prevSize);

    memmove(_logBuf.begin() + len, _logBuf.begin(), prevSize);
    memcpy(_logBuf.begin(), tempBuf, len);
}

static void errorCallback(const int error, const char* const description)
{
    (void)error;
    printf("GLFW error: %s\n", description);
}

static void keyCallback(GLFWwindow* const window, const int key, const int scancode,
                             const int action, const int mods)
{
    WinEvent e;
    e.type = WinEvent::Key;
    e.key.key = key;
    e.key.action = action;
    e.key.mods = mods;
    _winEvents->pushBack(e);

    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
}

static void cursorPosCallback(GLFWwindow*, const double xpos, const double ypos)
{
    WinEvent e;
    e.type = WinEvent::Cursor;
    e.cursor.pos.x = xpos;
    e.cursor.pos.y = ypos;
    _winEvents->pushBack(e);
}

static void mouseButtonCallback(GLFWwindow* const window, const int button,
        const int action, const int mods)
{
    WinEvent e;
    e.type = WinEvent::MouseButton;
    e.mouseButton.button = button;
    e.mouseButton.action = action;
    e.mouseButton.mods = mods;
    _winEvents->pushBack(e);

    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
}

static void scrollCallback(GLFWwindow* const window, const double xoffset,
                           const double yoffset)
{
    WinEvent e;
    e.type = WinEvent::Scroll;
    e.scroll.offset.x = xoffset;
    e.scroll.offset.y = yoffset;
    _winEvents->pushBack(e);

    ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
}

static void charCallback(GLFWwindow* const window, const unsigned int codepoint)
{
    ImGui_ImplGlfw_CharCallback(window, codepoint);
}
