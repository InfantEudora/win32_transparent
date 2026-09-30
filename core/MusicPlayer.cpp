#include "MusicPlayer.h"

#include "Debug.h"

#include <cstdio>
#include <cstring>

static Debugger* debug = new Debugger("MusicPlayer", DEBUG_ALL);

ma_data_source_vtable MusicPlayer::vtable = {
    MusicPlayer::OnRead,
    MusicPlayer::OnSeek,
    MusicPlayer::OnGetDataFormat,
    NULL,       //no cursor: generated music has no position to report
    NULL,       //and no length - it never ends
    NULL,
    0
};

MusicPlayer::MusicPlayer(){
    std::memset(&source, 0, sizeof(source));
    source.owner = this;
    ma_data_source_config cfg = ma_data_source_config_init();
    cfg.vtable = &vtable;
    ma_data_source_init(&cfg, &source.base);
}

MusicPlayer::~MusicPlayer(){
    Stop();
    ma_data_source_uninit(&source.base);
}

bool MusicPlayer::Start(SoundSystem* s, std::shared_ptr<MusicScore> new_score, uint32_t seed,
                        int bus, const std::string& section, bool f_held){
    Stop();
    sound = s;
    {
        std::lock_guard<std::mutex> lock(mutex);
        score = new_score;
    }
    if (!sound || !sound->f_initialised || !score){
        debug->Warn("No sound device - the music will not play (offline renders still work)\n");
        return false;
    }
    rate = (int)sound->GetSampleRate();

    //Not running yet, so the engine is still ours to set up without the lock's help - which is
    //also what lets a section and a hold be in place before the first block is ever mixed.
    engine.Init(score.get(), rate, seed, section.empty() ? 0 : score->SectionIndex(section));
    if (f_held){
        MusicEvent e;
        e.type = MusicEvent::PAUSE;
        e.value = 1;
        e.f_now = true;
        engine.Post(e);
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        MusicParams p = params;
        p.bpm = score->bpm;             //a new score brings its own tempo; the rest carry over
        params = p;
        engine.SetParams(p);
        f_params_dirty = false;
        queue.clear();
        status = engine.Status();
    }
    handle = sound->PlayStream((ma_data_source*)&source.base, 1.0f, bus);
    if (handle == SOUND_INVALID_HANDLE){
        debug->Err("Could not start the music stream\n");
        return false;
    }
    debug->Ok("Playing '%s' at %d Hz\n", score->name.c_str(), rate);
    return true;
}

void MusicPlayer::Stop(){
    if (sound && handle != SOUND_INVALID_HANDLE){
        //Once Stop returns the mixer no longer reads `source`, so the engine and score are free.
        sound->Stop(handle);
    }
    handle = SOUND_INVALID_HANDLE;
}

//--- any thread ----------------------------------------------------------------------------

void MusicPlayer::SetParams(const MusicParams& p){
    std::lock_guard<std::mutex> lock(mutex);
    params = p;
    f_params_dirty = true;
}

MusicParams MusicPlayer::GetParams(){
    std::lock_guard<std::mutex> lock(mutex);
    return params;
}

void MusicPlayer::Post(const MusicEvent& e){
    std::lock_guard<std::mutex> lock(mutex);
    queue.push_back(e);
}

void MusicPlayer::Audition(std::shared_ptr<const MusicSample> sample){
    std::shared_ptr<const MusicSample> retired;
    {
        std::lock_guard<std::mutex> lock(mutex);
        retired = audition_keep[2];
        audition_keep[2] = audition_keep[1];
        audition_keep[1] = audition_keep[0];
        audition_keep[0] = sample;
        MusicEvent e;
        e.type = MusicEvent::AUDITION;
        e.sample = sample;
        queue.push_back(e);
    }
    //`retired` goes out of scope here, outside the lock and on this thread.
}

std::shared_ptr<const MusicScore> MusicPlayer::GetScore(){
    std::lock_guard<std::mutex> lock(mutex);
    return score;
}

MusicStatus MusicPlayer::GetStatus(){
    std::lock_guard<std::mutex> lock(mutex);
    return status;
}

//--- the audio thread ----------------------------------------------------------------------

ma_result MusicPlayer::OnRead(ma_data_source* ds, void* out, ma_uint64 frames, ma_uint64* read){
    MusicPlayer* self = ((Source*)ds)->owner;
    //try_lock, never lock - see the header. A miss costs one block of staleness.
    if (self->mutex.try_lock()){
        if (self->f_params_dirty){
            self->engine.SetParams(self->params);
            self->f_params_dirty = false;
        }
        for (const MusicEvent& e : self->queue) self->engine.Post(e);
        self->queue.clear();
        self->mutex.unlock();
    }
    self->engine.Render((float*)out, (int)frames);
    if (self->mutex.try_lock()){
        self->status = self->engine.Status();
        self->mutex.unlock();
    }
    if (read) *read = frames;
    return MA_SUCCESS;
}

ma_result MusicPlayer::OnSeek(ma_data_source* ds, ma_uint64 frame){
    (void)ds; (void)frame;
    return MA_NOT_IMPLEMENTED;
}

ma_result MusicPlayer::OnGetDataFormat(ma_data_source* ds, ma_format* format, ma_uint32* channels,
                                       ma_uint32* sample_rate, ma_channel* map, size_t map_cap){
    MusicPlayer* self = ((Source*)ds)->owner;
    *format = ma_format_f32;
    *channels = 2;
    *sample_rate = (ma_uint32)self->rate;      //the device's own, so miniaudio resamples nothing
    if (map) ma_channel_map_init_standard(ma_standard_channel_map_default, map, map_cap, 2);
    return MA_SUCCESS;
}

//--- offline -------------------------------------------------------------------------------

bool MusicPlayer::RenderToFile(const std::string& path, double seconds, const MusicParams& p, uint32_t seed,
                               const std::vector<TimedEvent>& timeline, MusicStatus& final_status, std::string& error){
    std::shared_ptr<const MusicScore> held = GetScore();
    if (!held){
        error = "no score loaded";
        return false;
    }
    const int out_rate = 48000;
    MusicEngine offline;
    offline.Init(held.get(), out_rate, seed);
    offline.SetParams(p);

    const int block = 480;          //10 ms, the size of a typical device block
    const size_t total = (size_t)(seconds * out_rate);
    std::vector<int16_t> pcm;
    pcm.reserve(total * 2);
    std::vector<float> buf(block * 2);
    size_t next_event = 0;
    for (size_t done = 0; done < total; done += block){
        const double now = done / (double)out_rate;
        while (next_event < timeline.size() && timeline[next_event].at_s <= now){
            offline.Post(timeline[next_event].event);
            next_event++;
        }
        const int n = (int)std::min<size_t>(block, total - done);
        offline.Render(buf.data(), n);
        for (int i = 0; i < n * 2; i++){
            const float v = std::max(-1.0f, std::min(1.0f, buf[i]));
            pcm.push_back((int16_t)(v * 32767.0f));
        }
    }
    final_status = offline.Status();

    FILE* f = fopen(path.c_str(), "wb");
    if (!f){
        error = "cannot write " + path;
        return false;
    }
    const uint32_t data_bytes = (uint32_t)(pcm.size() * 2);
    auto u32 = [&](uint32_t v){ fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v){ fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + data_bytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(2); u32(out_rate); u32(out_rate * 4); u16(4); u16(16);
    fwrite("data", 1, 4, f); u32(data_bytes);
    fwrite(pcm.data(), 2, pcm.size(), f);
    fclose(f);
    return true;
}
