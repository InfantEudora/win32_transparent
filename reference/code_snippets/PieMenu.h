#include "imgui.h"
#include "imgui_internal.h"

// Uses key if iMouseButton==-1.
bool BeginPiePopup(const char* pName, int iMouseButton=ImGuiMouseButton_Right);
void EndPiePopup();

bool PieMenuItem(const char* pName, bool bEnabled = true);
bool BeginPieMenu(const char* pName, bool bEnabled = true);
void EndPieMenu();
void PieMenuExample();