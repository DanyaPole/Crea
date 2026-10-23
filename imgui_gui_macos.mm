#import <AppKit/AppKit.h>
#import <OpenGL/gl3.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_osx.h>

#include "gui_platform.h"

@interface TectonicImGuiView : NSOpenGLView {
@public
    void* pluginData;
    ImGuiContext* imguiContext;
    NSTimer* redrawTimer;
}
- (void)startRedrawTimer;
- (void)stopRedrawTimer;
@end

@implementation TectonicImGuiView

- (void)prepareOpenGL {
    [super prepareOpenGL];
    GLint swapInterval = 1;
    [self.openGLContext setValues:&swapInterval forParameter:NSOpenGLContextParameterSwapInterval];
}

- (void)startRedrawTimer {
    if (!redrawTimer) {
        redrawTimer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 60.0)
                                                      target:self
                                                    selector:@selector(requestRedraw:)
                                                    userInfo:nil
                                                     repeats:YES];
    }
}

- (void)stopRedrawTimer {
    [redrawTimer invalidate];
    redrawTimer = nil;
}

- (void)requestRedraw:(NSTimer*)timer {
    (void)timer;
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    if (!pluginData || !self.openGLContext)
        return;

    [self.openGLContext makeCurrentContext];
    ImGui::SetCurrentContext(imguiContext);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplOSX_NewFrame(self);
    ImGui::NewFrame();
    tectonicDrawGui(pluginData);
    ImGui::Render();

    const CGFloat scale = self.window ? self.window.backingScaleFactor : 1.0;
    const GLsizei width = static_cast<GLsizei>(self.bounds.size.width * scale);
    const GLsizei height = static_cast<GLsizei>(self.bounds.size.height * scale);
    glViewport(0, 0, width, height);
    glClearColor(0.025f, 0.035f, 0.065f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    [self.openGLContext flushBuffer];
}

- (void)viewWillMoveToWindow:(NSWindow*)newWindow {
    if (!newWindow)
        [self stopRedrawTimer];
    [super viewWillMoveToWindow:newWindow];
}

- (BOOL)acceptsFirstResponder { return YES; }
- (void)mouseDown:(NSEvent*)event { [[self window] makeFirstResponder:self]; [super mouseDown:event]; }
- (void)mouseUp:(NSEvent*)event { [super mouseUp:event]; }
- (void)rightMouseDown:(NSEvent*)event { [super rightMouseDown:event]; }
- (void)rightMouseUp:(NSEvent*)event { [super rightMouseUp:event]; }
- (void)otherMouseDown:(NSEvent*)event { [super otherMouseDown:event]; }
- (void)otherMouseUp:(NSEvent*)event { [super otherMouseUp:event]; }
- (void)mouseMoved:(NSEvent*)event { [super mouseMoved:event]; }
- (void)mouseDragged:(NSEvent*)event { [super mouseDragged:event]; }
- (void)rightMouseDragged:(NSEvent*)event { [super rightMouseDragged:event]; }
- (void)otherMouseDragged:(NSEvent*)event { [super otherMouseDragged:event]; }
- (void)scrollWheel:(NSEvent*)event { [super scrollWheel:event]; }
- (void)keyDown:(NSEvent*)event { [super keyDown:event]; }
- (void)keyUp:(NSEvent*)event { [super keyUp:event]; }

@end

namespace {

struct MacGuiState {
    TectonicImGuiView* view = nil;
    bool platform_backend_initialized = false;
    bool renderer_backend_initialized = false;
};

}

bool tectonicGuiAttach(void* plugin, void* parent, ImGuiContext* context, void** platform_gui) {
    if (!plugin || !parent || !context || !platform_gui)
        return false;

    NSOpenGLPixelFormatAttribute attributes[] = {
        NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
        NSOpenGLPFAAccelerated, NSOpenGLPFADoubleBuffer,
        NSOpenGLPFAColorSize, 24,
        NSOpenGLPFADepthSize, 16,
        0
    };
    NSOpenGLPixelFormat* format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
    if (!format)
        return false;

    auto* state = new MacGuiState{};
    state->view = [[TectonicImGuiView alloc] initWithFrame:NSMakeRect(0, 0, 960, 600) pixelFormat:format];
    [format release];
    if (!state->view) {
        delete state;
        return false;
    }

    state->view->pluginData = plugin;
    state->view->imguiContext = context;
    [state->view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [state->view setWantsBestResolutionOpenGLSurface:YES];
    [state->view setHidden:YES];

    [state->view.openGLContext makeCurrentContext];
    ImGui::SetCurrentContext(context);
    state->platform_backend_initialized = ImGui_ImplOSX_Init(state->view);
    if (state->platform_backend_initialized)
        state->renderer_backend_initialized = ImGui_ImplOpenGL3_Init("#version 150");
    if (!state->platform_backend_initialized || !state->renderer_backend_initialized) {
        if (state->renderer_backend_initialized)
            ImGui_ImplOpenGL3_Shutdown();
        if (state->platform_backend_initialized)
            ImGui_ImplOSX_Shutdown();
        [state->view release];
        delete state;
        return false;
    }

    [(NSView*)parent addSubview:state->view];
    *platform_gui = state;
    return true;
}

void tectonicGuiShow(void* platform_gui, bool show) {
    auto* state = static_cast<MacGuiState*>(platform_gui);
    if (!state)
        return;
    [state->view setHidden:!show];
    if (show) {
        [state->view startRedrawTimer];
        [state->view setNeedsDisplay:YES];
    } else {
        [state->view stopRedrawTimer];
    }
}

void tectonicGuiDestroy(void* platform_gui, ImGuiContext* context) {
    auto* state = static_cast<MacGuiState*>(platform_gui);
    if (!state)
        return;

    ImGui::SetCurrentContext(context);
    [state->view.openGLContext makeCurrentContext];
    if (state->renderer_backend_initialized)
        ImGui_ImplOpenGL3_Shutdown();
    if (state->platform_backend_initialized)
        ImGui_ImplOSX_Shutdown();
    [state->view removeFromSuperview];
    [state->view release];
    delete state;
}
