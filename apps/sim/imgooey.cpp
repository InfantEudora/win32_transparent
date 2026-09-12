#include "imgooey.h"

namespace ImGooey{
    //Collection of modded ImGui stuff.



    // Render a rectangle shaped with optional rounding and borders
    void RenderFrameCorner(ImVec2 p_min, ImVec2 p_max, ImU32 fill_col, bool border, ImGooyItemFlags gflags){
        ImDrawFlags flags = ImDrawFlags_None;
        float rounding = 0.0f;
        if (gflags & ImGooyItemFlag_Corner){
            flags |=  ImDrawFlags_RoundCornersBottomRight;
            rounding = 5.0f;
        }
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        window->DrawList->AddRectFilled(p_min, p_max, fill_col, rounding,flags);
        float border_size = g.Style.FrameBorderSize;
        border_size = 1.0f;
        if (border && border_size > 0.0f){
            ImColor col = ImGui::GetColorU32(ImGuiCol_Border);
            if (gflags & ImGooyItemFlag_Highlight){
                col = ImGui::GetColorU32(ImVec4(1,1,1,1));
            }
            window->DrawList->AddRect(p_min + ImVec2(1, 1), p_max + ImVec2(1, 1), ImGui::GetColorU32(ImGuiCol_BorderShadow), rounding, flags, border_size);
            window->DrawList->AddRect(p_min, p_max, col, rounding, flags, border_size);
        }
    }

    IMGUI_API  bool Button2Ex(const char* label, const ImVec2& size_arg, ImGooyItemFlags gflags, ImGuiButtonFlags buttonflags, ImGuiItemFlags itemflags){
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

        ImVec2 pos = window->DC.CursorPos;
        if ((buttonflags & ImGuiButtonFlags_AlignTextBaseLine) && style.FramePadding.y < window->DC.CurrLineTextBaseOffset) // Try to vertically align buttons that are smaller/have no padding so that text baseline matches (bit hacky, since it shouldn't be a flag)
            pos.y += window->DC.CurrLineTextBaseOffset - style.FramePadding.y;
        ImVec2 size = ImGui::CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

        const ImRect bb(pos, pos + size);
        ImGui::ItemSize(size, style.FramePadding.y);
        if (!ImGui::ItemAdd(bb, id,NULL,itemflags))
            return false;
        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, buttonflags);

        // Render but override style with gflags
        ImU32 col;
        col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        if (gflags & ImGooyItemFlag_Fail){
            col = ImGui::GetColorU32(ImVec4(0.2,0,0,1));
        }
        if (gflags & ImGooyItemFlag_Greyed){
            col = ImGui::GetColorU32(ImVec4(0,0,0,1));
        }
        ImGui::RenderNavCursor(bb, id);
        RenderFrameCorner(bb.Min, bb.Max, col, true, gflags);

        if (g.LogEnabled)
            ImGui::LogSetNextTextDecoration("[", "]");

        //Text color
        if (gflags & ImGooyItemFlag_Fail){
            col = ImGui::GetColorU32(ImVec4(1.0,0.0,0.0,1));
        }else if (gflags & ImGooyItemFlag_Greyed){
            col = ImGui::GetColorU32(ImVec4(0.5,0.5,0.5,1));
        }else{
            col = ImGui::GetColorU32(ImGuiCol_Text);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, label, NULL, &label_size, style.ButtonTextAlign, &bb);
        ImGui::PopStyleColor();

        // Automatically close popups
        //if (pressed && !(flags & ImGuiButtonFlags_DontClosePopups) && (window->Flags & ImGuiWindowFlags_Popup))
        //    CloseCurrentPopup();

        IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
        return pressed;
    }

    IMGUI_API bool CustomButton(const char* label, const ImVec2& size_arg, ImGooyItemFlags gflags, ImGuiButtonFlags buttonflags, ImGuiItemFlags itemflags){
        bool res = Button2Ex(label, size_arg,gflags, buttonflags,itemflags);
        return res;
    }

    //Will use text so not selectable or anything.
    IMGUI_API bool StatusLabel(const char* label,const ImVec2& size_arg, ImGooyStatus status, float animation){
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

        ImVec2 pos = window->DC.CursorPos;
        pos.x += 3 + animation;
        //if ((buttonflags & ImGuiButtonFlags_AlignTextBaseLine) && style.FramePadding.y < window->DC.CurrLineTextBaseOffset) // Try to vertically align buttons that are smaller/have no padding so that text baseline matches (bit hacky, since it shouldn't be a flag)
        //    pos.y += window->DC.CurrLineTextBaseOffset - style.FramePadding.y;
        ImVec2 size = ImGui::CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);
        size.x -= 6;

        const ImRect bb(pos, pos + size);
        ImGui::ItemSize(size, style.FramePadding.y);
        if (!ImGui::ItemAdd(bb, id,NULL,0))
            return false;
        bool hovered, held;

        // Render but override style with gflags
        ImU32 col;
        col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        //if (gflags & ImGooyItemFlag_Greyed){
            col = ImGui::GetColorU32(ImVec4(0,0,0,1));
        //}
        ImGui::RenderNavHighlight(bb, id);
        RenderFrameCorner(bb.Min, bb.Max, col, true, ImGooyItemFlag_Corner | ImGooyItemFlag_Highlight);

        if (g.LogEnabled)
            ImGui::LogSetNextTextDecoration("[", "]");

        //TODO: This needs different colors.
        if (status == ImGooyStatusFlag_Ok){
            col = ImGui::GetColorU32(ImVec4(0,1,0,1));
        }else if (status == ImGooyStatusFlag_Fail){
            col = ImGui::GetColorU32(ImVec4(1,0,0,1));
        }else{
            col = ImGui::GetColorU32(ImVec4(1,1,1,1));
        }
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, label, NULL, &label_size, style.ButtonTextAlign, &bb);
        ImGui::PopStyleColor();

        // Automatically close popups
        //if (pressed && !(flags & ImGuiButtonFlags_DontClosePopups) && (window->Flags & ImGuiWindowFlags_Popup))
        //    CloseCurrentPopup();

        IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
        return true;
    }

    // ImageButton() is flawed as 'id' is always derived from 'texture_id' (see #2464 #1390)
    // We provide this internal helper to write your own variant while we figure out how to redesign the public ImageButton() API.
    bool StorageButton(ImGuiID id, ImTextureID texture_id, const ImVec2& image_size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& bg_col, const ImVec4& tint_col, ImGuiButtonFlags flags){
        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        const char* label = "Items!";

        const ImVec2 padding = g.Style.FramePadding;
        const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + image_size + padding * 2.0f);
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, id))
            return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);

        // Render
        const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        ImGui::RenderNavHighlight(bb, id);
        ImGui::RenderFrame(bb.Min, bb.Max, col, true, ImClamp((float)ImMin(padding.x, padding.y), 0.0f, g.Style.FrameRounding));
        if (bg_col.w > 0.0f)
            window->DrawList->AddRectFilled(bb.Min + padding, bb.Max - padding, ImGui::GetColorU32(bg_col));
        window->DrawList->AddImage(texture_id, bb.Min + padding, bb.Max - padding, uv0, uv1, ImGui::GetColorU32(tint_col));

        //We want the text displayed in a corner, to show the numer of items.
        ImGui::RenderText(bb.Min + style.FramePadding, label, NULL);

        return pressed;
    }

    //Starts our window with some default mangled stuff, mainly the titlebar
    bool Begin(const char* name, bool* p_open, ImGuiWindowFlags flags){
        bool res = ImGui::Begin(name,p_open,flags);
        if (!res)
            return false;

        StatusLabel(name,ImVec2(240,0),ImGooyStatusFlag_None);
        ImGui::Separator();
        return res;
    }
}

ComponentState* Component::GetMainState(){
    if (states.size() > 0){
        return &states.at(0);
    }
    return NULL;
}

void Component::AddState(const char* string, float perc, ImGooyStatus status){
    ComponentState s;
    s.name = string;
    s.percentage = perc;
    s.status = status;
    states.push_back(s);
}

//Returns true if one of the components states have matching status.
bool Component::HasStatus(ImGooyStatus status){
    for (ComponentState s:states){
        if (s.status == status){
            return true;
        }
    }
    return false;
}

int Component::NumStates(){
    return states.size();
}
