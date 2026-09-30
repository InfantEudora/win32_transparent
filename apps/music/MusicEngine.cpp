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
    section = 0;
    pending_section = -1;
    section_bar = 0;
    //A reload starts from the top, so it starts playing too: a paused score is not the new one.
    f_paused = false;
    pause_pos = 1;
    //Status is written by Render, so the first status after a reload would otherwise still say
    //where the OLD score had got to.
    status.time_s = 0;
    status.f_paused = status.f_held = false;
    status.pause_gain = 1;
    status.bar = status.beat = 0;
    status.root_pc = root_pc;
    status.mode = mode;
    status.pending_root_pc = status.pending_mode = -1;
    status.section = 0;
    status.pending_section = -1;
    status.section_bar = 0;
    status.section_bars = s->sections.empty() ? 0 : s->sections[0].bars;
    status.section_names.clear();
    for (const MusicSectionDef& d : s->sections) status.section_names.push_back(d.name);

    beds.assign(s->beds.size(), Bed());
    status.bed_names.clear();
    status.bed_heights.clear();
    status.bed_sections.clear();
    for (size_t i = 0; i < beds.size(); i++){
        Bed& b = beds[i];
        b.def = &s->beds[i];
        b.transpose = BedTranspose(b);
        b.presence = InSection(b.def->section) ? 1.0f : 0.0f;
        status.bed_sections.push_back(b.def->section);
        //A pitched bed sits where its note is; an unpitched one where its brightness is.
        const MusicSample* sample = s->Sample(b.def->sample);
        if (b.def->height >= 0) b.height = b.def->height;
        else if (b.def->root_midi >= 0) b.height = HeightFromMidi((float)b.def->root_midi);
        else if (sample && sample->zcr_hz > 0) b.height = HeightFromMidi(69.0f + 12.0f * std::log2(sample->zcr_hz / 440.0f));
        if (b.presence > 0) StartBedPlay(b, 0.0, 2.0f);
        status.bed_names.push_back(s->beds[i].name);
        status.bed_heights.push_back(b.height);
    }
    status.bed_gains.assign(beds.size(), 0.0f);
    status.bed_transpose.assign(beds.size(), 0);

    voices.assign(s->voices.size(), Voice());
    status.voice_names.clear();
    status.voice_heights.clear();
    status.voice_sections.clear();
    for (size_t i = 0; i < voices.size(); i++){
        Voice& v = voices[i];
        v.def = &s->voices[i];
        v.height = v.def->height >= 0 ? v.def->height : HeightFromMidi(0.5f * (v.def->low_midi + v.def->high_midi));
        status.voice_names.push_back(v.def->name);
        status.voice_sections.push_back(v.def->section);
        status.voice_heights.push_back(v.height);
    }
    notes.assign(kNotePool, Play());
    audition = Play();
    audition_sample.reset();
    tilt_lp[0] = tilt_lp[1] = 0;

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

//A bed's presence as heard: a quarter sine, so a bed going out and one coming in sum to the same
//power all the way through the crossfade rather than dipping 3 dB in the middle.
static float Heard(float presence){
    return (float)std::sin(presence * kPi * 0.5);
}

void MusicEngine::RenderBeds(float* out, int frames, int64_t block_start){
    const float coef = 1.0f - std::exp(-(float)frames / (0.7f * rate));     //0.7 s to follow suspense
    const float section_step = frames / (score->section_fade_s * rate);
    for (Bed& b : beds){
        const float want = InSection(b.def->section) ? 1.0f : 0.0f;
        if (b.presence < want) b.presence = std::min(want, b.presence + section_step);
        else if (b.presence > want) b.presence = std::max(want, b.presence - section_step);
    }
    //Brightness re-weights the beds against each other, not the level of the whole: scaled back so
    //their summed energy is what it is at 0.5. Without this both ends of the slider came out 4-6 dB
    //louder, into the limiter, because the part being lifted is often the one already loudest.
    float e_scored = 0, e_weighted = 0;
    for (const Bed& b : beds){
        const float g = Lerp(b.def->gain_calm, b.def->gain_tense, params.suspense) * Heard(b.presence);
        const float w = HeightGain(b.height);
        e_scored += g * g;
        e_weighted += g * g * w * w;
    }
    const float compensate = e_weighted > 1e-9f ? std::sqrt(e_scored / e_weighted) : 1.0f;
    for (Bed& b : beds){
        const MusicBedDef& d = *b.def;
        const float target = Lerp(d.gain_calm, d.gain_tense, params.suspense) * params.bed_gain * HeightGain(b.height) * compensate;
        b.gain += (target - b.gain) * coef;

        //Out of the section and faded all the way: stop, so it costs nothing until it is back.
        if (b.presence <= 0){
            for (Play& p : b.plays) p.f_on = false;
            continue;
        }
        //Coming back: from the top of the recording, in the key as it is now. The presence ramp is
        //its fade-in, so the play itself starts at full.
        bool f_any = false;
        for (const Play& p : b.plays) f_any |= p.f_on;
        if (!f_any){
            b.transpose = BedTranspose(b);
            StartBedPlay(b, 0.0, 0.0f);
        }
        const float heard = Heard(b.presence);

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
            p.gain = b.gain * heard;
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

    /*
        Brightness narrows the register towards one end of it: the upper half at 1, the lower half
        at 0, all of it at 0.5. Inside the register the score gave, so a bright kalimba climbs to
        its top notes rather than being dragged somewhere its sample was never meant to go.
    */
    const float t = 2.0f * params.brightness - 1.0f;
    const int span = d.high_midi - d.low_midi;
    const int low = d.low_midi + (t > 0 ? (int)std::lround(span * t * 0.5f) : 0);
    const int high = d.high_midi + (t < 0 ? (int)std::lround(span * t * 0.5f) : 0);
    const int last = v.last_midi >= 0 ? v.last_midi : (low + high) / 2;

    if (Random() < params.suspense * score->tension_max){
        int best = -1;
        for (int m = low; m <= high; m++){
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
    for (int m = low; m <= high && n < 64; m++){
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
    if (beat_count % kBeatsPerBar == 0){
        if (pending_root >= 0 || pending_mode >= 0) ApplyKey(pending_root, pending_mode);
        if (!score->sections.empty()){
            if (beat_count > 0) section_bar++;
            const int bars = score->sections[section].bars;
            if (pending_section != -1) ApplySection(pending_section == -2 ? NextSection() : pending_section);
            else if (bars > 0 && section_bar >= bars) ApplySection(NextSection());
        }
    }
    const double beat_len = 60.0 * rate / std::max(20.0f, params.bpm);
    //The beds' compensation, for voices: energy per beat is gain squared times how often it plays.
    float e_scored = 0, e_weighted = 0;
    for (const Voice& v : voices){
        if (!InSection(v.def->section)) continue;
        const float energy = v.def->gain * v.def->gain * Lerp(v.def->density_calm, v.def->density_tense, params.suspense);
        const float w = HeightGain(v.height);
        e_scored += energy;
        e_weighted += energy * w * w;
    }
    const float compensate = e_weighted > 1e-9f ? std::sqrt(e_scored / e_weighted) : 1.0f;
    for (Voice& v : voices){
        const MusicVoiceDef& d = *v.def;
        if (!InSection(d.section)) continue;
        if (Random() >= Lerp(d.density_calm, d.density_tense, params.suspense)) continue;
        int64_t start = beat_frame;
        if (Random() < d.offbeat) start += (int64_t)(beat_len / 2);
        start += (int64_t)(Random() * 0.02f * rate);     //up to 20 ms late, so no two land together
        bool f_tension = false;
        const int midi = ChooseNote(v, f_tension);
        v.last_midi = midi;
        const float gain = d.gain * params.voice_gain * HeightGain(v.height) * compensate * (0.6f + 0.4f * Random());
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

int MusicEngine::NextSection(){
    //Only the sections in the rotation: one asked for by name (a cave, the title) is never where
    //the music wanders to by itself, and "next" from inside one goes back to the rotation.
    std::vector<int> rotation;
    for (int i = 0; i < (int)score->sections.size(); i++) if (score->sections[i].f_rotation) rotation.push_back(i);
    if (rotation.empty()) return section;
    if (!score->f_section_random){
        for (int i : rotation) if (i > section) return i;
        return rotation[0];
    }
    //Any but the one playing: a repeat would be no change at all.
    std::vector<int> others;
    for (int i : rotation) if (i != section) others.push_back(i);
    if (others.empty()) return section;
    return others[RandomInt((int)others.size())];
}

void MusicEngine::ApplySection(int s){
    if (s >= 0 && s < (int)score->sections.size()) section = s;
    section_bar = 0;
    pending_section = -1;
    //RenderBeds sees the new section on its next block and starts the crossfade; OnBeat rolls only
    //the new section's voices from the next beat on.
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
        else if (e.type == MusicEvent::BRIGHTNESS){
            params.brightness = std::max(0.0f, std::min(1.0f, e.value));
        }
        else if (e.type == MusicEvent::SECTION){
            if (score->sections.empty()) continue;
            const int want = e.name.empty() ? -2 : score->SectionIndex(e.name);
            if (want == -1) continue;               //no such section; the tools check names before posting
            if (e.f_now) ApplySection(want == -2 ? NextSection() : want);
            else pending_section = want;
        }
        else if (e.type == MusicEvent::PAUSE){
            f_paused = e.value > 0.5f;
        }
        else if (e.type == MusicEvent::AUDITION){
            audition = Play();
            audition_sample = e.sample;
            if (audition_sample && audition_sample->frames > 1){
                audition.f_on = true;
                audition.sample = audition_sample.get();
                audition.step = (double)audition_sample->rate / rate;
                audition.fade = 1;
                audition.gain = 1;
                audition.start_at = (int64_t)clock;
            }
        }
        else{
            for (const MusicStingerDef& d : score->stingers){
                if (d.name != e.name) continue;
                StartNote(score->Sample(d.sample), 60, 60, d.gain, 0.0f, (int64_t)clock, -1, 0.3f);
            }
        }
    }
    events.clear();

    /*
        HELD: the pause has faded all the way out, so nothing of the music runs - not the clock,
        the beat, the beds, the notes or the tilt filter's state. Skipping it all, rather than
        rendering and throwing it away, is what makes a resume carry on from the very sample the
        fade ended on. Only the audition and the meter below go on.
    */
    const bool f_hold = f_paused && pause_pos <= 0.0f;
    if (!f_hold) RenderMusic(out, frames);

    //The audition goes in AFTER the tilt: it is the file as recorded, the thing being judged.
    if (audition.f_on && !Mix(audition, out, frames, (int64_t)clock)) audition.f_on = false;

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

    if (!f_hold) clock += frames;
    status.f_paused = f_paused;
    status.f_held = f_paused && pause_pos <= 0.0f;
    status.pause_gain = PauseCurve(pause_pos);
    status.time_s = clock / (double)rate;
    status.bar = beat_count / kBeatsPerBar;
    status.beat = beat_count % kBeatsPerBar;
    status.root_pc = root_pc;
    status.mode = mode;
    status.pending_root_pc = pending_root;
    status.pending_mode = pending_mode;
    status.section = section;
    status.pending_section = pending_section;
    status.section_bar = section_bar;
    status.section_bars = score->sections.empty() ? 0 : score->sections[section].bars;
    for (size_t i = 0; i < beds.size(); i++){
        status.bed_gains[i] = beds[i].presence > 0 ? beds[i].gain * Heard(beds[i].presence) : 0.0f;
        status.bed_transpose[i] = beds[i].transpose;
    }
    status.notes_sounding = 0;
    for (const Play& p : notes) if (p.f_on && p.start_at <= (int64_t)clock) status.notes_sounding++;
    if (audition.f_on && audition_sample){
        if (status.auditioning != audition_sample->name) status.auditioning = audition_sample->name;
    }
    else if (!status.auditioning.empty()) status.auditioning.clear();
}

/*
    The pause fade as heard: a raised cosine of its linear position, so it leaves full level and
    arrives at silence both at zero slope. Not the sections' Heard(), which is a sine for an
    equal-power CROSSFADE - two sounds summing - and reaches zero at full slope: fine when
    another sound is coming up underneath, an audible drop to nothing when there is none.
*/
float MusicEngine::PauseCurve(float pos){
    pos = std::max(0.0f, std::min(1.0f, pos));
    return 0.5f - 0.5f * (float)std::cos(pos * kPi);
}

void MusicEngine::RenderMusic(float* out, int frames){
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
        The brightness tilt: a one-pole split at 500 Hz, and the band on the far side of the slider
        CUT, by up to 6 dB - the highs when dark, the lows when bright. Cut only, never boost: a
        boost lands on whatever band holds the energy, and a first version that boosted one band as
        it cut the other pushed the dark end 5 dB louder. Exactly flat at 0.5, where both gains
        are 1 and the two bands sum back to the input.
    */
    const float tilt = 2.0f * params.brightness - 1.0f;
    if (std::fabs(tilt) > 1e-3f){
        const float a = 1.0f - std::exp(-2.0f * (float)kPi * 500.0f / rate);
        const float g_low = tilt > 0 ? std::pow(10.0f, -6.0f * tilt / 20.0f) : 1.0f;
        const float g_high = tilt < 0 ? std::pow(10.0f, 6.0f * tilt / 20.0f) : 1.0f;
        for (int i = 0; i < frames; i++){
            for (int c = 0; c < 2; c++){
                float& x = out[i * 2 + c];
                tilt_lp[c] += a * (x - tilt_lp[c]);
                x = tilt_lp[c] * g_low + (x - tilt_lp[c]) * g_high;
            }
        }
    }
    else{
        //Kept following the signal while flat, so moving the slider off 0.5 does not click.
        const float a = 1.0f - std::exp(-2.0f * (float)kPi * 500.0f / rate);
        for (int i = 0; i < frames; i++) for (int c = 0; c < 2; c++) tilt_lp[c] += a * (out[i * 2 + c] - tilt_lp[c]);
    }

    /*
        The pause fade, per frame, so it is as smooth at a 10 ms block as at a 100 ms one. Untouched
        at full level - not even a multiply by one - so outside a fade the output is exactly what
        it was before pausing existed. It can end partway into a block: the rest of that block is
        silent, and the next is held.
    */
    if (f_paused || pause_pos < 1.0f){
        const float seconds = std::max(0.01f, f_paused ? params.pause_fade_s : params.resume_fade_s);
        const float step = (f_paused ? -1.0f : 1.0f) / (seconds * rate);
        for (int i = 0; i < frames; i++){
            pause_pos = std::max(0.0f, std::min(1.0f, pause_pos + step));
            const float g = PauseCurve(pause_pos);
            out[i * 2] *= g;
            out[i * 2 + 1] *= g;
        }
    }
}

//--- brightness ----------------------------------------------------------------------------

//C2 is the bottom and C6 the top: below C2 is rumble, above C6 is air, and everything scored
//here lives between them.
float MusicEngine::HeightFromMidi(float midi){
    return std::max(0.0f, std::min(1.0f, (midi - 36.0f) / 48.0f));
}

/*
    What brightness does to one part's level: up to 9 dB either way, for the parts at the very
    top and bottom, and nothing for a part in the middle or at brightness 0.5. dB rather than a
    straight factor so dark and bright are symmetric - a part turned down 9 dB at 0 comes back up
    by the same 9 dB at 1.
*/
float MusicEngine::HeightGain(float height) const{
    const float db = 9.0f * (2.0f * params.brightness - 1.0f) * (2.0f * height - 1.0f);
    return std::pow(10.0f, db / 20.0f);
}
