#ifndef _APPLICATION_MUSIC_H_
#define _APPLICATION_MUSIC_H_

#include "Application.h"
#include "MusicLibrary.h"
#include "MusicPlayer.h"

#include <atomic>
#include <condition_variable>
#include <thread>
#include <mutex>
#include <string>

/*
    music - a bench for the adaptive music. See readme.md for what it is for and MusicEngine.h
    for how the music is made; this is the summary a reader of this class needs.

    It draws nothing but panels. One score plays (assets/music/jungle.json unless told otherwise),
    the "Music" panel steers it - suspense, brightness, tempo, key, stingers - and the music_* MCP
    tools do the same from outside, so an agent can tune it with the person listening. The
    "Library" panel and the library_* tools are the sample library behind the scores: what has
    been classified and how, what is new in unsorted/, and the means to audition and classify it,
    and to export it to assets/sound as a wav a score can name. music_render writes an
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
    ~ApplicationMusic();

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

    //--- the library ----------------------------------------------------------------------
    //samples/ and its catalog; see MusicLibrary.h. The Library panel and the library_* tools.
    MusicLibrary library;
    json EntryJson(const MusicLibrary::Entry& e, bool f_full);

    /*
        Plays one library file as it is, beside the music: samplescan exports it to a wav in
        renders/, and the player auditions that. BLOCKING for the export (a fraction of a second),
        so the panel runs it on `audition_thread`; the MCP tool can just wait. One at a time -
        audition_mutex - because every audition goes through the same renders/audition.wav.
    */
    bool AuditionFile(const std::string& file, std::string& error);
    std::mutex audition_mutex;
    std::thread audition_thread;
    std::atomic<bool> f_audition_busy{false};
    std::string audition_error;         //the panel's last audition failure, under audition_mutex

    //The panel's exports to assets/sound, on their own thread for the same reason: a minute-long
    //bed takes a few seconds to decode, and the panel must not stop drawing for it.
    std::thread export_thread;
    std::atomic<bool> f_export_busy{false};
    std::string export_error;           //under audition_mutex, like audition_error
    void StartPanelExport(const std::string& file, const std::string& name, bool f_trim, bool f_replace, bool f_missing);

    json StateJson();
    //"up a fifth" and friends: a key event from a root name, a mode name and/or a shift.
    bool KeyEventFromJson(const json& args, int current_root, MusicEvent& e, std::string& error);

#ifdef USE_IMGUI
    void RenderMusicPanel();
    float render_seconds = 30.0f;
    std::string last_render;

    void RenderLibraryPanel();
    int library_filter = 0;             //0 needs attention, 1 classified, 2 all
    char library_find[64] = "";
    std::string library_selected;
    //The edit fields for the selected file, loaded from it when the selection changes.
    int  edit_category = -1;            //index into MusicLibrary::Categories, -1 = none
    char edit_instrument[128] = "";
    char edit_root[16] = "";
    char edit_comment[256] = "";
    char edit_export_name[64] = "";     //sound/<this>.wav
    bool edit_export_trim = true;
    void SelectLibraryFile(const MusicLibrary::Entry& e);
#endif
#ifdef USE_MCP
    void RegisterMCPTools();
#endif
};

#endif
