/*
    The MCP half of Application, switched off. USE_MCP=0 compiles this instead of
    core/ApplicationMCP.cpp - see the note at the top of that file for why the two are separate
    translation units rather than one file with an #ifdef in it.

    THE POINT OF THIS FILE IS THAT Application.cpp DOES NOT KNOW. It calls RegisterCoreMCPTools()
    and StartMCPServer() unconditionally and hands every tool result through
    MaybeAttachScreenshot(); with MCP off those three do nothing, and nothing in the rest of core
    acquires an #ifdef. A build with USE_MCP=0 also drops core/MCPServer.cpp entirely, which is
    why these cannot simply call into it.

    An app's OWN tools are a different matter and are guarded at their call sites with
    #ifdef USE_MCP - those are app objects, compiled with CFLAGS, so a define reaches them safely.
    See engine.mk's CORE_CFLAGS block for the line between the two.
*/
#include "Application.h"

void Application::RegisterCoreMCPTools(){
    //Nothing. The tools this registers - object_list, sim_step, screenshot and the rest - are a
    //debugging interface, and this is a build that does not have one.
}

void Application::StartMCPServer(){
    //Nothing, and in particular NO PORT IS BOUND. That is most of the point of USE_MCP=0: a
    //shipped build should not be listening on 127.0.0.1:8765 for something to drive it.
}

/*
    Hands back exactly what it was given.

    The signature keeps its `include_screenshot` parameter rather than dropping it, because the
    call sites are app code that compiles either way:

        return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));

    Those live inside #ifdef USE_MCP blocks today, so in this build they are not compiled at all -
    but the moment something outside MCP wants a screenshot attached to a result, this is where
    that decision is, and silently discarding the request is the honest behaviour for a build with
    no transport to send an image over.
*/
json Application::MaybeAttachScreenshot(json result, bool include_screenshot, bool include_ui){
    (void)include_screenshot;
    (void)include_ui;
    return result;
}
