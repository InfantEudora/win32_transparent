#include "ApplicationChasm.h"
#include "Debug.h"
#include "File.h"           //FileWriteTime - the cue table's poll

#include <algorithm>
#include <cmath>
#include <thread>

#ifdef USE_SOUND
//Only here, and only with sound: the one file that names both the cues and SoundSystem.
#include "CueSoundOutput.h"
#endif
#ifdef USE_MCP
#include "MCPServer.h"
#endif

static Debugger* debug = new Debugger("ChasmSound",DEBUG_ALL);

/*
    SOUND (docs/sound_plan.md, "As built").

    WHAT THE GAME SOUNDS LIKE IS NOT IN THIS FILE. It is assets/cues/chasm.json, which says for each signal what
    plays, how loud, how often and on which bus (the format is core/CueSystem.h's). This file only reports what
    happened, by signal name:
      - the rules' EVENTS (Economy.h), each by EconomyEventName - "born", "tree_felled" - with its place and amount,
        whether or not the table has a row for it yet, so a sound for one is a row and no code;
      - the player's commands answered: "placed", "erased", "refused";
      - the HUD: "click";
      - THE PEOPLE'S VOICES: a person picked out or given an order answers - "picked", "sent" and the rest (Say);
      - WORK IN HAND: "chop", "hammer", "chisel", "saw" - a blow at a time, from clocks of the workers' own (below).

    ON THE PHYSICS THREAD, ONCE A PASS, paused or not (UpdateSound, from the top of UpdateView). The cues' clock is
    the count of passes, which is real time: 4x is four ticks inside one pass, so a blow keeps its rhythm and a
    sound's length is still its length at any speed, and a pass still runs in a pause, so a click answers there. It
    gives up what archer has, sound that a replay repeats exactly - nothing here is a rule, so no state, hash or
    replay changes with it (sound_plan.md, "Where it runs").
*/

#define CHASM_CUE_TABLE             "cues/chasm.json"
#define CHASM_SOUND_LAYER           "sounds"    //the table's layer bus: its buses are "sounds/<name>" (CueSoundOutput)

//THE LISTENER hears from over the view's centre, this share of the camera's distance up: zooming out turns everything
//down, and a blow under the middle of a close view is near, not the whole way to the camera's eye.
#define SOUND_LISTENER_HEIGHT       0.5f
//THE VOICES: one person answering is panned by where he stands, `side` -1 to 1 this far either side of the view's
//middle; a pair is -1 and 1. A box of a hundred keeps the first few asked, of whom two answer.
#define SOUND_VOICE_SIDE_REACH      30.0f
#define SOUND_VOICE_ASKED           16
//The people's work stops when the world does - paused, the menu, the title, the statistics - and fades over this,
//rather than stopping dead or ringing on; and back in over the other.
#define SOUND_PAUSE_FADE_S          0.6f
#define SOUND_RESUME_FADE_S         0.3f

/*
    WORK IN HAND: the blows, in real seconds (a pass is 1/CALENDAR_TICKS_PER_SECOND of one) at any speed. Current
    tuning, made up for the first sounds; how loud each is, and its pan, are the table's.
*/
#define SOUND_WORK_NEAREST          4       //of each kind, the nearest this many have a clock: a forest felled is 4 axes
#define SOUND_WORK_REACH            48.0f   //...within this of the listener; the table's curves are silent there anyway
#define SOUND_WORK_FIRST            0.3f    //someone starting: his first blow at least this far in, never on the spot
#define SOUND_BLOW_JITTER           0.12f   //every blow this share early or late, so no one is a metronome
//An axe: a blow every SOUND_CHOP_SECONDS, each woodcutter's own tempo up to SOUND_CHOP_SPREAD of it either way.
#define SOUND_CHOP_SECONDS          1.15f
#define SOUND_CHOP_SPREAD           0.25f
//A builder works in BOUTS with a rest after each: a nail hammered home, a joint chiselled, or a saw's stroke.
#define SOUND_HAMMER_SECONDS        0.42f
#define SOUND_HAMMER_BLOWS_MIN      3
#define SOUND_HAMMER_BLOWS_MAX      6
#define SOUND_CHISEL_SECONDS        0.30f
#define SOUND_CHISEL_BLOWS_MIN      2
#define SOUND_CHISEL_BLOWS_MAX      4
#define SOUND_SAW_SECONDS           2.2f    //sawing_wood_1 is a pair of strokes, 2.17 s
#define SOUND_BUILD_SAW_SHARE       0.2f    //of the bouts; then chisel's, and the rest are the hammer's
#define SOUND_BUILD_CHISEL_SHARE    0.25f
#define SOUND_BUILD_REST_MIN        0.9f
#define SOUND_BUILD_REST_MAX        2.0f

/*
    THE WATER HEARD (areas_plan.md): a loop opens when its nearest water is within OPEN of the listener and closes past
    CLOSE - the table's curves fall silent before either. Under the clouds is eased over WATER_HIDDEN_EASE_S, so water
    coming into sight swells. Distances are the listener's, in 3D: zooming out turns the water down.
*/
#define SOUND_RIVER_OPEN            55.0f
#define SOUND_RIVER_CLOSE           65.0f
#define SOUND_FALLS_OPEN            65.0f
#define SOUND_FALLS_CLOSE           75.0f
#define SOUND_WATER_HIDDEN_EASE_S   1.5f
//Under the title the world is not on screen: its water fades out over this, and back.
#define SOUND_TITLE_FADE_S          1.0f

#define CHASM_WORK_CHOP             0       //a woodcutter at a standing tree
#define CHASM_WORK_BUILD            1       //a builder at a site
#define CHASM_WORK_KINDS            2

#define CHASM_BOUT_HAMMER           0
#define CHASM_BOUT_CHISEL           1
#define CHASM_BOUT_SAW              2

namespace {

//Seconds as passes - the cues' clock and the work clock both count them.
uint64_t Passes(float seconds){
    return (uint64_t)std::max(1.0f,seconds * (float)CALENDAR_TICKS_PER_SECOND + 0.5f);
}

//A draw in [0,1) for a blow: core's hash, never rrand, whose stream the rules share (rrand_shared_stream).
float Draw(uint32_t person, uint32_t blow, uint64_t clock, uint32_t salt){
    return CueHash01(person,blow,clock,salt);
}

//Either way of 1 by up to `share`.
float Wobble(float share, float draw){
    return 1.0f + share * (2.0f * draw - 1.0f);
}

//A woodcutter's own tempo: fixed by his id, so he keeps it, and his neighbour has another.
float ChopPeriod(uint32_t person){
    return SOUND_CHOP_SECONDS * Wobble(SOUND_CHOP_SPREAD,CueHash01(person,0,0,1));
}

const char* WorkKindName(int kind){
    return (kind == CHASM_WORK_CHOP) ? "chop" : "build";
}

const char* BoutName(int bout){
    return (bout == CHASM_BOUT_SAW) ? "saw" : (bout == CHASM_BOUT_CHISEL) ? "chisel" : "hammer";
}

} //namespace

/*
    The device, the table and the output between them. With USE_SOUND=0, or no device, the cues get no output and
    still decide and log exactly the same - which is what chasm_sound reads.
*/
void ApplicationChasm::SetupSound(){
    CueOutput* output = NULL;
#ifdef USE_SOUND
    soundsystem = new SoundSystem();
    soundsystem->Initialise();
    //Only a working device gets an output: a table checked against a device with nothing registered would refuse
    //every sound in it, and the cues would not even decide.
    if (soundsystem->f_initialised){
        //Under a layer of its own, so one gain on it is every sound but the music - the settings' effects volume.
        int layer = soundsystem->AddBus(CHASM_SOUND_LAYER);
        sounds_bus = (layer >= 0) ? layer : SOUND_BUS_MASTER;
        cue_output.reset(new CueSoundOutput(soundsystem,sounds_bus));
        output = cue_output.get();
    }
#endif
    cues.Init((float)CALENDAR_TICKS_PER_SECOND,output);
    std::string error;
    if (!cues.LoadTable(CHASM_CUE_TABLE,error)){
        debug->Err("Cue table: %s - silent until it loads\n",error.c_str());
    }
    //The score's players, and the table's `music` action (ApplicationChasmMusic.cpp).
    SetupMusic();
#ifdef USE_SOUND
    if (soundsystem->f_initialised){
        activity_bus = soundsystem->FindBus(CHASM_SOUND_LAYER "/activity");
        ambience_bus = soundsystem->FindBus(CHASM_SOUND_LAYER "/ambience");
    }
#endif
    //For the poll. A baked build has no file to watch, and needs none.
    if (ResolveAssetPath(CHASM_CUE_TABLE,cue_table_path)){
        int64_t written = FileWriteTime(cue_table_path);
        cue_table_mtime = (written >= 0) ? written : 0;
    }else{
        cue_table_path.clear();
    }
    cue_table_polled = std::chrono::steady_clock::now();
}

//The table saved while the game runs is loaded within a second - paused or not, since this runs every pass.
void ApplicationChasm::PollCueTable(){
    if (cue_table_path.empty()){
        return;
    }
    auto t = std::chrono::steady_clock::now();
    if (t - cue_table_polled < std::chrono::seconds(1)){
        return;
    }
    cue_table_polled = t;
    int64_t written = FileWriteTime(cue_table_path);
    if (written < 0 || written == cue_table_mtime){
        return;
    }
    cue_table_mtime = written;
    std::string error;
    if (cues.Reload(error)){
        debug->Info("Cue table reloaded: %s\n",cues.File().c_str());
#ifdef USE_SOUND
        //A bus the table has only now declared.
        if (soundsystem && soundsystem->f_initialised){
            activity_bus = soundsystem->FindBus(CHASM_SOUND_LAYER "/activity");
            ambience_bus = soundsystem->FindBus(CHASM_SOUND_LAYER "/ambience");
        }
#endif
    }else{
        debug->Err("Cue table NOT reloaded, keeping the last good one: %s\n",error.c_str());
    }
}

void ApplicationChasm::UpdateSound(){
    sound_pass++;
    PollCueTable();
    //LIVE: the world ticking and on screen - not under the title, the pause menu, the statistics or the pause key.
    const bool f_live = !TitleShowing() && world_scene && main_scene == world_scene && !world_scene->IsPhysicsPaused();
#ifdef USE_SOUND
    if (soundsystem && soundsystem->f_initialised){
        float volume = sound_volume.load();
        if (soundsystem->GetBusGain(SOUND_BUS_MASTER) != volume){
            soundsystem->SetBusGain(SOUND_BUS_MASTER,volume);
        }
        //Held on the fader, not the gain, which is the table's: the two never meet.
        if (activity_bus >= 0 && soundsystem->IsBusPaused(activity_bus) == f_live){
            soundsystem->SetBusPaused(activity_bus,!f_live,f_live ? SOUND_RESUME_FADE_S : SOUND_PAUSE_FADE_S);
        }
        //The water runs on in a pause - time stops, the river does not - but not under the title.
        const bool f_shown = !TitleShowing() && world_scene && main_scene == world_scene;
        if (ambience_bus >= 0 && soundsystem->IsBusPaused(ambience_bus) == f_shown){
            soundsystem->SetBusPaused(ambience_bus,!f_shown,SOUND_TITLE_FADE_S);
        }
        //A fade out holds its bus once it is silent - here, once a pass.
        soundsystem->UpdateFades();
    }
#endif
    //Over the view's centre, its right the camera's (UpdateCamera's `right`).
    cues.SetListener(camera_target.x,camera_target.z,cam_distance * SOUND_LISTENER_HEIGHT,cosf(cam_yaw),-sinf(cam_yaw));
    SignalEvents();
    SignalWork(f_live);
    SignalWater();
    SignalVoice();
    UpdateMusic();
    /*
        The rules' answers to the player's commands. Answered in the tick, so a placement made in a pause is heard
        when the game goes on - with the building it makes, which is when it can be seen too. At most one of each
        a pass: a drag paints a plot or two a pass, and the table's gap thins that out further.
    */
    uint32_t placed = zone_placed.load();
    if (placed != sound_placed_seen){
        sound_placed_seen = placed;
        cues.Signal("placed");
    }
    uint32_t erased = zone_erased.load();
    if (erased != sound_erased_seen){
        sound_erased_seen = erased;
        cues.Signal("erased");
    }
    uint32_t refusals = zone_refusals.load() + order_refusals.load() + crate_refusals.load();
    if (refusals != sound_refusals_seen){
        sound_refusals_seen = refusals;
        cues.Signal("refused");
    }
    cues.Tick(sound_pass);
}

void ApplicationChasm::SignalUI(const char* name){
    cues.Signal(name);
}

/*
    THE PEOPLE'S VOICES (sound_plan.md, "Voices"). What he says is the table's: here is only that he was picked out
    ("picked") or what he was told - "sent" to a place or a tower, "released" back to his work, to "gather" a pack or
    take a tent first ("take_tent") or go "straight_on" without, to "settle" in another settlement, "pitch" his tent,
    or "forage" (with `what`, FORAGE_*). Each by name, as the events are, so a line of its own for one is a row.
*/
void ApplicationChasm::Say(uint32_t person, const char* signal, int rank, int what){
    std::lock_guard<std::mutex> lock(say_mutex);
    int now = say_next.empty() ? -1 : say_next[0].rank;
    if (rank < now || say_next.size() >= SOUND_VOICE_ASKED){
        return;
    }
    if (rank > now){
        say_next.clear();
    }
    Said s;
    s.signal = signal;
    s.person = person;
    s.rank = rank;
    s.what = what;
    say_next.push_back(s);
}

void ApplicationChasm::SayPicked(const std::vector<uint32_t>& ids){
    for (uint32_t id : ids){
        Say(id,"picked",CHASM_SAY_PICKED);
    }
}

/*
    The answer asked for since the last pass, each with his `voice` - 0 a man's, 1 a woman's; the table's rows pick the
    takes - and his `side`, which the rows pan by. ONE asked: one answers, his side where he stands across the view.
    MORE: two answer at once, the first asked and the first after him in the other voice if there is one - a man and
    a woman sound like two people where two men are the same man twice - one on each side, the one more to the left
    on the left (the table's `voice` group speaks them `together`).
*/
void ApplicationChasm::SignalVoice(){
    std::vector<Said> asked;
    {
        std::lock_guard<std::mutex> lock(say_mutex);
        asked.swap(say_next);
    }
    if (asked.empty()){
        return;
    }
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!e){
        return;
    }
    struct Voice{
        const Said* said;
        const EconomyWorker* k;
        int voice;
        float dx;                   //along the view's right, from its middle
    };
    std::vector<Voice> voices;
    for (const Said& s : asked){
        for (const EconomyWorker& k : e->workers){
            if (k.id != s.person){
                continue;
            }
            //A child has no voice yet, and says nothing.
            int figure = PersonFigure(k,e->tick);
            if (figure == PERSON_ADULT_MALE || figure == PERSON_ADULT_FEMALE){
                vec2 at = EconomyBody(k);
                float dx = (at.x - camera_target.x) * cosf(cam_yaw) - (at.y - camera_target.z) * sinf(cam_yaw);
                voices.push_back(Voice{&s,&k,(figure == PERSON_ADULT_FEMALE) ? 1 : 0,dx});
            }
            break;
        }
    }
    if (voices.empty()){
        return;
    }
    std::vector<Voice> say(1,voices[0]);
    if (voices.size() >= 2){
        auto other = std::find_if(voices.begin() + 1,voices.end(),[&](const Voice& v){ return v.voice != voices[0].voice; });
        say.push_back((other != voices.end()) ? *other : voices[1]);
    }
    for (size_t i = 0; i < say.size(); i++){
        float side;
        if (say.size() == 1){
            side = std::max(-1.0f,std::min(1.0f,say[0].dx / SOUND_VOICE_SIDE_REACH));
        }else{
            const Voice& o = say[1 - i];
            side = (say[i].dx < o.dx || (say[i].dx == o.dx && i == 0)) ? -1.0f : 1.0f;
        }
        const Voice& v = say[i];
        CuePayload p;
        vec2 at = EconomyBody(*v.k);
        p.Set("x",at.x).Set("z",at.y).Set("person",(float)v.k->id);
        p.Set("voice",(float)v.voice).Set("side",side).Set("what",(float)v.said->what);
        cues.Signal(v.said->signal,p,(int)v.k->id);     //his id: two of one row draw two takes
    }
}

/*
    The rules' EVENTS made since the last look, each signalled by its name with where it happened (x, z: a sound's
    place) and its amount, person and building. Counted as the notice line counts them (ApplicationChasmTime.cpp):
    a new map, or a load - whose ring starts empty - starts the count over and tells nothing old. Only news: a
    replay's start can bring events of long ago into the ring, so one older than half a day is passed over.
*/
void ApplicationChasm::SignalEvents(){
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!e){
        return;
    }
    if (e->world.get() != sound_events_world || e->events_made < sound_events_seen){
        sound_events_world = e->world.get();
        sound_events_seen = e->events_made;
        work_beats.clear();             //people's ids belong to their map
        return;
    }
    if (e->events_made == sound_events_seen){
        return;
    }
    size_t fresh = std::min((size_t)(e->events_made - sound_events_seen),e->events.size());
    for (size_t i = e->events.size() - fresh; i < e->events.size(); i++){
        const EconomyEvent& ev = e->events[i];
        if (e->tick >= ev.tick + CALENDAR_DAY_TICKS / 2){
            continue;
        }
        CuePayload p;
        p.Set("amount",(float)ev.amount).Set("person",(float)ev.person).Set("building",(float)ev.building);
        /*
            No place, no distance: an event of nowhere in particular (a pack run out) is heard as the HUD is. A place is
            a plot, or a position without one - a harvest's field stands on cells, not a plot. Economy::Event takes
            (0, 0) for "none", so it is read that way here too.
        */
        if (ev.plot >= 0 || ev.pos.x != 0.0f || ev.pos.y != 0.0f){
            p.Set("x",ev.pos.x).Set("z",ev.pos.y);
        }
        //An area found (areas_plan.md): whether it is the first of its kind, and which kind (AREA_*), for a row's `when`.
        if (ev.kind == EVENT_DISCOVERED){
            const AreaData* areas = e->world ? e->world->areas.get() : nullptr;
            bool f_known = areas && ev.amount >= 0 && ev.amount < (int)areas->areas.size();
            p.Set("first",(ev.what & 1) ? 1.0f : 0.0f).Set("area",f_known ? (float)areas->areas[(size_t)ev.amount].kind : -1.0f);
        }
        cues.Signal(EconomyEventName(ev.kind),p);
    }
    sound_events_seen = e->events_made;
}

/*
    WORK IN HAND (ApplicationChasm.h): a clock for each of the nearest few at work near the view, and a signal on
    each of its blows, at where he stands. Felling and building have no clip to time a blow to, so the rhythm is the
    sound's own: an axe at the woodcutter's own tempo; a builder in bouts - a nail hammered home, a joint chiselled,
    a saw's strokes - with a rest after each. A clock is made when he comes within the nearest and dropped when he
    leaves them, or stops; its first blow is a moment in and drawn, so a crew that starts together is not in step.

    The work clock stands still with the world, so nothing is owed when it goes on: a pause is a pause in the work.
*/
void ApplicationChasm::SignalWork(bool f_live){
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!f_live || !e || !e->world){
        return;
    }
    work_clock++;
    const float height = cam_distance * SOUND_LISTENER_HEIGHT;
    struct Heard{
        uint32_t person;
        int kind;
        float distance;
        vec2 at;
    };
    std::vector<Heard> heard;
    for (const EconomyWorker& k : e->workers){
        int kind = -1;
        if (k.state == WORKER_CLEARING){
            kind = CHASM_WORK_CHOP;
        }else if (k.state == WORKER_WORKING && k.job == WORKER_JOB_WOODCUTTER && k.prop >= 0
                  && (size_t)k.prop < e->prop_state.size() && e->prop_state[k.prop] == PROP_STATE_STANDING){
            kind = CHASM_WORK_CHOP;     //felling: at a log WORKING is only picking it up
        }else if (k.state == WORKER_BUILDING){
            kind = CHASM_WORK_BUILD;
        }
        if (kind < 0){
            continue;
        }
        //Where his body stands, pushed off his place by those near him (SPACING) - where the blow is seen.
        vec2 at(k.pos.x + k.off.x,k.pos.y + k.off.y);
        float dx = at.x - camera_target.x;
        float dz = at.y - camera_target.z;
        float d = sqrtf(dx * dx + dz * dz + height * height);
        if (d <= SOUND_WORK_REACH){
            heard.push_back(Heard{k.id,kind,d,at});
        }
    }
    std::sort(heard.begin(),heard.end(),[](const Heard& a, const Heard& b){
        if (a.kind != b.kind){
            return a.kind < b.kind;
        }
        return (a.distance != b.distance) ? a.distance < b.distance : a.person < b.person;
    });
    std::vector<WorkBeat> kept;
    int count[CHASM_WORK_KINDS] = {};
    for (const Heard& w : heard){
        if (count[w.kind]++ >= SOUND_WORK_NEAREST){
            continue;
        }
        WorkBeat b;
        auto it = std::find_if(work_beats.begin(),work_beats.end(),[&](const WorkBeat& o){
            return o.person == w.person && o.kind == w.kind;
        });
        if (it != work_beats.end()){
            b = *it;
        }else{
            b.person = w.person;
            b.kind = w.kind;
            float span = (w.kind == CHASM_WORK_CHOP) ? ChopPeriod(w.person) : SOUND_BUILD_REST_MAX;
            b.next = work_clock + Passes(SOUND_WORK_FIRST + span * Draw(w.person,0,work_clock,4));
        }
        b.at = w.at;
        kept.push_back(b);
    }
    work_beats.swap(kept);

    for (WorkBeat& b : work_beats){
        if (work_clock < b.next){
            continue;
        }
        CuePayload p;
        p.Set("x",b.at.x).Set("z",b.at.y).Set("person",(float)b.person);
        //His id as the instance: the cue's draws - which take, what pitch_jitter - are hashed with it, so two
        //blows on one pass are two draws, not one twice.
        const int instance = (int)b.person;
        if (b.kind == CHASM_WORK_CHOP){
            cues.Signal("chop",p,instance);
            b.blows++;
            b.next = work_clock + Passes(ChopPeriod(b.person) * Wobble(SOUND_BLOW_JITTER,Draw(b.person,b.blows,work_clock,2)));
            continue;
        }
        //A builder between bouts: what the next one is, and how many blows it has.
        if (b.left <= 0){
            float r = Draw(b.person,b.blows,work_clock,3);
            b.bout = (r < SOUND_BUILD_SAW_SHARE) ? CHASM_BOUT_SAW
                   : (r < SOUND_BUILD_SAW_SHARE + SOUND_BUILD_CHISEL_SHARE) ? CHASM_BOUT_CHISEL : CHASM_BOUT_HAMMER;
            int lo = (b.bout == CHASM_BOUT_CHISEL) ? SOUND_CHISEL_BLOWS_MIN : SOUND_HAMMER_BLOWS_MIN;
            int hi = (b.bout == CHASM_BOUT_CHISEL) ? SOUND_CHISEL_BLOWS_MAX : SOUND_HAMMER_BLOWS_MAX;
            b.left = (b.bout == CHASM_BOUT_SAW) ? 1 : std::min(hi,lo + (int)(Draw(b.person,b.blows,work_clock,5) * (hi - lo + 1)));
        }
        cues.Signal(BoutName(b.bout),p,instance);
        b.blows++;
        b.left--;
        float gap = (b.bout == CHASM_BOUT_SAW) ? SOUND_SAW_SECONDS
                  : (b.bout == CHASM_BOUT_CHISEL) ? SOUND_CHISEL_SECONDS : SOUND_HAMMER_SECONDS;
        gap *= Wobble(SOUND_BLOW_JITTER,Draw(b.person,b.blows,work_clock,2));
        if (b.left <= 0){
            gap += SOUND_BUILD_REST_MIN + (SOUND_BUILD_REST_MAX - SOUND_BUILD_REST_MIN) * Draw(b.person,b.blows,work_clock,6);
        }
        b.next = work_clock + Passes(gap);
    }
}

/*
    THE WATER HEARD (areas_plan.md): each pass, the nearest river water and the nearest waterfall to the listener -
    how far (in 3D, over the view's centre), to which side, and whether that nearest stretch is under the clouds -
    as parameters the table's two loops follow. The game measures and the table decides how loud.

    A river's water is its centre line widened by its half-width at each point (TerrainRiver::HalfAt), so over it
    is 0 across. A river that ends in a fall stops at the lip: its line runs on a little past it into the chasm's
    air, which is the fall's to be heard from, not the river's.
*/
void ApplicationChasm::SignalWater(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const Exploration> ex = GetExploration();
    const bool f_world = w && w->terrain;
    const vec2 at(camera_target.x,camera_target.z);
    const vec2 right(cosf(cam_yaw),-sinf(cam_yaw));
    const float height = cam_distance * SOUND_LISTENER_HEIGHT;
    auto hidden_at = [&](const vec2& p){
        return (ex && !ex->Empty() && ex->Explored(p)) ? 0.0f : 1.0f;
    };
    const float ease = std::min(1.0f,1.0f / (SOUND_WATER_HIDDEN_EASE_S * (float)CALENDAR_TICKS_PER_SECOND));

    float river_d = 1e30f;
    vec2 river_at;
    float falls_d = 1e30f;
    vec2 falls_at;
    int falls_which = -1;
    if (f_world){
        const Terrain& t = *w->terrain;
        for (size_t r = 0; r < t.rivers.size(); r++){
            const TerrainRiver& river = t.rivers[r];
            float cut = 1e30f;
            for (const TerrainFall& f : t.falls){
                cut = (f.river == (int)r) ? std::min(cut,f.along) : cut;
            }
            float along = 0.0f;
            for (size_t i = 0; i + 1 < river.points.size() && along < cut; i++){
                const vec2& a = river.points[i];
                const vec2& b = river.points[i + 1];
                vec2 ab = b - a;
                float len2 = ab.dot(ab);
                float s = (len2 > 0.0f) ? std::max(0.0f,std::min(1.0f,(at - a).dot(ab) / len2)) : 0.0f;
                along += sqrtf(len2);
                vec2 c = a + ab * s;
                float across = (at - c).length();
                float half = (river.half.size() > i + 1) ? river.HalfAt(i,s) : river.width * 0.5f;
                float edge = std::max(0.0f,across - half);
                if (edge < river_d){
                    river_d = edge;
                    //The nearest of its water: on the line, or out to its edge toward the listener.
                    river_at = (across > 1e-4f) ? c + (at - c) * (std::min(across,half) / across) : c;
                }
            }
        }
        for (size_t i = 0; i < t.falls.size(); i++){
            float d = (t.falls[i].lip - at).length();
            if (d < falls_d){
                falls_d = d;
                falls_at = t.falls[i].lip;
                falls_which = (int)i;
            }
        }
    }
    /*
        A FALL is faint until it is discovered: its area seen (Areas.h), not the lip's own cell, which is on the rim's
        edge and may stay unexplored while everything round it is seen. A river is faint where its nearest stretch is
        unexplored - one is long, and seldom all seen at once.
    */
    auto fall_hidden = [&](int which){
        if (!f_world || !w->areas || !ex){
            return 1.0f;
        }
        for (const Area& a : w->areas->areas){
            if (a.kind != AREA_WATERFALL || a.which != which){
                continue;
            }
            for (int c : a.cells){
                if (c < (int)ex->cells.size() && ex->cells[(size_t)c]){
                    return 0.0f;
                }
            }
        }
        return 1.0f;
    };
    //`target`: how hidden that water is now, 1 under the clouds - eased toward it.
    auto heard = [&](const char* scope, bool& f_open, float flat, const vec2& where, float target, float& hidden,
                     float open, float close, const char* distance_name, const char* dx_name, const char* hidden_name){
        float d = (flat < 1e29f) ? sqrtf(flat * flat + height * height) : 1e30f;
        if (d < 1e29f){
            hidden += (target - hidden) * ease;
            cues.SetParameter(distance_name,d);
            cues.SetParameter(dx_name,(where - at).dot(right));
            cues.SetParameter(hidden_name,hidden);
        }
        if (!f_open && d < open){
            //Opened at what it is now, not eased up from wherever the last water left it.
            hidden = target;
            cues.SetParameter(hidden_name,hidden);
            cues.BeginScope(scope);
            f_open = true;
        }else if (f_open && d > close){
            cues.EndScope(scope);
            f_open = false;
        }
    };
    heard("river",f_river_heard,f_world ? river_d : 1e30f,river_at,hidden_at(river_at),river_hidden,
          SOUND_RIVER_OPEN,SOUND_RIVER_CLOSE,"river_distance","river_dx","river_hidden");
    heard("falls",f_falls_heard,f_world ? falls_d : 1e30f,falls_at,fall_hidden(falls_which),falls_hidden,
          SOUND_FALLS_OPEN,SOUND_FALLS_CLOSE,"falls_distance","falls_dx","falls_hidden");
}

#ifdef USE_MCP
void ApplicationChasm::RegisterSoundTools(){
    MCPServer::Get()->RegisterTool("chasm_sound",
        "THE SOUND (docs/sound_plan.md): what the cue table decided and what is playing. The table is "
        "assets/cues/chasm.json, reloaded within a second of being saved. op status (default): `device` (false: no "
        "sound device or a USE_SOUND=0 build - the cues still decide and log the same), `table` and its `cues`, "
        "`volume`, `live` (the world ticking and on screen: only then is anyone heard at work), `activity_held` (the "
        "work's bus faded out and held, or fading - while not live), `pass` (the cues' "
        "clock, 50 a second at any speed), the `listener` (x, z over the view's centre, and its height), the `work` "
        "clocks (person, kind chop or build, bout, blows, distance, passes to his next blow), `voices` playing "
        "(sound, bus, gain, pan, seconds in), `playing` and `waiting` as the cues count them, and the cue log's last "
        "`lines` (default 30): pass, cue, play or skip and why, sound, gain, pan. op play: `cue` at full strength, as "
        "if its trigger came (CueSystem::Audition). `volume` 0..1, with any op, sets the master: 0 mutes without "
        "changing what the cues decide or log. `music`, with any op, steers the score as the Music panel does: "
        "section (a section's name, \"\" the season's), clouds and chasm (0..1 hold the layers' shares of the view, "
        "below 0 lets them go), stinger (play one of the score's), suspense and brightness (0..1, below 0 the "
        "score's own). The status's `music` says what each player plays, what drives it (section_from, "
        "shares_held, mood) and what the game `sent` it last.",
        json{
            {"type","object"},
            {"properties",{
                {"op",{{"type","string"},{"description","status (default) or play"}}},
                {"cue",{{"type","string"},{"description","op play: the cue's name"}}},
                {"volume",{{"type","number"},{"description","0..1, the master"}}},
                {"music",{{"type","object"},{"description","section, clouds, chasm, stinger, suspense, brightness"}}},
                {"lines",{{"type","integer"},{"description","the cue log's last lines, default 30"}}}
            }}
        },
        [this](const json& args) -> json {
            std::string op = "status";
            auto jo = args.find("op");
            if (jo != args.end() && jo->is_string()){
                op = jo->get<std::string>();
            }
            auto jv = args.find("volume");
            if (jv != args.end() && jv->is_number()){
                sound_volume = std::max(0.0f,std::min(1.0f,jv->get<float>()));
            }
            auto jm = args.find("music");
            if (jm != args.end() && jm->is_object()){
                main_scene->AtTickBoundary([&](){
                    SteerMusic(*jm);
                });
            }
            int lines = 30;
            auto jl = args.find("lines");
            if (jl != args.end() && jl->is_number_integer()){
                lines = std::max(0,jl->get<int>());
            }
            json result;
            if (op == "play"){
                std::string cue;
                auto jc = args.find("cue");
                if (jc != args.end() && jc->is_string()){
                    cue = jc->get<std::string>();
                }
                bool f_known = false;
                main_scene->AtTickBoundary([&](){
                    std::vector<std::string> names = cues.CueNames();
                    f_known = std::find(names.begin(),names.end(),cue) != names.end();
                    if (f_known){
                        cues.Audition(cue);
                    }
                });
                if (!f_known){
                    return json{{"error","no cue named '" + cue + "' - op status lists them"}};
                }
                result["played"] = cue;
                //Heard on the next pass: let it start, so `voices` shows it.
                std::this_thread::sleep_for(std::chrono::milliseconds(60));
            }else if (op != "status"){
                return json{{"error","op: status or play"}};
            }
            //Between passes, so nothing below is read half-way through UpdateSound.
            main_scene->AtTickBoundary([&](){
                bool f_device = false;
                bool f_activity_held = false;
                json voices = json::array();
#ifdef USE_SOUND
                if (soundsystem && soundsystem->f_initialised){
                    f_device = true;
                    f_activity_held = activity_bus >= 0 && soundsystem->IsBusPaused(activity_bus);
                    std::vector<SoundVoiceInfo> list;
                    soundsystem->ListVoices(list);
                    for (const SoundVoiceInfo& v : list){
                        voices.push_back(json{{"sound",v.name},{"bus",soundsystem->GetBusName(v.bus)},
                                              {"gain",std::round(v.gain * 1000.0f) / 1000.0f},
                                              {"pan",std::round(v.pan * 100.0f) / 100.0f},
                                              {"at",std::round(v.position * 100.0f) / 100.0f},{"held",v.f_held}});
                    }
                }
#endif
                json work = json::array();
                const float height = cam_distance * SOUND_LISTENER_HEIGHT;
                for (const WorkBeat& b : work_beats){
                    float dx = b.at.x - camera_target.x;
                    float dz = b.at.y - camera_target.z;
                    json j{{"person",b.person},{"kind",WorkKindName(b.kind)},{"blows",b.blows},
                           {"distance",std::round(sqrtf(dx * dx + dz * dz + height * height) * 10.0f) / 10.0f},
                           {"next_in",(int64_t)b.next - (int64_t)work_clock}};
                    if (b.kind == CHASM_WORK_BUILD){
                        j["bout"] = BoutName(b.bout);
                        j["left"] = b.left;
                    }
                    work.push_back(j);
                }
                result["device"] = f_device;
                result["table"] = cues.File();
                result["cues"] = cues.CueNames();
                result["volume"] = sound_volume.load();
                result["live"] = !TitleShowing() && world_scene && main_scene == world_scene && !world_scene->IsPhysicsPaused();
                result["activity_held"] = f_activity_held;
                result["pass"] = sound_pass;
                result["listener"] = json{{"x",std::round(camera_target.x * 10.0f) / 10.0f},
                                          {"z",std::round(camera_target.z * 10.0f) / 10.0f},
                                          {"height",std::round(height * 10.0f) / 10.0f}};
                result["work"] = work;
                //The water heard (SignalWater): whether each loop is open, and what it follows.
                auto param = [&](const char* name){
                    return std::round(cues.Parameter(name) * 100.0f) / 100.0f;
                };
                result["water"] = json{{"river",json{{"heard",f_river_heard},{"distance",param("river_distance")},
                                                     {"dx",param("river_dx")},{"hidden",param("river_hidden")}}},
                                       {"falls",json{{"heard",f_falls_heard},{"distance",param("falls_distance")},
                                                     {"dx",param("falls_dx")},{"hidden",param("falls_hidden")}}}};
                result["voices"] = voices;
                result["music"] = MusicStatusJson();
                result["playing"] = cues.NumPlaying();
                result["waiting"] = cues.NumWaiting();
            });
            //Lines(0) is every line, so none is asked for as none.
            result["log"] = (lines > 0) ? json(cues.log.Lines((size_t)lines)) : json::array();
            return result;
        });
}
#endif
