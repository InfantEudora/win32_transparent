#ifndef _APPLICATION_MUSIC_H_
#define _APPLICATION_MUSIC_H_

#include "Application.h"
#include "MusicPlayer.h"

#include <condition_variable>
#include <mutex>
#include <string>

/*
    music - a bench for the adaptive music. See readme.md for what it is for and MusicEngine.h
    for how the music is made; this is the summary a reader of this class needs.

    It draws nothing but panels. One score plays (assets/music/jungle.json unless told otherwise),
    the "Music" panel steers it - suspense, tempo, key, stingers - and the music_* MCP tools do the
    same from outside, so an agent can tune it with the person listening. music_render writes an
    offline render to apps/music/renders/, which tools/samplescan can then measure: that is how
    the agent half of the loop hears anything.

    WHERE THE THREADS ARE. Init, PreRender and DrawImGuiUI run on the RENDER thread, and that is
    the only thread that calls SoundSystem (which has no lock) - so a reload, which restarts the
    stream, is a request the render thread services in PreRender. MCP handlers run on their own
    threads and use only MusicPlayer's thread-safe half. See MusicPlayer.h for the audio thread.
*/
class ApplicationMusic : public Application{
public:
    ApplicationMusic();

    void Init(void) override;
    void PreRender(void) override;
#ifdef USE_IMGUI
    void DrawImGuiUI(void) override;
#endif

private:
    SoundSystem* soundsystem = nullptr;
    MusicPlayer player;
    std::string score_file = "music/jungle.json";
    std::string score_error;        //what the last load said, shown on the panel

    //RENDER THREAD. Loads `file` and starts it; on failure the old score keeps playing.
    bool LoadAndPlay(const std::string& file);

    //--- reload requests from other threads ---------------------------------------------------
    std::mutex request_mutex;
    std::condition_variable request_cv;
    std::string requested_file;
    bool f_request_pending = false;
    bool f_request_ok = false;
    //Asks the render thread to (re)load a score and waits for the answer. Not for the render thread.
    bool RequestReload(const std::string& file, std::string& error, int timeout_ms = 5000);

    //--- offline render, shared by the panel and the tool -------------------------------------
    std::string RendersDirectory();
    json RenderJson(const std::string& file, double seconds, const MusicParams& p, uint32_t seed,
                    const std::vector<MusicPlayer::TimedEvent>& timeline);

    json StateJson();
    //"up a fifth" and friends: a key event from a root name, a mode name and/or a shift.
    bool KeyEventFromJson(const json& args, int current_root, MusicEvent& e, std::string& error);

#ifdef USE_IMGUI
    void RenderMusicPanel();
    float render_seconds = 30.0f;
    std::string last_render;
#endif
#ifdef USE_MCP
    void RegisterMCPTools();
#endif
};

#endif
