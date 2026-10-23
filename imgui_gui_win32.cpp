#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <cstdint>
#include <mutex>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_win32.h>

#include "gui_platform.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

namespace {

constexpr wchar_t WINDOW_CLASS[] = L"TectonicSynthImGui";
constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091;
constexpr int WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;

using CreateContextAttribs = HGLRC(WINAPI*)(HDC, HGLRC, const int*);

struct WinGuiState {
    HWND window = nullptr;
    HDC device_context = nullptr;
    HGLRC gl_context = nullptr;
    void* plugin = nullptr;
    ImGuiContext* imgui_context = nullptr;
    bool platform_backend_initialized = false;
    bool renderer_backend_initialized = false;
    bool retained_window_class = false;
};

std::mutex windowClassMutex;
uint32_t windowClassUsers = 0;

void renderFrame(WinGuiState* state) {
    if (!state || !state->renderer_backend_initialized || !state->device_context || !state->gl_context || !wglMakeCurrent(state->device_context, state->gl_context))
        return;

    ImGui::SetCurrentContext(state->imgui_context);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    tectonicDrawGui(state->plugin);
    ImGui::Render();

    RECT client{};
    GetClientRect(state->window, &client);
    const int width = std::max(1L, client.right - client.left);
    const int height = std::max(1L, client.bottom - client.top);
    glViewport(0, 0, width, height);
    glClearColor(0.025f, 0.035f, 0.065f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SwapBuffers(state->device_context);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<WinGuiState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        state = static_cast<WinGuiState*>(create->lpCreateParams);
        state->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    if (state && state->platform_backend_initialized) {
        ImGui::SetCurrentContext(state->imgui_context);
        if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam))
            return 1;
    }

    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        renderFrame(state);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

bool retainWindowClass() {
    std::lock_guard<std::mutex> lock(windowClassMutex);
    if (windowClassUsers == 0) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = windowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = WINDOW_CLASS;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
    }
    ++windowClassUsers;
    return true;
}

HGLRC createOpenGLContext(HDC device_context) {
    HGLRC legacy_context = wglCreateContext(device_context);
    if (!legacy_context)
        return nullptr;
    if (!wglMakeCurrent(device_context, legacy_context)) {
        wglDeleteContext(legacy_context);
        return nullptr;
    }

    const PROC address = wglGetProcAddress("wglCreateContextAttribsARB");
    const uintptr_t address_value = reinterpret_cast<uintptr_t>(address);
    if (!address || address_value <= 3 || address_value == static_cast<uintptr_t>(-1)) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(legacy_context);
        return nullptr;
    }

    const auto create_context = reinterpret_cast<CreateContextAttribs>(address);
    const int attributes[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 0,
        0
    };
    HGLRC context = create_context(device_context, nullptr, attributes);
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(legacy_context);
    if (context && !wglMakeCurrent(device_context, context)) {
        wglDeleteContext(context);
        return nullptr;
    }
    return context;
}

void releaseWindowClass() {
    std::lock_guard<std::mutex> lock(windowClassMutex);
    if (windowClassUsers && --windowClassUsers == 0)
        UnregisterClassW(WINDOW_CLASS, GetModuleHandleW(nullptr));
}

} // namespace

bool tectonicGuiAttach(void* plugin, void* parent, ImGuiContext* context, void** platform_gui) {
    if (!plugin || !parent || !context || !platform_gui || !retainWindowClass())
        return false;

    auto* state = new WinGuiState{};
    state->retained_window_class = true;
    state->plugin = plugin;
    state->imgui_context = context;
    state->window = CreateWindowExW(
        0, WINDOW_CLASS, L"Tectonic Synth", WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, 960, 600, static_cast<HWND>(parent), nullptr, GetModuleHandleW(nullptr), state);
    if (!state->window) {
        tectonicGuiDestroy(state, context);
        return false;
    }

    state->device_context = GetDC(state->window);
    PIXELFORMATDESCRIPTOR pixel_format{};
    pixel_format.nSize = sizeof(pixel_format);
    pixel_format.nVersion = 1;
    pixel_format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pixel_format.iPixelType = PFD_TYPE_RGBA;
    pixel_format.cColorBits = 32;
    pixel_format.cDepthBits = 24;
    pixel_format.iLayerType = PFD_MAIN_PLANE;
    const int format = ChoosePixelFormat(state->device_context, &pixel_format);
    if (!format || !SetPixelFormat(state->device_context, format, &pixel_format)) {
        tectonicGuiDestroy(state, context);
        return false;
    }

    state->gl_context = createOpenGLContext(state->device_context);
    if (!state->gl_context) {
        tectonicGuiDestroy(state, context);
        return false;
    }

    ImGui::SetCurrentContext(context);
    state->platform_backend_initialized = ImGui_ImplWin32_InitForOpenGL(state->window);
    if (state->platform_backend_initialized)
        state->renderer_backend_initialized = ImGui_ImplOpenGL3_Init("#version 130");
    if (!state->platform_backend_initialized || !state->renderer_backend_initialized) {
        tectonicGuiDestroy(state, context);
        return false;
    }
    *platform_gui = state;
    return true;
}

void tectonicGuiShow(void* platform_gui, bool show) {
    auto* state = static_cast<WinGuiState*>(platform_gui);
    if (!state)
        return;
    ShowWindow(state->window, show ? SW_SHOW : SW_HIDE);
    if (show) {
        SetTimer(state->window, 1, 16, nullptr);
        InvalidateRect(state->window, nullptr, FALSE);
    } else {
        KillTimer(state->window, 1);
    }
}

void tectonicGuiDestroy(void* platform_gui, ImGuiContext* context) {
    auto* state = static_cast<WinGuiState*>(platform_gui);
    if (!state)
        return;
    ImGui::SetCurrentContext(context);
    if (state->gl_context && state->device_context)
        wglMakeCurrent(state->device_context, state->gl_context);
    if (state->renderer_backend_initialized) {
        ImGui_ImplOpenGL3_Shutdown();
        state->renderer_backend_initialized = false;
    }
    if (state->platform_backend_initialized) {
        ImGui_ImplWin32_Shutdown();
        state->platform_backend_initialized = false;
    }
    wglMakeCurrent(nullptr, nullptr);
    if (state->gl_context)
        wglDeleteContext(state->gl_context);
    if (state->device_context && state->window)
        ReleaseDC(state->window, state->device_context);
    if (state->window) {
        KillTimer(state->window, 1);
        DestroyWindow(state->window);
    }
    if (state->retained_window_class)
        releaseWindowClass();
    delete state;
}
