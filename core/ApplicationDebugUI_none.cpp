/*
    The debug UI, switched off. USE_IMGUI=0 compiles this instead of core/ApplicationDebugUI.cpp -
    see the note at the top of that file for why the two are separate translation units rather
    than one file with an #ifdef in it.

    Every entry point here is empty. core/Application.cpp calls them unconditionally and is
    byte-identical whichever twin is linked, which is the property that lets the core objects stay
    shared between all fourteen apps.

    WHAT IS LOST IN SUCH A BUILD is worth being explicit about, because it is a lot: the menu bar,
    the Scene tree, the Inspector and its five tabs, the Engine panel with its timings and
    sliders, the shader list, and object picking by click. All of it is developer surface. None of
    it is the game. An app's own HUD is guarded separately at its own definitions (#ifdef
    USE_IMGUI) and disappears with it - anything a PLAYER needs to see has to be drawn by
    core/UIOverlay or by geometry, which is what apps/tetris already does for its score.
*/
#include <winsock2.h>
#include "Application.h"

void Application::UpdateUICameraControls(Camera* camera,int id){
    (void)camera;
    (void)id;
}

void Application::UpdateUIWorldPhysics(PhysicsWorld* physics_world){
    (void)physics_world;
}

void Application::RenderDebugMenuBar(){
}

void Application::RenderShaderUI(Shader* shader){
    (void)shader;
}

void Application::RenderApplicationUI(){
}

void Application::RenderSceneWindow(){
}

void Application::UpdateUISceneObjectTreeNode(Object* object, Object* lastclicked){
    (void)object;
    (void)lastclicked;
}

void Application::RenderInspectorWindow(){
}

void Application::RenderInspectorTransformTab(Object* object){
    (void)object;
}

void Application::RenderInspectorPhysicsTab(Object* object){
    (void)object;
}

bool Application::RenderBitmaskCheckboxes(const char* id, uint32_t& mask){
    (void)id;
    (void)mask;
    //Nothing was clicked, because nothing was drawn. Callers use the return to mean "the mask
    //changed", so false is both true and the only safe answer.
    return false;
}

void Application::RenderInspectorRenderTab(Object* object){
    (void)object;
}

void Application::RenderInspectorAnimationTab(Object* object){
    (void)object;
}

void Application::RenderInspectorDebugTab(Object* object){
    (void)object;
}

void Application::RenderEngineWindow(){
}

void Application::RenderRandTestWindow(){
}

void Application::CheckObjectSelection(){
    //Picking is a debug-UI affordance: it reads the ImGui mouse state and writes the Inspector's
    //selection. With no Inspector there is nothing to select into.
}

/*
    Does the debug UI want the mouse?

    ImGui::GetIO().WantCaptureMouse, behind a name the rest of the engine can call unconditionally.
    Game logic asks this before acting on a click, so that dragging a slider does not also swing
    the camera; with no panels on screen the honest answer is always no, and the click belongs to
    the game.
*/
bool Application::UIWantsMouse(){
    return false;
}

bool Application::UIWantsKeyboard(){
    return false;
}
