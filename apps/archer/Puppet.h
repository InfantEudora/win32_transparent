#ifndef _ARCHER_PUPPET_H_
#define _ARCHER_PUPPET_H_

#include "Stage.h"

#include <vector>

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
    CLIP_ROPE,              //Hanging_Rope           gripping a rope, both hands overhead
    CLIP_STOP,              //Running_ToStop         plant and settle out of a run
    CLIP_KICK_SPIN,         //Kick_FrontSpin         the spinning kick; a real pivot
    CLIP_KICK_2,            //Kick_Front_2           Down+K - see KICK_FRONT_2 in Stage.h
    CLIP_KICK_3,            //Kick_Front_3           Up+K - see KICK_FRONT_3
    CLIP_HANDSTAND,         //Walk_ToHandstand       set dressing; preview only
    CLIP_JUMP_IN_PLACE,     //Jumping_InPlace        a whole standing jump; preview only
    CLIP_JUMP_FORWARD,      //Jump_Forward           a whole travelling jump; not wired
    CLIP_DRAW,              //Standing_DrawArrow     the upper layer's draw, pinned to the rules', over any base
    CLIP_STRETCH2,
    CLIP_AIM_IDLE,          //Standing_AimArrowIdle  held at full draw, looping; follows the draw
    CLIP_OVERDRAW,          //Standing_OverdrawArrow pulled past full draw; preview only for now
    /*
        THE KNEEL, as three pieces: down, held, up. All three hold a RIFLE in the arms, which is
        why the upper layer is on for the whole of a kneel - the legs are the clips', the arms are
        the draw's first frame (Puppet::ChooseUpper).
    */
    CLIP_KNEEL_DOWN,        //Stand_ToKneel          standing -> one knee, fitted to KNEEL_DOWN_TICKS
    CLIP_KNEEL_IDLE,        //Kneel_Idle             held on one knee, looping
    CLIP_KNEEL_UP,          //Kneel_ToStand          one knee -> standing, fitted to KNEEL_UP_TICKS
    CLIP_LAYING_UP,         //Laying_StandingUp      the level entry, MODE_GETUP; plays whole
    CLIP_ROPE_CLIMB,        //Rope_Climbing          hand over hand; playhead pinned to the distance climbed
    CLIP_TEETER,            //Teeter_Forward         stopped past a lip, falling FORWARD over it
    CLIP_BALANCE_WALK,      //Balance_Walking        on a branch; playhead pinned to the distance walked
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

    /*
        The part of the clip this game plays, in SECONDS of the export (30 fps, so frame n is
        n/30), cut at load by Animation::Trim before anything measures the clip. Left off a row,
        both are 0 and the whole clip plays; trim_end <= 0 means "to the end".

        A trim moves every measurement taken off the clip, which is the point - but it does not
        move the constants in Stage.h that were fitted to the untrimmed one. The kick's are the
        ones that care, and MeasureKickClip says so at load if they have been left behind.
    */
    float trim_start = 0.0f;
    float trim_end = 0.0f;
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
    THE FASTEST THE ROPE CLIMB'S PLAYHEAD MAY CATCH UP, as a multiple of the clip's own speed.
    Mapped purely by distance, a pause in the hips - Rope_Climbing has one at the end of its cycle,
    0.28s in which the right hand reaches up - is passed in a single tick, and the reaching hand
    snapped half a metre in two frames. Capped, the pause plays out over a few ticks while the
    playhead lags the distance by a few centimetres of climb. It must stay well above the average
    rate a steady climb needs (1.97 at ROPE_CLIMB_SPEED today) or the lag would never close; the
    Puppet raises it to 1.5x that average if the climb speed ever outgrows it.
*/
#define PUPPET_CLIMB_RATE_MAX       3.50f

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
    And it is a JUMP'S speed - running off an edge at any speed is a fall (Puppet::air_clip).
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
    THE TEETER: standing still with her centre past a lip she is facing, over a real drop - and
    LOOKS ONLY. The rules do not know it is happening: she is as supported as ever (her box holds
    until its far side leaves the corner, ARCHER_HALF_W past the lip), pushing on walks her off
    and pulling back steps her away, exactly as before. Teeter_Forward is her falling FORWARD over
    the lip; the thin branch's sideways loss of balance is a different clip and a different system.

    WHERE SHE ENDS UP PAST THE LIP is the whole trigger, and the rules already put her there: a
    stop from a full run carries her 0.34 on, so letting go at the lip leaves her standing over it.
    Only just - 0.35 of release point is two ticks at a sprint - so it is mostly reached by
    creeping up, a tap or the stick at a time.

    PUPPET_TEETER_FROM is how far past the lip her centre has to be, which is where the drawn turf
    decides it: past its lip the grass reads as floor, so tune it by eye. The drop is fear's own
    line, so a teeter and the fear of that edge start at the same height. PUPPET_TEETER_REACH is how
    far short of the lip DescribeArcher still reports the edge, for the panel and the tests.
*/
/*
    HOW MUCH OF THE BRANCH LEAN THE WHOLE BODY TILTS BY. Stage::lean is the rules' - it is what
    she falls off at, BALANCE_FALL_DEG, and what the gauge shows - and the model used to roll by
    all of it, which was the only thing saying "balancing" before there was a clip for it. With
    Balance_Walking's arms out doing that, half the tilt reads as the same danger without laying
    her over. The rules are untouched; this is only how far the drawn body goes with them.
*/
#define PUPPET_BRANCH_LEAN_SHARE    0.5f

#define PUPPET_TEETER_FROM          0.0f
#define PUPPET_TEETER_DROP          VITALS_DROP_FROM
#define PUPPET_TEETER_REACH         0.5f

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

/*
    How long the AIM takes to take hold of her body, and to let go of it, in ticks.

    The aim override (animation_plan.md, Step 3) bends her spine and shoulders to point the bow
    along aim_deg - but only while she is in the draw pose, since bending a running torso that is
    not holding a bow up is wrong. This is the ease between the two, so starting or ending a draw
    does not snap her chest through the whole aim angle in one frame. Six ticks is 0.1s: well
    inside BOW_DRAW_TICKS' 36, so the aim is fully on long before the bow is fully drawn.
*/
#define PUPPET_AIM_BLEND_TICKS      6
//And how long the upper-body layer takes to come on and go off. The same 0.1s: the draw's first
//frames are the arm swinging back to the quiver, which reads fine arriving over six ticks.
#define PUPPET_UPPER_BLEND_TICKS    6
//The loose legs (Puppet::leg_weight) come on and go off over a quarter second: slower than the
//arms, because the climb clip hands its feet over to the swing rather than snapping between them.
#define PUPPET_LEG_BLEND_TICKS      15
//The fall pose (Puppet::fall_weight): full at this fall speed - just over a full jump's 19, so a
//routine jump never quite gets there and the whole pose is kept for real drops - and eased over a
//crossfade's worth of ticks (0.15s, animation_transition_time_max).
#define PUPPET_FALL_POSE_VEL        20.0f
#define PUPPET_FALL_BLEND_TICKS     9
/*
    The most of it that is ever laid on - and it is ZERO, which switches the fall pose off.

    It was written for a Falling_Idle that stood still. The one exported now is a real 0.917s loop,
    one arm up and the legs cycling, and the pose froze it: at 1.0 every bone was slerped onto one
    frame, so a long drop was a photograph again, and 0.5 only halved motion that is gentle to start
    with. The reach for the ground it added is the hard landing's lead-in's job, which starts 0.37s
    out. Compared side by side on the rope scene's x 23, y 100 drop at 0, 0.5 and 1 (2026-09-29).

    Left as a dial rather than deleted: the overlay is also the natural way to lean a clip toward a
    pose - a teeter, a catch - and the easing it rides on is tested at any ceiling above zero.
*/
#define PUPPET_FALL_POSE_MAX        0.0f
//How far the pump swings her legs toward the way she is pushing, in degrees about the camera
//axis. A real swinger pumps WITH the legs; this is the input made visible, and the chain's spring
//turns the step into a kick.
#define PUPPET_LEG_PUMP_DEG         25.0f
/*
    How much of the world's gravity the loose legs feel. All of it hanging: Hanging_Rope's legs are
    loose, and dead weight is what makes them trail and float. A fifth of it stopped mid-climb,
    where the pose is her FEET GRIPPING THE ROPE: at full gravity the chain sagged them 12-27
    degrees off it, a climber letting go with her feet; held up by the pose's own muscle they stay
    on, and the swing still moves them.
*/
#define PUPPET_LEG_GRAVITY_HANG     1.0f
#define PUPPET_LEG_GRAVITY_GRIP     0.2f

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
    int   kick_kind = 0;            //KickKind, while action is ACTION_KICK
    float aim_deg = 0.0f;
    float aim_side_deg = 0.0f;      //Stage::AimSwaySideDeg - the cone's sideways half, + to her left
    float draw_power = 0.0f;        //0..1
    int   kneel_phase = -1;         //KneelPhase while mode is MODE_KNEEL, else -1
    //On the rope: which way she is climbing this tick (+1 up, -1 down, 0 not), and how far she has
    //climbed since catching it, signed - up is +. Stage::rope_climb / rope_climbed.
    int   rope_climb = 0;
    float rope_climbed = 0.0f;
    float rope_pump = 0.0f;         //-1..1 along world X, the lean pushing the swing. Stage::rope_pump
    /*
        Her next landing as Stage::PredictLanding forecasts it: in how many ticks (1 = the next
        one; -1 when none is in sight) and how hard. Not the rules' state, so DescribeArcher cannot
        fill it - the forecast needs this tick's input - and the app does.
    */
    int   land_in_ticks = -1;
    float land_speed = 0.0f;
    //Hanging from a branch rather than a ledge: nothing to brace the feet on, so the rope's grip.
    bool  f_free_hang = false;
    /*
        The lip she is FACING on the floor she stands on, from Stage::edges: how far her centre is
        past it (negative: short of it; -1 with edge_drop 0 when there is none within
        PUPPET_TEETER_REACH), and how far it drops. What the teeter is decided on.
    */
    float edge_over = -1.0f;
    float edge_drop = 0.0f;
    //Standing on a branch (Stage::branch_on), feet on it rather than hanging from it.
    bool  f_on_branch = false;
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

//The clip each KickKind plays, in KickKind order.
extern const int PUPPET_KICK_CLIP[KICK_KIND_COUNT];

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

    /*
        THE UPPER-BODY LAYER (animation_plan.md, Step 2): a clip for the spine and up, over
        whatever `clip` does with the legs. -1 for none. `upper_phase` is where in it to sample,
        0..1 of its length, PINNED to the rules' draw progress - or negative for a loop that runs
        on its own clock (the hold at full draw). Its weight is Puppet::upper_weight.
    */
    int   upper_clip = -1;
    float upper_phase = -1.0f;
    /*
        While the layer CROSSFADES between two of its own poses, the clip it is leaving, else -1;
        the mix toward upper_clip is Puppet::upper_mix. The app samples the leaving clip at the
        time it last showed it, frozen - see Puppet::upper_xfade_serial.
    */
    int   upper_from_clip = -1;
    /*
        The base clip's playhead, SET rather than advanced, in seconds - or negative for a clip on
        its own clock. The rope climb uses it: the pose is chosen by how far she has climbed, not by
        how long she has been climbing. `rate` is 0 whenever this is set.

        `lift` goes with it: how far the chosen pose has raised her hips above the clip's first
        frame, in world units. Rope_Climbing is not in place - the hips rise 0.28 rig units over a
        cycle while the gripping hand stays still - so the app LOWERS the model by this along her
        up, which leaves the hips where the rules' body is and the gripping hand where the rope is.

        Precisely, `lift` is how far she has climbed since the START of the cycle the pose is in,
        `lift_base` the distance climbed at that start: the rise the pose carries while the playhead
        keeps up, and a little more while it lags a pause (PUPPET_CLIMB_RATE_MAX). Measured that way
        the gripping hand stays on the rope even then - the hips sag the few centimetres instead.
        The app lowers a pose by rope_climbed - lift_base, the tick it is SHOWN.
    */
    float pinned_time = -1.0f;
    float lift = 0.0f;
    float lift_base = 0.0f;
    /*
        A WHOLE-BODY OVERLAY: one clip held at one time, blended over the base pose at a weight -
        ArcherModel::ApplyOverlay. The fall pose (Puppet::fall_weight) is what uses it: the hard
        landing's airborne opening, both arms up and legs reaching, over Falling_Idle. -1 for none.
    */
    int   overlay_clip = -1;
    float overlay_time = 0.0f;
    float overlay_weight = 0.0f;
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
    /*
        And where the RIGHT foot is planted, measured the same way off the other toe. Not assumed to
        be half a cycle on: a gait is not quite symmetric, and a footstep heard a few frames off the
        foot it belongs to is exactly the kind of wrong nobody can name. The phase sync above only
        needs the left; the footsteps need both (ApplicationArcher::SignalFootsteps).
    */
    float clip_phase_right[CLIP_COUNT] = {};

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
        And the other end: HOW FAR INTO A TRANSITION CLIP THE MOVE IS DONE, in seconds - zero for
        "all of it". The kneel set is why: Stand_ToKneel has her down by 0.7s and then holds still
        for another 0.8, so the rules are timed to the drop (KNEEL_DOWN_TICKS) and the still tail
        is never played - the base crossfades on to Kneel_Idle, which holds the same pose. The
        mirror image of clip_entry, found by watching the hip settle
        (ApplicationArcher::MeasureKneelClips).
    */
    float clip_settle[CLIP_COUNT] = {};

    /*
        THE ROPE CLIMB, as distance rather than time. `climb_times` are the clip's keyframe times and
        `climb_rise` how far its hips have risen by each, in WORLD units from the first - measured at
        load (ApplicationArcher::MeasureRopeClimb) and made monotonic, because the playhead is found
        by inverting it. `climb_cycle_rise` is one whole cycle's rise; 0 means no clip to climb with.

        WHY DISTANCE. The clip's rise is not steady: the hips pause for 0.2s at mid-cycle and slow
        toward the end, while the rules climb at one speed. Played by time, the gripping hand would
        slide by the difference; played by distance, every frame is the pose that belongs to how far
        up she is, so the hand that holds the rope stays on it. Down is the same curve run backwards.
    */
    std::vector<float> climb_times;
    std::vector<float> climb_rise;
    float climb_cycle_rise = 0.0f;
    //Latched on the first tick she climbs; cleared when she leaves the rope. Once she has climbed
    //she holds the climb's pose where she stopped - hands staggered on the rope - rather than
    //crossfading back to Hanging_Rope, whose two hands are at one height.
    bool  f_rope_climbing = false;
    /*
        The climb's playhead, UNWRAPPED - whole cycles times the clip's duration plus the time in
        this one - so a cycle boundary is a number going past a multiple rather than a jump back to
        0 that could not be told from climbing down. It follows the distance's own time (the
        target) no faster than PUPPET_CLIMB_RATE_MAX; see UpdateRope.
    */
    bool  f_climb_playhead = false;
    float climb_playhead = 0.0f;
    float climb_target = 0.0f;          //where the distance alone would put it, for the panel

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

        ONLY A JUMP GETS THE RUNNING JUMP. Running_Jump opens on a push off one foot, and running
        off a ledge used to play exactly that - a takeoff she never made - and then hold its last
        frame for the rest of a fifteen-unit drop. What tells the two apart is vel_y on the tick she
        leaves: a jump leaves rising, a walk-off leaves already falling (-0.95 measured). A jump in
        the coyote window after walking off is the same push, only a few ticks late, so it latches
        too - see UpdateAir. Letting go of a rope or a ledge never does: that is no push at all.
    */
    int   air_clip = -1;
    /*
        THE FLIGHT'S CLOCK. `f_was_flying` is last tick's "in the air and holding nothing", so a
        flight starting is an edge whatever it started from; `air_ticks` counts from that edge,
        which is what bounds the coyote latch above.

        `run_jump_time` is where Running_Jump's playhead has got to, in seconds of the clip - it
        runs at RunJumpRate from its first frame. When it reaches the end the arc is SPENT and the
        flight goes over to the standing set: Falling_Idle, then the landing's lead-in. A running
        jump into a pit used to hold the clip's last frame all the way down and land without one.
    */
    bool  f_was_flying = false;
    int   air_ticks = 0;
    float run_jump_time = 0.0f;

    /*
        THE TEETER'S CLOCK: ticks she has stood still at the lip, -1 when she is not at one. The
        clip plays ONCE from its first frame - tip, wobble, the throw back upright, settled - and
        then she idles there, so standing at a ledge is not a six-second loop.

        `f_teeter_spent` keeps it from starting again for as long as she stays: set when the clip
        has played out, and when the bow or a kick took over before it did, because aiming down
        from a ledge is a thing she will do, and every loose would otherwise set her wobbling
        again. Stepping off the spot - moving at all - re-arms it.
    */
    int   teeter_ticks = -1;
    bool  f_teeter_spent = false;

    /*
        HOW FAR SHE HAS WALKED ALONG THE BRANCH she is on, signed along facing - forward is + - and
        0 on the tick she steps onto it. Balance_Walking's playhead is this over the clip's own
        speed, wrapped: her feet stay where they were put, standing still holds the step she is in,
        and backing up plays it backwards. The rope climb's arrangement, for the same reason, with
        a straight line in place of the climb's measured curve - the walk's pace is even.
    */
    float branch_walked = 0.0f;

    /*
        THE LANDING'S LEAD-IN (animation_plan.md, *Meeting the ground*). While the forecast landing
        is closer than a landing clip's contact frame, that clip plays in the air, its playhead
        pinned so the contact frame falls on the contact tick. `lead_clip` is the one playing, -1
        for none; at contact it IS the landing, so the choice made in the air is not re-made from
        a speed read one tick apart and flipped on the tick she touches down.

        The lead-in's authored descent - FallingIdle_ToLanding's hips come down 0.5 before its feet
        touch - is taken back out by the app, which keeps the hips at standing height on the body
        all through a flight (ApplicationArcher::SyncArcherAnimation, `air_hip_weight`).
    */
    int   lead_clip = -1;

    /*
        THE FALL POSE, 0..1: how much of the hard landing's airborne opening (arms up, legs
        reaching) is laid over Falling_Idle. Written when that clip was a photograph - 0.0005 of
        hip motion in 0.733s - and OFF since the re-export gave it a loop of its own: see
        PUPPET_FALL_POSE_MAX, which caps it. Aims at PUPPET_FALL_POSE_MAX times
        smoothstep(fall speed / PUPPET_FALL_POSE_VEL) and eases there over PUPPET_FALL_BLEND_TICKS,
        so it grows as she speeds up and fades out, crossfade-length, when a lead-in or anything
        else takes over. Standing set only, falling only, and not while a lead-in plays.
    */
    float fall_weight = 0.0f;

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
        WHEN THE BOOT ACTUALLY CONNECTS in each kick's clip, in seconds, by KickKind - the frame
        where a foot is furthest from the hips. MEASURED at load, and the only way to keep the
        rules' active windows pointed at the right moment of each clip.

        The rules cannot read it: KICK_SPECS is a compile-time table in Stage.h, which names no
        engine type and has never seen a .glb. So this does not SET a window, it CHECKS it - the
        app logs each clip's strike beside the window the rules use, and a re-export that moves the
        impact shows up as a number that no longer lines up rather than as a kick that connects
        before the leg has moved.
    */
    float kick_strike[KICK_KIND_COUNT] = {};

    //The uniform scale the model is drawn at. A clip's speed in WORLD units is its measured speed
    //times this, which is why the two have to be known together - a rig authored half-size walks
    //half as fast even though nothing about the clip changed.
    float model_scale = 1.0f;

    //Stretch playback to keep the feet planted, or play every clip at 1.0 and let them slide.
    //A toggle rather than a decision because seeing both is the point of the first animation pass.
    bool  f_match_feet = true;

    //Where the model is facing right now, in degrees, slewed toward the side `facing` asks for.
    float yaw_deg = PUPPET_YAW_RIGHT;

    /*
        How much of the aim angle her body takes, 0..1, eased over PUPPET_AIM_BLEND_TICKS.

        1 only while the chosen clip is a draw pose (IsDrawPose: the draw, and the hold at full
        draw that follows it) - the poses the aim override is written against.
        Keyed on the clip rather than on ACTION_DRAW on purpose: a draw started at a run is still a
        run cycle today (step 2's mask layer is what changes that), and bending that would lean a
        running torso that is not holding a bow up. When the mask layer lands, the draw is on the
        upper body during every clip, and this becomes "is the upper-body draw playing".
    */
    float aim_weight = 0.0f;

    //The upper layer's weight, 0..1, eased over PUPPET_UPPER_BLEND_TICKS - on while she draws, in
    //any stance. `upper_latched` keeps the last upper clip on while the weight fades back out,
    //so letting go does not snap the arms to whatever the legs are doing.
    float upper_weight = 0.0f;
    int   upper_latched = -1;

    /*
        THE LOOSE LEGS, 0..1, eased over PUPPET_LEG_BLEND_TICKS: how much of the legs' dynamic chain
        (core/DynamicChain, ArcherModel::ApplyLegChains) replaces the clip's own legs. And
        `leg_lead_deg`, how far the pump turns the pose the chain springs toward - world degrees
        about the camera axis, + toward +X. The chain itself is view state; these two are the only
        decisions in it, and they are made here so `make rules` checks them.
    */
    float leg_weight = 0.0f;
    float leg_lead_deg = 0.0f;
    float leg_gravity = PUPPET_LEG_GRAVITY_HANG;   //the share of gravity - see PUPPET_LEG_GRAVITY_GRIP

    /*
        THE LAYER'S OWN CROSSFADE, for when its pose would otherwise JUMP with the weight still on.
        Kneeling is what needs it: the layer stays on after a release, so the hold (or a draw
        cancelled halfway) has to ease back to the resting frame, where standing the whole layer
        simply fades out. A jump is a change of clip, or the draw's phase going BACKWARDS; the draw
        handing over to the hold is authored seamless and is not one, and neither is a draw
        starting from the rest pose, which is its own first frame.

        `upper_mix` runs 0 -> 1 over PUPPET_UPPER_BLEND_TICKS; `upper_xfade_serial` counts
        crossfades, so the app knows when to freeze the leaving clip's time even when a second one
        starts before the first has finished.
    */
    float upper_mix = 1.0f;
    int   upper_xfade_serial = 0;
    int   upper_from = -1;
    int   upper_prev_clip = -1;
    float upper_prev_phase = -1.0f;

    PuppetChoice choice;

    /*
        What it carries from one tick to the next - the choice, the yaw, the weights, the settle and
        crossfade memory - into a replay's state hash (core/StateHash.h), as the part `puppet`. The
        measured tables (clip_speed, clip_phase...) are fixed once the clips are loaded and left out.
        A new field that outlives a tick belongs here too.
    */
    void HashState(class StateHash& hash) const;
    /*
        Forget everything carried from earlier ticks - the same fields HashState covers - as a
        fresh Puppet would have it, keeping what was measured off the clips. `facing` is where she
        now faces, so the yaw starts there rather than turning round on the first tick. For a
        replay's restore: without it the choice, the settle and the crossfade memory came from
        whatever ran before the replay (docs/replay_determinism_plan.md, section 8).
    */
    void Reset(float facing);

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
    //Latches f_rope_climbing and moves the climb's playhead. Called by Tick before Choose, like
    //UpdateAir.
    void UpdateRope(const ArcherAnimParams& in);
    //Runs the teeter's clock. After UpdateAir, whose stop it takes over and whose landing it
    //waits for.
    void UpdateTeeter(const ArcherAnimParams& in);
    //Walks branch_walked along, or zeroes it off a branch.
    void UpdateBranch(const ArcherAnimParams& in);
    //Is she standing where a teeter belongs - on the ground, past a lip she faces, over a real
    //drop? Where only, not whether she is still: pure, from `in`.
    static bool AtLip(const ArcherAnimParams& in);
    //Is the teeter on screen this tick? Reads the clock; Choose asks it.
    bool  TeeterPlaying(const ArcherAnimParams& in) const;
    /*
        The climb's playhead for a climbed distance, in seconds - wrapped into one cycle either way,
        so climbing down past where she caught the rope keeps cycling backwards - and in `lift` the
        rise that pose carries, which is the distance's remainder in the cycle. -1 without a curve.
    */
    float ClimbTimeAt(float climbed, float& lift) const;

    //The clip choice on its own, without touching the yaw. Pure - the rules test calls this.
    PuppetChoice Choose(const ArcherAnimParams& in) const;
    //The rate Running_ToStop plays at, after the clamp. Shared so UpdateAir holds it for exactly
    //as many ticks as it will actually take.
    float StopRate() const;
    //And the rate Running_Jump plays at, shared the same way: UpdateAir needs it to know when the
    //arc has played out.
    float RunJumpRate() const;

    //A clip's forward speed in WORLD units per second, or 0 if it does not travel.
    float WorldClipSpeed(int clip) const;

    //The landing a touchdown at `speed` gets: CLIP_LAND_HARD, CLIP_LAND_SOFT, or -1 for none.
    static int LandingClipFor(float speed);
    //The landing whose lead-in should be playing now, from the forecast in `in`; -1 for none.
    int   LeadInClip(const ArcherAnimParams& in) const;
    //Where fall_weight is heading this tick. Pure.
    float FallPoseTarget(const ArcherAnimParams& in) const;

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

    //Is this one of the bow-up poses - the draw or the hold at full draw? The aim override bends
    //these, and the string follows the hand in them. Overdraw joins when it is wired.
    static bool IsDrawPose(int clip);

    //The upper layer's clip and phase for these parameters - the draw pinned to the rules'
    //progress, then the hold; kneeling and not drawing, the draw's first frame. Pure, like
    //Choose; Tick adds the weight, the fade-out latch and the crossfade.
    static void ChooseUpper(const ArcherAnimParams& in, PuppetChoice& out);

    //Is an arrow on the string, judged from the draw's progress the way Stage::IsNocked judges it
    //from draw_ticks? The aim takes hold only from here (animation_plan.md, Step 2, the live
    //neutral).
    static bool IsNocked(const ArcherAnimParams& in);
};

#endif
