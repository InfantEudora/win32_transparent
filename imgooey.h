
#ifndef _IMGOOEY_H_
#define _IMGOOEY_H_

//Requires #define IMGUI_DEFINE_MATH_OPERATORS to be set.

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"


typedef int ImGooyItemFlags;             // -> enum ImGooyItemFlags_
enum ImGooyItemFlags_
{
    ImGooyItemFlag_None         = 0,
    ImGooyItemFlag_Greyed       = 1 << 0,
    ImGooyItemFlag_Highlight    = 1 << 1,   //Makes the border white
    ImGooyItemFlag_Corner       = 1 << 2,
    ImGooyItemFlag_Fail         = 1 << 3,
};

typedef int ImGooyStatus;             // -> enum ImGooyStatus_
enum ImGooyStatus_
{
    ImGooyStatusFlag_None = 0,
    ImGooyStatusFlag_Ok = 1,
    ImGooyStatusFlag_Warn = 2,
    ImGooyStatusFlag_Fail = 3,
};


namespace ImGooey{
    IMGUI_API bool CustomButton(const char* label, const ImVec2& size_arg = ImVec2(0, 0), ImGooyItemFlags gflags = 0, ImGuiButtonFlags bflags = 0, ImGuiItemFlags iflags = 0);
    IMGUI_API bool StatusLabel(const char* label,const ImVec2& size_arg, ImGooyStatus status, float animation = 0.0f);
    IMGUI_API bool Begin(const char* name, bool* p_open, ImGuiWindowFlags flags);
};

#include <string>
#include <vector>

struct ComponentState{
    ImGooyStatus status;
    std::string name;
    float percentage;
};

struct Operation{
    std::string name;
    float animation = 0;
};

struct Component{
    std::string name;
    std::vector<ComponentState>states;
    bool expanded = false; //If selected for a new operation
    float animation = 0;

    void AddState(const char* string, float perc, ImGooyStatus status);
    bool HasStatus(ImGooyStatus status);
    ComponentState* GetMainState();
    std::vector<ComponentState>& GetStates();
    int NumStates();
};

//Tree state thing as exampled in ui example:


//Contains a selectable list of items that can be tabbed throug.
//Only on item in one level can be active at the same time.
//And we want some stuff done per level.

//1rst level will list items and show a simple state when selected.
//You can roll through that list with arrow keys.
//The list should fade out maybe at the top or bottom.
/*
    Item
    State (Percentage, Operational Degraded etc.)

*/



#endif