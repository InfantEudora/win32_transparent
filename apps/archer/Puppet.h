#ifndef _ARCHER_PUPPET_H_
#define _ARCHER_PUPPET_H_

#include "Stage.h"

/*
    The PUPPET: what the archer's model is doing, as opposed to what the archer is doing.

    Stage.h owns the rules - where the body is, how fast, whether it is hanging off a ledge. This
    file owns the second question, which is a genuinely different one: given that state, WHICH CLIP
    plays, at WHAT RATE, and WHICH WAY IS SHE FACING. Those are animation decisions, they have
    their own constants, and they are wrong in their own ways.

    --- WHY THIS IS A SEPARATE FILE AND NOT A FEW LINES IN THE APP -------------------------------
    Two reasons, and the second is the one that made it worth doing.

    It NAMES NO ENGINE TYPE, exactly like Stage.h, so `make rules` builds and tests it with no
    core, no window and no GPU. "At a dead run the walk cycle is played at 1.8x" is a fact about
    the game that can be asserted in a test rather than squinted at on screen. The moment this
    logic lives in ApplicationArcher.cpp it can only be checked by looking.

    And it puts ONE STRUCT between the rules and the animation - ArcherAnimParams. Everything the
    animation is allowed to know arrives in it. That means the rules are not the only thing that
    can fill it: a debug panel can, and then the model animates with no level, no gravity and no
    input at all. That is PUPPET MODE, it is how animation gets iterated on without disturbing the
    physics prototype, and it costs nothing extra because the struct had to exist anyway.

    --- WHAT THIS DELIBERATELY DOES NOT DO -------------------------------------------------------
    It does not blend, sample or pose anything. It answers "play THIS, at THIS rate, facing THIS
    way" and the app carries it out through Object::TransitionToAnimation. When the signed-speed
    blend space lands (step 1 in animation_plan.md) the answer grows a second clip and a weight;
    the seam stays here.
*/

//--- The clips, as meshes/archer.glb names them -------------------------------------------------
/*
    The export is all-or-nothing, so the file carries more than the game has a use for. Every clip
    in it is listed anyway: a clip that is not in this table cannot be previewed, and "did that
    export correctly" is a question about ALL of them.
*/
enum ArcherClip{
    CLIP_IDLE = 0,
    CLIP_IDLE_LOOK,
    CLIP_WALK,
    CLIP_WALK2,
    CLIP_RUN_SLOW,
    CLIP_RUN_FAST,
    CLIP_TURN,
    CLIP_KICK,
    CLIP_CLIMB,
    CLIP_CROUCH,
    CLIP_STRETCH,
    CLIP_WARMUP,
    CLIP_DANCE,
    CLIP_TWIRL,
    CLIP_COUNT
};

/*
    The LOCOMOTION LADDER: the clips that are a way of covering ground, slowest first.

    Everything else in the table is a pose, a one-shot or set dressing. These are the ones
    Choose picks between on speed, and the order is relied on only by the tests - the choice
    itself is made on each clip's MEASURED speed, so adding a sprint to this list and to the
    export is the whole of what it takes to use one.

    Walking2 is deliberately NOT here. It is a second walk at almost exactly Walking's speed
    (1.48 against 1.58), so including it would make the choice between them arbitrary and
    flip-floppy at a walk. It stays in the table so it can still be previewed and compared.
*/
#define PUPPET_LOCOMOTION_COUNT     3
extern const int PUPPET_LOCOMOTION[PUPPET_LOCOMOTION_COUNT];

struct ArcherClipInfo{
    const char* name;           //exactly as exported; Object::FindAnimation matches on it
    bool  f_looping;
    //Does this clip's hip track carry real forward travel? If so its speed is MEASURED from the
    //clip itself at load (see Puppet::clip_speed) rather than typed in here, because a re-export
    //with a longer stride must not need a number in this file changed to match.
    bool  f_travels;
    /*
        Does this clip TURN THE CHARACTER, or is its hip yaw part of the gait?

        Every clip's root yaw is taken off the bone by the engine whether anyone wants it or not
        (Animation::SampleRootMotion, which does not gate yaw on the extract flags the way it gates
        position). What is left is deciding what to DO with it, and the answer is different for two
        kinds of clip that look identical from the outside:

          - a PIVOT really does turn her, and dropping its yaw leaves her sliding round facing the
            wrong way;
          - a RUN CYCLE swings its hips a dozen degrees each way as part of running, and applying
            THAT to the character's transform wags her whole body from side to side. Measured on
            Running_Fast: 23 degrees of it.

        So it is per clip, and it has to be authored knowledge rather than a threshold - a gentle
        curve and a hip swing are the same shape at different amplitudes. Puppet::clip_turn_deg
        reports each clip's measured net turn so this column can be checked against the export.

        ONE COMBINATION IS FORBIDDEN, and the rules test enforces it: a clip may not have f_turns
        AND f_travels. The character's rotation is applied ABOVE the root bone, so extracting the
        yaw while the translation stays on the bone means R(yaw) * T(p) - the authored offset gets
        SWEPT ROUND rather than translated, and a clip that walks two steps while turning orbits a
        point instead. Extracting the translation as well is not the way out either, because Stage
        owns where the archer is and the app throws that translation away. So: a clip that turns
        must turn ON THE SPOT. A pivot does; a travelling spin does not and simply keeps its
        rotation on the bone, where it still reads as a spin.
    */
    bool  f_turns;
};
extern const ArcherClipInfo ARCHER_CLIPS[CLIP_COUNT];

//--- The feel constants, which are the animation's, not the rules' ------------------------------
/*
    Durations are TICKS, at ARCHER_TPS, per the house rule - see the note at the top of Stage.h.
*/

//Below this ground speed she stands still. Well under a walk, so a nudge does not start the cycle.
#define PUPPET_IDLE_SPEED           0.30f

/*
    How far playback rate is allowed to stretch to keep the feet planted.

    A clip has ONE stride length, so the only way to cover ground faster without a new clip is to
    play it faster - and past about 1.8x a walk stops reading as a person and starts reading as a
    fast-forward. Clamping here rather than letting the rate run free is what turns "the animation
    looks wrong" into a MEASURABLE amount of missing clip: Puppet::choice.wanted_rate is what the
    match asked for, choice.rate is what it got, and the gap between them is the authoring bill.
*/
#define PUPPET_RATE_MIN             0.60f
#define PUPPET_RATE_MAX             1.80f

/*
    And the same limit for a ONE-SHOT action clip, which is fitted to a WINDOW rather than to a
    stride. Looser than PUPPET_RATE_MAX because there is no gait to break, far tighter than
    "whatever it takes" because a kick at five times speed is a blur rather than a kick. Both the
    kick and the climb hit this today - see the retiming table in animation_plan.md.
*/
#define PUPPET_ACTION_RATE_MAX      2.50f

/*
    How long the turnaround takes, in ticks.

    She is always side-on, so the model only ever faces +X or -X and a "turn" is a 180 degree yaw.
    Slewing it over a few ticks rather than snapping is the cheapest turnaround there is (option 1
    in animation_plan.md section 7) and at speed it reads fine. Five ticks is 83ms - fast enough
    not to fight the controls, slow enough to be visible.
*/
#define PUPPET_TURN_TICKS           5

//Which way the MODEL faces at rest. Mixamo rigs are authored facing +Z, so a side-view character
//running towards +X is that model yawed a quarter turn. Measured from the export rather than
//assumed: in bind pose the toes sit at +Z of the ankle, and the walk clip travels +Z.
#define PUPPET_YAW_RIGHT            90.0f
#define PUPPET_YAW_LEFT             -90.0f

//--- What the animation is allowed to know ------------------------------------------------------
/*
    THE SEAM. Filled by DescribeArcher from the rules, or by hand from the debug panel, and those
    two are indistinguishable from here - which is the whole point.

    Everything is in world units and seconds, not ticks and not per-tick deltas, because an
    animation parameter that changes meaning when ARCHER_TPS changes is a bug waiting for someone
    to change ARCHER_TPS.
*/
struct ArcherAnimParams{
    float speed = 0.0f;             //signed along FACING: + running forwards, - backing up
    float ground_speed = 0.0f;      //|vel.x|, which is what the stride has to match
    float vel_y = 0.0f;             //+ rising, - falling
    float facing = 1.0f;            //+1 right, -1 left; never 0
    bool  f_on_ground = true;
    int   mode = MODE_GROUND;       //ArcherMode
    int   action = 0;               //ArcherAction
    float action_phase = 0.0f;      //0..1 through whatever `action` is, for one-shot clips
    float aim_deg = 0.0f;
    float draw_power = 0.0f;        //0..1
};

//What she is doing with her ARMS, which is a separate question from what her legs are doing - and
//stays separate, because that is the whole argument for an upper-body layer (step 2).
enum ArcherAction{
    ACTION_NONE = 0,
    ACTION_DRAW,                    //bow being drawn or held at full
    ACTION_KICK,
    ACTION_CLIMB,                   //pulling up over a ledge
    ACTION_HANG                     //hanging off one
};

//Reads the rules into the seam. Pure; the rules are not touched.
void DescribeArcher(const Stage& stage, ArcherAnimParams& out);

//--- The answer ---------------------------------------------------------------------------------
struct PuppetChoice{
    int   clip = CLIP_IDLE;
    float rate = 1.0f;              //what to play it at, after clamping
    float wanted_rate = 1.0f;       //what matching the feet to the ground actually asked for
    /*
        True when NOTHING IS AUTHORED for this state and some other clip is standing in.

        Not a warning - it is the shopping list. The panel lists every state that came back
        placeholder, so "what do I need to animate next" is read off the running game rather than
        remembered.
    */
    bool  f_placeholder = false;
};

class Puppet{
public:
    /*
        Each travelling clip's forward speed IN CLIP UNITS PER SECOND, measured from its own root
        track when it loads. Zero for a clip that stays put.

        Measured rather than declared because it is the one number that decides whether the feet
        slide, and a re-export changes it. See ApplicationArcher::MeasureClipSpeeds.
    */
    float clip_speed[CLIP_COUNT] = {};

    //Net yaw each clip turns her through, in DEGREES, measured at load. Reported rather than acted
    //on: it is what says whether a clip's f_turns column is set right. A cycle reads near zero
    //however much its hips swing on the way, because the swing comes back.
    float clip_turn_deg[CLIP_COUNT] = {};

    //Each clip's length in seconds, also read from the clip at load. The preview panel shows it,
    //and it is what a ONE-SHOT clip's rate is matched against - an action has a duration to fit,
    //where a locomotion cycle has a stride to fit.
    float clip_duration[CLIP_COUNT] = {};

    //The uniform scale the model is drawn at. A clip's speed in WORLD units is its measured speed
    //times this, which is why the two have to be known together - a rig authored half-size walks
    //half as fast even though nothing about the clip changed.
    float model_scale = 1.0f;

    //Stretch playback to keep the feet planted, or play every clip at 1.0 and let them slide.
    //A toggle rather than a decision because seeing both is the point of the first animation pass.
    bool  f_match_feet = true;

    //Where the model is facing right now, in degrees, slewed toward the side `facing` asks for.
    float yaw_deg = PUPPET_YAW_RIGHT;

    PuppetChoice choice;

    //One tick. The only thing that changes state here is the yaw slew; the clip choice is a pure
    //function of the parameters and could be asked for at any time.
    void Tick(const ArcherAnimParams& in);

    //The clip choice on its own, without touching the yaw. Pure - the rules test calls this.
    PuppetChoice Choose(const ArcherAnimParams& in) const;

    //A clip's forward speed in WORLD units per second, or 0 if it does not travel.
    float WorldClipSpeed(int clip) const;

    //Which way the model should be facing for this `facing`, with no slew.
    static float TargetYaw(float facing);
};

#endif
