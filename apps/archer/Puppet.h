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
    /*
        THE AIR SET, as four composable pieces rather than one whole jump.

        Jump_ToAir ends on exactly the pose Falling_Idle holds, so the handover at the apex is
        between two near-identical poses and costs nothing. That composability is the reason to
        prefer these over Jumping_InPlace, which is the same jump baked into one 1.93s clip and
        can only be used by slicing it - see animation_plan.md. Jumping_InPlace stays previewable
        because comparing the two is the point of having both.
    */
    CLIP_JUMP_RISE,         //Jump_ToAir             standing -> the airborne pose
    CLIP_FALL,              //Falling_Idle           the airborne pose, held
    CLIP_LAND_SOFT,         //Jump_FromAir           airborne pose -> standing, no real absorb
    CLIP_LAND_HARD,         //FallingIdle_ToLanding  the dramatic one, with a deep absorb
    /*
        AND THE RUNNING JUMP, which is one whole arc rather than four pieces - because unlike the
        standing set it does not need to be composable. Every frame of it is airborne, so there is
        no anticipation to skip and no landing glued to the end; it is entered at takeoff, played
        once, and holds its last frame if she is still in the air when it runs out.
    */
    CLIP_RUN_JUMP,          //Running_Jump           takeoff -> apex -> descending, 0.933s
    CLIP_HANG,              //Hanging_Braced         holding a ledge
    CLIP_STOP,              //Running_ToStop         plant and settle out of a run
    CLIP_KICK_SPIN,         //Kick_FrontSpin         the spinning kick; a real pivot
    CLIP_HANDSTAND,         //Walk_ToHandstand       set dressing; preview only
    CLIP_JUMP_IN_PLACE,     //Jumping_InPlace        a whole standing jump; preview only
    CLIP_JUMP_FORWARD,      //Jump_Forward           a whole travelling jump; not wired
    CLIP_DRAW,              //Standing_DrawArrow     needs step 2's mask layer before it can play
    CLIP_STRETCH2,
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
        and a translation left on the bone. The character's rotation is applied ABOVE the root
        bone, so R(yaw) * T(p) - an authored offset still sitting there gets SWEPT ROUND rather
        than translated, and a clip that walks two steps while turning orbits a point instead.
        The way out is f_extract_move, which takes the offset off the bone entirely; what is not
        allowed is extracting the yaw and leaving the translation behind. A pivot turns on the
        spot and needs neither. Twirl travels and spins, and keeps BOTH on the bone, where the
        spin still reads as a spin and the step back still reads as a step back.
    */
    bool  f_turns;

    /*
        Does the hip's authored TRANSLATION belong to the character rather than to the pose?

        A DIFFERENT QUESTION FROM f_travels, and conflating the two cost a session. f_travels asks
        "should the blend space measure this clip's stride"; this asks "is that stride already
        being walked by the rules". Climb answers no to the first (its speed would be a meaningless
        number - it is a mantle, not a stride) and yes to the second (it really does carry the body
        forward and up). Twirl answers the other way round.

        WHERE IT GOES WRONG WHEN IT IS OFF AND SHOULD BE ON: Stage owns the archer's position and
        SyncArcherAnimation writes it every tick, so an un-extracted stride is drawn ON TOP of the
        position she has already been moved to. She creeps ahead of herself over the cycle - 1.53
        world units for Walking, 2.94 for Running_Fast - and snaps back the moment the clip wraps
        or is replaced. Coming to a stop and dropping to Idle looks like the character jumping
        backwards, because that is exactly what the hips do.

        Extracting it hands the translation to Object::ApplyRootMotion, and ArcherModel's override
        DISCARDS it while keeping the yaw. That is the point: the pose is pinned so the rules are
        the only thing that moves her. (An earlier note here claimed discarding it was a reason
        NOT to extract. It is the reason TO.)

        Off for a clip that is not driving her anywhere - the idle's weight shifts, the crouch's
        squat, Twirl's authored step back - because there the translation IS the performance, and
        pinning it flattens the thing worth looking at.

        f_extract_lift is the same question for Y, and is separate because the answer usually differs: a
        gait's vertical is a 0.02-unit footfall bob that must stay on the bone, while a climb's is
        a metre of genuine rise that Stage is also applying. Extraction pins the axis to the BIND
        pose, so setting this on a run cycle does not merely remove drift - it flattens the bounce.
    */
    bool  f_extract_move;   //X/Z
    bool  f_extract_lift;   //Y
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
    HOW HARD SHE HIT, in units per second downward, and what each band is worth animating.

    Measured against the arcs this game actually produces: a tapped jump lands at 8.6, a full one
    at 18.7 (apex 3.07 units under ARCHER_FALL_GRAVITY_MUL), and a drop of 5.5 units arrives at 25.
    So a routine jump is deliberately SOFT - the dramatic landing is for falling off something,
    not for using the jump button, or it plays constantly and stops reading as dramatic.

    Below PUPPET_LAND_VEL there is no landing clip at all. Stepping off a kerb is not a landing,
    and playing a recovery for it would put a hitch in ordinary walking.
*/
#define PUPPET_LAND_VEL             5.0f
#define PUPPET_HARD_LAND_VEL        25.0f

/*
    HOW LONG THE GAME'S RISE LASTS, derived rather than typed: a jump leaves the ground at
    ARCHER_JUMP_SPEED and is pulled down by ARCHER_GRAVITY, so it stops climbing after v/g seconds.
    0.390s at today's numbers, and the running jump's own rise is 0.367s - a 0.94x fit, the closest
    anything in this file gets to 1.0. Written as a division so that retuning the jump moves the
    fit with it instead of letting the clip drift quietly out of step.
*/
#define PUPPET_RISE_TIME            (ARCHER_JUMP_SPEED / ARCHER_GRAVITY)

/*
    How fast she has to be going at TAKEOFF for the jump to be a running one.

    A threshold and not a blend, which is the opposite of the choice made on the ground. Two gaits
    can be mixed because they are the same cycle at different speeds; Jump_ToAir is 0.267s of tuck
    and Running_Jump is a 0.933s arc, so a shared playhead between them would mean nothing. Two
    discrete sets chosen once is the honest shape.

    LATCHED AT TAKEOFF, which is the part that matters - see Puppet::air_clip. Air friction is
    14 u/s^2, enough to take her from a full run to a standstill inside a single flight, so a
    choice re-made every tick would swap clips in mid-air.

    Set at the slow run's world speed: jogging jumps like a runner, a shuffle jumps like a stander.
*/
#define PUPPET_RUN_JUMP_SPEED       2.90f

/*
    THE RUN-TO-STOP, and the two numbers that decide when it plays.

    PUPPET_STOP_FROM_SPEED is how fast she has to have been going for stopping to be worth
    animating. Below it she was not running, she was shuffling, and a plant-and-settle on the end
    of a walk is a hitch rather than a flourish.

    PUPPET_STOP_TIME is how long the RULES take to stop her from a full sprint, derived rather than
    typed: ARCHER_RUN_FRICTION removes ARCHER_RUN_SPEED in v/a seconds. 0.075s - five ticks - which
    is the number the clip has to be fitted into and is nothing like long enough. See the gap that
    Choose reports.
*/
#define PUPPET_STOP_FROM_SPEED      2.90f
#define PUPPET_STOP_TIME            (ARCHER_RUN_SPEED / ARCHER_RUN_FRICTION)

/*
    HOW MUCH SPEED FRICTION CAN POSSIBLY REMOVE IN ONE TICK, and therefore the line between
    stopping and being stopped.

    This is the whole discriminator between a run-to-stop and a run-into-a-wall, and it is not a
    tuned threshold - it is a fact about the rules. Deceleration on the ground is
    ARCHER_RUN_FRICTION, so a tick can take at most this much off. A drop bigger than it did not
    come from letting go of the key; something was in the way. Measured: releasing at a full run
    steps 9 -> 7 -> 5 -> 3 -> 1 -> 0, exactly this much each tick, while a wall goes 9 -> 0 in one
    and a crate clamps to ARCHER_PUSH_SPEED in one.

    Stage computes `f_hit_wall` and currently throws it away, and publishing it would be the exact
    answer rather than this inferred one. It is not needed while the only question is which of
    these two clips to play, and it becomes worth doing when a wall-stop clip wants an impact speed
    to pick a soft or hard variant with.
*/
#define PUPPET_FRICTION_STEP        (ARCHER_RUN_FRICTION * ARCHER_DT)

/*
    The line between rising and falling, in units per second.

    Zero, and deliberately not a band around it. The apex is the one moment where a hold pose and
    the top of a jump arc look the same, so a clip change costs nothing there - which is exactly
    where a hysteresis band would be spent to avoid a flicker that cannot happen anyway, since
    vel_y passes through zero once per jump and never returns.
*/
#define PUPPET_RISE_VEL             0.0f

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
    /*
        The second clip of a blend, or -1 for a single one.

        `blend` is the weight toward it, 0..1, and `blend_phase_offset` is what brings the two
        clips' footfalls into step. Together these are the blend space: between two rungs of the
        locomotion ladder the answer is both of them and a weight, so there is no transition
        between walking and running to interrupt - the weight simply moves.
    */
    int   blend_clip = -1;
    float blend = 0.0f;
    float blend_phase_offset = 0.0f;
    float rate = 1.0f;              //what to play it at, after clamping
    float wanted_rate = 1.0f;       //what matching the feet to the ground actually asked for
    /*
        Where in the clip to BEGIN, in seconds, or negative for "wherever it already is".

        Only the jump uses it, and only because Jumping_Up is authored as a whole standing jump -
        anticipation crouch, launch, apex, fall, landing absorb, recovery - while this game's jump
        leaves the ground on the tick the button goes down. Starting that clip at zero would have
        her tucking into a crouch while already travelling upwards. So the rise starts at the
        measured launch instead, and the 0.43s of anticipation in front of it is simply not played.

        Applied on ENTRY only. A clip already running is left alone, or the playhead would be
        pinned to the start frame every tick.
    */
    float start_time = -1.0f;
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

    /*
        Where in each clip's cycle the LEFT FOOT is planted, as a fraction 0..1, measured at load.

        This is what lets two cycles be blended without the feet skating. Clips are authored with
        their footfalls wherever the animator happened to start, and this export's three locomotion
        clips plant the left foot at 0.83, 0.69 and 0.62 of their cycle - up to a fifth of a cycle
        apart. Mixing them on a shared playhead without correcting for that puts a left-foot-down
        pose against a mid-stride one, which is the classic blend-space skate. The difference
        between two clips' values IS the phase offset the blend needs, so nothing has to be
        re-authored to line them up.
    */
    float clip_phase[CLIP_COUNT] = {};

    //Net yaw each clip turns her through, in DEGREES, measured at load. Reported rather than acted
    //on: it is what says whether a clip's f_turns column is set right. A cycle reads near zero
    //however much its hips swing on the way, because the swing comes back.
    float clip_turn_deg[CLIP_COUNT] = {};

    //Each clip's length in seconds, also read from the clip at load. The preview panel shows it,
    //and it is what a ONE-SHOT clip's rate is matched against - an action has a duration to fit,
    //where a locomotion cycle has a stride to fit.
    float clip_duration[CLIP_COUNT] = {};

    /*
        HOW FAR INTO EACH CLIP THE GAME'S STATE ACTUALLY BEGINS, in seconds. Zero for almost
        everything; the landings are why it exists.

        A landing clip authored on its own starts in the air and falls to the floor, because that
        is what a landing looks like to an animator. FallingIdle_ToLanding descends 0.52 world
        units before its feet touch. But by the time this game plays it she is ALREADY standing on
        the ground - Stage put her there, that is what triggered the landing - so those frames
        describe a fall that has happened, and playing them sinks her half a body into the floor
        and pops her back out.

        So the clip is entered at its own moment of contact, found by posing the model and watching
        the toe (ApplicationArcher::MeasureLandingClips). Same shape of answer as the jump's
        anticipation: the clip is right, the part of it this game can use starts later.

        Measured rather than declared, so a re-export trimmed to start at contact simply measures
        zero here and nothing needs changing.
    */
    float clip_entry[CLIP_COUNT] = {};

    /*
        THE LANDING, which is the one piece of animation state the Puppet has to remember.

        Everything else here is a pure function of the current ArcherAnimParams - the rules say
        "running at 4.2 units/s" and the answer follows. A landing is not: it is an EVENT, fired by
        the tick where f_on_ground goes true, and then it owns the character for as long as the
        clip lasts. So it needs a countdown, and the impact speed has to be remembered from the
        tick BEFORE contact because Stage has zeroed vel_y by the time the landing is visible.
    */
    /*
        A SETTLE is a one-shot the animation layer holds after an event: landing, and now stopping.
        Both are the same shape - fired by a transition the rules made, held for the clip's length,
        and abandoned the moment she moves again - so they share one slot rather than two.

        `last_vel_y` and `last_ground_speed` are read one tick late on purpose. Stage zeroes both on
        contact, so by the time a landing or a wall stop is visible the number that says how hard it
        was has already gone.
    */
    bool  f_was_on_ground = true;
    float last_vel_y = 0.0f;
    float last_ground_speed = 0.0f;
    int   settle_ticks = 0;
    int   settle_clip = -1;

    /*
        WHICH AIR SET THIS FLIGHT IS USING, latched on the tick she leaves the ground.

        Not re-decided in the air, and that is the whole reason it is remembered rather than
        derived. ARCHER_AIR_FRICTION is 14 u/s^2, so letting go of the run key at takeoff bleeds a
        full 9 u/s off inside about two thirds of a second - less than one flight. A choice made
        from the current ground_speed would therefore cross PUPPET_RUN_JUMP_SPEED in mid-air and
        swap a running jump for a standing one halfway through the arc.

        CLIP_RUN_JUMP for a running jump; -1 for the standing set, which then splits on vel_y.
        Set at takeoff whether she jumped or simply walked off a ledge, because both are flights.
    */
    int   air_clip = -1;

    /*
        How long Running_Jump spends climbing, in seconds - its start to its highest hip. MEASURED
        at load, because it is what the clip is fitted to: rate = this over PUPPET_RISE_TIME.

        Only the RISE is fitted. The clip's descent is 0.566s against the game's 0.327s, so no
        single rate matches both halves, and the rise is the half worth matching - it is the part
        with the push in it, and it is the part whose length the rules actually guarantee.
    */
    float run_jump_rise = 0.0f;

    //Where Running_ToStop plants, in seconds - its lowest hip. MEASURED at load, and the beat the
    //clip is fitted by: the plant should land near the tick the rules actually bring her to rest.
    float stop_plant = 0.0f;

    /*
        WHEN THE BOOT ACTUALLY CONNECTS in Kick_Front, in seconds - the frame where a foot is
        furthest from the hips. MEASURED at load, and the only way to keep the rules' active window
        pointed at the right moment of the clip.

        The rules cannot read it: KICK_ACTIVE_FROM and KICK_ACTIVE_TO are compile-time constants in
        Stage.h, which names no engine type and has never seen a .glb. So this does not SET the
        window, it CHECKS it - the app logs the clip's strike beside the window the rules use, and a
        re-export that moves the impact shows up as a number that no longer lines up rather than as
        a kick that connects before the leg has moved.
    */
    float kick_strike = 0.0f;

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
    /*
        BOTH ENDS OF A FLIGHT: latch the air set at takeoff, then fire, run down and cancel the
        landing at the other end. Called by Tick BEFORE Choose, because Choose is a pure read of
        this state and of `in` - which is what keeps it testable and what lets the panel drive it.

        CANCELLED BY MOVING, deliberately. The rules do not stun her on landing, so she can run the
        instant she touches down; an animation that held her through a 1.1s recovery would be the
        animation layer overruling the game. Letting go the moment ground_speed picks up is the
        cheap version of step 6's cancel windows, and it is the honest one until those exist.
    */
    void UpdateAir(const ArcherAnimParams& in);

    //The clip choice on its own, without touching the yaw. Pure - the rules test calls this.
    PuppetChoice Choose(const ArcherAnimParams& in) const;
    //The rate Running_ToStop plays at, after the clamp. Shared so UpdateAir holds it for exactly
    //as many ticks as it will actually take.
    float StopRate() const;

    //A clip's forward speed in WORLD units per second, or 0 if it does not travel.
    float WorldClipSpeed(int clip) const;

    //How far a clip covers in ONE cycle, in world units. The blend of two cycles advances on a
    //shared normalised phase at an interpolated duration, so the speed it produces is the
    //interpolated STRIDE over the interpolated duration - not the interpolation of the two
    //speeds, which is a different and slightly wrong number.
    float WorldClipStride(int clip) const;
    //What a blend of two clips at this weight covers per second, which is what the rate has to
    //match against. Handles blend_clip < 0 as "just the one".
    float BlendedSpeed(int clip, int blend_clip, float blend) const;

    //Which way the model should be facing for this `facing`, with no slew.
    static float TargetYaw(float facing);
};

#endif
