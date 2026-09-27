#include "MusicEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
const double kPi = 3.14159265358979323846;
const int kBeatsPerBar = 4;
const size_t kNotePool = 32;
const size_t kRecentNotes = 24;

float Lerp(float a, float b, float t){ return a + (b - a) * t; }
float ToDb(float lin){ return lin > 1e-6f ? 20.0f * std::log10(lin) : -120.0f; }
}

//--- random --------------------------------------------------------------------------------

//xorshift32: tiny, fast, and ours alone. See the note on Application::rrand in the header.
float MusicEngine::Random(){
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (rng >> 8) * (1.0f / 16777216.0f);
}

int MusicEngine::RandomInt(int n){
    return n > 0 ? std::min(n - 1, (int)(Random() * n)) : 0;
}

//--- setup ---------------------------------------------------------------------------------

void MusicEngine::Init(const MusicScore* s, int out_rate, uint32_t seed){
    score = s;
    rate = out_rate > 0 ? out_rate : 48000;
    rng = seed ? seed : 1;
    events.clear();
    clock = 0;
    beat_count = 0;
    next_beat = rate * 0.5;         //half a second of beds before the first note
    root_pc = s->root_pc;
    mode = s->mode;
    pending_root = pending_mode = -1;
    params.bpm = s->bpm;

    beds.assign(s->beds.size(), Bed());
    status.bed_names.clear();
    for (size_t i = 0; i < beds.size(); i++){
        beds[i].def = &s->beds[i];
        beds[i].transpose = BedTranspose(beds[i]);
        StartBedPlay(beds[i], 0.0, 2.0f);
        status.bed_names.push_back(s->beds[i].name);
    }
    status.bed_gains.assign(beds.size(), 0.0f);
    status.bed_transpose.assign(beds.size(), 0);

    voices.assign(s->voices.size(), Voice());
    for (size_t i = 0; i < voices.size(); i++) voices[i].def = &s->voices[i];
    notes.assign(kNotePool, Play());

    status.recent.clear();
    status.notes_total = 0;
    status.clipped = 0;
    meter_sq = 0;
    meter_peak = 0;
    meter_frames = 0;
}

//--- mixing one playing --------------------------------------------------------------------

bool MusicEngine::Mix(Play& p, float* out, int frames, int64_t block_start){
    const MusicSample& s = *p.sample;
    int i = 0;
    if (p.start_at > block_start){
        if (p.start_at >= block_start + frames) return true;     //not yet
        i = (int)(p.start_at - block_start);
    }
    const double last = (double)s.frames - 1.0;
    const int ch = s.channels;
    for (; i < frames; i++){
        if (p.pos >= last) return false;
        if (p.release_at >= 0 && block_start + i >= p.release_at && p.fade_target > 0){
            p.fade_target = 0;
            p.fade_step = -1.0f / std::max(1.0f, p.release_frames);
        }
        if (p.fade_step != 0){
            p.fade += p.fade_step;
            if ((p.fade_step > 0 && p.fade >= p.fade_target) || (p.fade_step < 0 && p.fade <= p.fade_target)){
                p.fade = p.fade_target;
                p.fade_step = 0;
            }
            if (p.fade <= 0 && p.fade_target <= 0) return false;
        }
        //Linear interpolation. Not a good resampler, and a fine one for this: every sample here
        //is soft, low-passed and repitched by less than an octave, where its aliasing is far
        //below the noise floor of the recordings themselves.
        const size_t k = (size_t)p.pos;
        const float t = (float)(p.pos - k);
        float l, r;
        if (ch == 2){
            l = s.pcm[k * 2] + (s.pcm[k * 2 + 2] - s.pcm[k * 2]) * t;
            r = s.pcm[k * 2 + 1] + (s.pcm[k * 2 + 3] - s.pcm[k * 2 + 1]) * t;
        }
        else{
            l = r = s.pcm[k] + (s.pcm[k + 1] - s.pcm[k]) * t;
        }
        const float g = p.gain * p.fade;
        out[i * 2] += l * g * p.pan_l;
        out[i * 2 + 1] += r * g * p.pan_r;
        p.pos += p.step;
    }
    return true;
}

//--- beds ----------------------------------------------------------------------------------

/*
    How far a pitched bed is shifted to put its note on its degree of the current key: the
    shortest way round, -5..+6 semitones. Shortest because a bed is a long recording, and pitch
    and speed move together here - six semitones is already 41% faster wind.
*/
int MusicEngine::BedTranspose(const Bed& b) const{
    if (b.def->root_midi < 0) return 0;
    const int target = (root_pc + b.def->degree) % 12;
    int d = ((target - b.def->root_midi % 12) % 12 + 12) % 12;
    if (d > 6) d -= 12;
    return d;
}

void MusicEngine::StartBedPlay(Bed& b, double pos, float fade_s){
    const MusicSample* s = score->Sample(b.def->sample);
    if (!s || s->frames < 2) return;
    //The quietest slot: an idle one if there is one, else whichever is furthest faded out.
    Play* slot = &b.plays[0];
    for (Play& p : b.plays){
        if (!p.f_on){ slot = &p; break; }
        if (p.fade < slot->fade) slot = &p;
    }
    *slot = Play();
    slot->f_on = true;
    slot->sample = s;
    slot->pos = pos;
    slot->step = (double)s->rate / rate * std::pow(2.0, b.transpose / 12.0);
    slot->gain = b.gain;
    if (fade_s > 0){
        slot->fade = 0;
        slot->fade_target = 1;
        slot->fade_step = 1.0f / (fade_s * rate);
    }
    else slot->fade = 1;
}

void MusicEngine::RenderBeds(float* out, int frames, int64_t block_start){
    const float coef = 1.0f - std::exp(-(float)frames / (0.7f * rate));     //0.7 s to follow suspense
    for (Bed& b : beds){
        const MusicBedDef& d = *b.def;
        const float target = Lerp(d.gain_calm, d.gain_tense, params.suspense) * params.bed_gain;
        b.gain += (target - b.gain) * coef;

        //A key change reached this bed: cross over to a transposed copy from the same point in
        //the recording, so the texture carries on and only the pitch moves.
        const int want = BedTranspose(b);
        if (want != b.transpose){
            Play* main = nullptr;
            for (Play& p : b.plays) if (p.f_on && p.fade_target > 0){ main = &p; break; }
            b.transpose = want;
            const double pos = main ? main->pos : 0.0;
            if (main){
                main->fade_target = 0;
                main->fade_step = -1.0f / (d.fade_s * rate);
                main->f_loop_spawned = true;    //it is on its way out; it hands over nothing
            }
            StartBedPlay(b, pos, d.fade_s);
        }

        for (Play& p : b.plays){
            if (!p.f_on) continue;
            p.gain = b.gain;
            //The loop seam: fade_s of OUTPUT time before the end, start the next pass from the top
            //and cross into it. The sample frames that covers depend on the playback speed.
            const double xfade = std::min((double)p.sample->frames / 3.0, d.fade_s * rate * p.step);
            if (!p.f_loop_spawned && p.fade_target > 0 && p.pos >= p.sample->frames - xfade){
                p.f_loop_spawned = true;
                p.fade_target = 0;
                p.fade_step = -1.0f / (float)(xfade / p.step);
                StartBedPlay(b, 0.0, (float)(xfade / p.step / rate));
            }
            if (!Mix(p, out, frames, block_start)) p.f_on = false;
        }
    }
}

//--- voices --------------------------------------------------------------------------------

/*
    The next note for one voice: a random walk over the current scale, inside its register.

    Mostly steps (one or two scale degrees), sometimes a repeat, now and then a leap to the root or
    fifth - which is what keeps a random walk sounding anchored in its key rather than lost in it.
    With suspense, a share of notes (score.tension_max at suspense 1) are tension notes instead:
    the flat 2nd or the tritone, nearest the last note.
*/
int MusicEngine::ChooseNote(Voice& v, bool& f_tension){
    const MusicVoiceDef& d = *v.def;
    f_tension = false;
    const int last = v.last_midi >= 0 ? v.last_midi : (d.low_midi + d.high_midi) / 2;

    if (Random() < params.suspense * score->tension_max){
        int best = -1;
        for (int m = d.low_midi; m <= d.high_midi; m++){
            const int pc = ((m - root_pc) % 12 + 12) % 12;
            if (pc != 1 && pc != 6) continue;
            if (best < 0 || std::abs(m - last) < std::abs(best - last)) best = m;
        }
        if (best >= 0){
            f_tension = true;
            return best;
        }
    }

    int cand[64];
    int n = 0;
    const std::vector<int>& iv = MusicModeIntervals(mode);
    for (int m = d.low_midi; m <= d.high_midi && n < 64; m++){
        const int pc = ((m - root_pc) % 12 + 12) % 12;
        if (std::find(iv.begin(), iv.end(), pc) != iv.end()) cand[n++] = m;
    }
    if (n == 0) return last;

    int idx = 0;
    for (int i = 1; i < n; i++) if (std::abs(cand[i] - last) < std::abs(cand[idx] - last)) idx = i;

    if (Random() < 0.12f){
        int anchors[64];
        int na = 0;
        for (int i = 0; i < n; i++){
            const int pc = ((cand[i] - root_pc) % 12 + 12) % 12;
            if (pc == 0 || pc == 7) anchors[na++] = cand[i];
        }
        if (na > 0) return anchors[RandomInt(na)];
    }
    static const int steps[] = {-2, -1, -1, -1, 0, 1, 1, 1, 2};
    idx += steps[RandomInt(9)];
    if (idx < 0) idx = -idx;                    //reflect off the edges of the register
    if (idx >= n) idx = 2 * (n - 1) - idx;
    return cand[std::max(0, std::min(n - 1, idx))];
}

void MusicEngine::StartNote(const MusicSample* s, int root_midi, int midi, float gain, float pan, int64_t start,
                            int64_t release_after, float release_s){
    if (!s || s->frames < 2) return;
    //An idle slot, else steal the note that started longest ago.
    Play* slot = &notes[0];
    for (Play& p : notes){
        if (!p.f_on){ slot = &p; break; }
        if (p.start_at < slot->start_at) slot = &p;
    }
    *slot = Play();
    slot->f_on = true;
    slot->sample = s;
    slot->step = (double)s->rate / rate * std::pow(2.0, (midi - root_midi) / 12.0);
    slot->fade = 1;
    slot->gain = gain;
    //Equal-power pan, scaled so the centre is unity rather than -3 dB.
    const double angle = (std::max(-1.0f, std::min(1.0f, pan)) + 1.0) * kPi / 4.0;
    slot->pan_l = (float)(std::cos(angle) * std::sqrt(2.0));
    slot->pan_r = (float)(std::sin(angle) * std::sqrt(2.0));
    slot->start_at = start;
    slot->release_at = release_after >= 0 ? start + release_after : -1;
    slot->release_frames = release_s * rate;
}

void MusicEngine::OnBeat(int64_t beat_frame){
    if (beat_count % kBeatsPerBar == 0 && (pending_root >= 0 || pending_mode >= 0)){
        ApplyKey(pending_root, pending_mode);
    }
    const double beat_len = 60.0 * rate / std::max(20.0f, params.bpm);
    for (Voice& v : voices){
        const MusicVoiceDef& d = *v.def;
        if (Random() >= Lerp(d.density_calm, d.density_tense, params.suspense)) continue;
        int64_t start = beat_frame;
        if (Random() < d.offbeat) start += (int64_t)(beat_len / 2);
        start += (int64_t)(Random() * 0.02f * rate);     //up to 20 ms late, so no two land together
        bool f_tension = false;
        const int midi = ChooseNote(v, f_tension);
        v.last_midi = midi;
        const float gain = d.gain * params.voice_gain * (0.6f + 0.4f * Random());
        const float pan = (Random() * 2.0f - 1.0f) * d.spread;
        const int64_t release = d.length_beats > 0 ? (int64_t)(d.length_beats * beat_len) : -1;
        StartNote(score->Sample(d.sample), d.root_midi, midi, gain, pan, start, release, d.release_s);

        status.notes_total++;
        status.recent.push_back({start / (double)rate, d.name, midi, f_tension});
        if (status.recent.size() > kRecentNotes) status.recent.erase(status.recent.begin());
    }
}

void MusicEngine::ApplyKey(int new_root, int new_mode){
    if (new_root >= 0) root_pc = new_root % 12;
    if (new_mode >= 0 && new_mode < MUSIC_MODE_COUNT) mode = new_mode;
    pending_root = pending_mode = -1;
    //Beds pick the new key up on their next block (RenderBeds compares transpositions); voices on
    //their next note, which walks from wherever they were to the nearest note of the new scale.
}

//--- the block -----------------------------------------------------------------------------

void MusicEngine::Render(float* out, int frames){
    std::memset(out, 0, sizeof(float) * 2 * frames);
    if (!score) return;

    for (const MusicEvent& e : events){
        if (e.type == MusicEvent::KEY){
            if (e.f_now) ApplyKey(e.root_pc, e.mode);
            else{
                if (e.root_pc >= 0) pending_root = e.root_pc;
                if (e.mode >= 0) pending_mode = e.mode;
            }
        }
        else if (e.type == MusicEvent::SUSPENSE){
            params.suspense = std::max(0.0f, std::min(1.0f, e.value));
        }
        else{
            for (const MusicStingerDef& d : score->stingers){
                if (d.name != e.name) continue;
                StartNote(score->Sample(d.sample), 60, 60, d.gain, 0.0f, (int64_t)clock, -1, 0.3f);
            }
        }
    }
    events.clear();

    //Cut the block at every beat, so a beat's notes are scheduled before the frames they start on
    //are mixed. A note scheduled off the beat just starts partway into a later block.
    int done = 0;
    while (done < frames){
        const int64_t at = (int64_t)clock + done;
        if ((double)at >= next_beat){
            OnBeat((int64_t)next_beat);
            beat_count++;
            next_beat += 60.0 * rate / std::max(20.0f, params.bpm);
        }
        int n = frames - done;
        const int64_t to_beat = (int64_t)std::ceil(next_beat) - at;
        if (to_beat > 0 && to_beat < n) n = (int)to_beat;

        float* o = out + done * 2;
        RenderBeds(o, n, at);
        for (Play& p : notes){
            if (p.f_on && !Mix(p, o, n, at)) p.f_on = false;
        }
        done += n;
    }

    /*
        Master gain, then a soft limiter above 0.8: transparent below it, and a curve into full
        scale above it rather than a hard clip. Counted when it has real work to do (over 1.0 in),
        so a score that keeps it busy says so instead of quietly squashing.
    */
    for (int i = 0; i < frames * 2; i++){
        float y = out[i] * params.master;
        const float a = std::fabs(y);
        if (a > 1.0f) status.clipped++;
        if (a > 0.8f) y = (y > 0 ? 1.0f : -1.0f) * (0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f));
        out[i] = y;
        meter_sq += (double)y * y;
        meter_peak = std::max(meter_peak, std::fabs(y));
    }
    meter_frames += frames;
    if (meter_frames >= rate){
        status.rms_db = ToDb((float)std::sqrt(meter_sq / (meter_frames * 2.0)));
        status.peak_db = ToDb(meter_peak);
        meter_sq = 0;
        meter_peak = 0;
        meter_frames = 0;
    }

    clock += frames;
    status.time_s = clock / (double)rate;
    status.bar = beat_count / kBeatsPerBar;
    status.beat = beat_count % kBeatsPerBar;
    status.root_pc = root_pc;
    status.mode = mode;
    status.pending_root_pc = pending_root;
    status.pending_mode = pending_mode;
    for (size_t i = 0; i < beds.size(); i++){
        status.bed_gains[i] = beds[i].gain;
        status.bed_transpose[i] = beds[i].transpose;
    }
    status.notes_sounding = 0;
    for (const Play& p : notes) if (p.f_on && p.start_at <= (int64_t)clock) status.notes_sounding++;
}
