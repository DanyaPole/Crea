#pragma once

struct ImGuiContext;

bool tectonicGuiAttach(void* plugin, void* parent, ImGuiContext* context, void** platform_gui);
void tectonicGuiShow(void* platform_gui, bool show);
void tectonicGuiDestroy(void* platform_gui, ImGuiContext* context);

extern "C" void tectonicDrawGui(void* plugin);
