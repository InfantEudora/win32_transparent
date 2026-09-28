#include "Puppet.h"
#include "StateHash.h"

#include <math.h>

/*
    See Puppet.h for what this is and where the seam falls. This file is the decisions.
*/

/*
    The clip table.

    NAMES ARE THE CONTRACT with the .glb - Object::FindAnimation matches on the string, and a name
    that does not resolve fails SILENTLY: the clip is simply never found, the model keeps playing
    whatever it was playing, and nothing says so. ApplicationArcher::BuildArcherModel reports every
    one of these it could not find for exactly that reason.

    `f_travels` marks the clips whose hip track carries real forward motion. Both walks do. The
    turnaround does not, despite the name - it is an in-place pivot.
*/
const ArcherClipInfo ARCHER_CLIPS[CLIP_COUNT] = {
    //                     loops  travels  turns   move   lift
    { "Idle",                true,  false,  false, false, false },
    { "Idle_LookingAround",  true,  false,  false, false, false },
    //The four gaits: their stride is walked by the RULES, so it comes off the bone. Their Y does
    //not - that is the footfall bob, 0.02 units of it, and pinning it would flatten the bounce.
    { "Walking",             true,  true,   false, true,  false },
    { "Walking2",            true,  true,   false, true,  false },
    { "Running_Slow",        true,  true,   false, true,  false },
    { "Running_Fast",        true,  true,   false, true,  false },
    //THE ONE CLIP THAT IS A TURN. Its hip yaw is the whole point of it, where every cycle above
    //has hip yaw that is just the gait - see f_turns. It pivots on the spot (0.001 units of
    //authored travel), which is what makes extracting its yaw safe.
    { "Running_TurnAround",  false, false,  true,  false, false },
    //The first ten frames are her dropping her fists from a guard to arms down, which the
    //crossfade in from whatever she was doing already covers - and blends better than they do.
    { "Kick_Front",          false, false,  false, false, false, 10.0f / 30.0f },
    //THE CLIMB TRAVELS BUT IS NOT LOCOMOTION - its hip track goes a metre UP as well as forward,
    //because it is a mantle rather than a stride. Measuring its horizontal speed would produce a
    //number that looks like a walking pace and means nothing, so it is not marked as travelling -
    //but both axes ARE the character moving, and Stage carries her up and across the ledge at the
    //same time, so both come off the bone or she is moved twice.
    //Its first 31 frames are a run-up to the wall and a jump for the lip, which the game does from
    //a hang; the hands land on the lip at 1.033s, where the move starts here.
    { "Climb",               false, false,  false, true,  true,  31.0f / 30.0f },
    { "Crouch",              false, false,  false, false, false },
    { "Stretching",          true,  false,  false, false, false },
    { "WarmUp",              true,  false,  false, false, false },
    { "Dance",               true,  false,  false, false, false },
    /*
        A pirouette - and NOT a turn, which is the opposite of what it looks like.

        It is the one clip in the file that both TRAVELS and SPINS, and those cannot both be
        honoured while the yaw is extracted and the translation is not: the character's rotation is
        applied ABOVE the root bone, so R(yaw) * T(p) = T(R(yaw)*p) * R(yaw) and the authored offset
        gets SWEPT ROUND instead of translated. Measured with f_turns set, the hips traced a circle
        growing to 1.759 world units of radius - exactly the authored 0.879-unit walk-back times the
        2.02 model scale, rotated. In Blender she turns on the spot and steps slightly back; in the
        game she orbited a point.

        With f_turns off the whole authored transform stays on the bone and the clip plays exactly
        as animated - she still visibly spins, because the twist is on the hips and every other bone
        hangs off them. Nothing here needs her FACING to change, and that is the only thing
        extracting the yaw buys. See the invariant in stage_test.cpp.

        And its translation stays too (f_extract_move off), for the same reason: the rules never
        play this clip, so there is no second copy of the movement to double up with, and the
        0.879-unit step back is part of the performance. It is marked f_travels only so its speed
        is measured and reported with the rest; it is not in PUPPET_LOCOMOTION.
    */
    { "Twirl",               true,  true,   false, false, false },
    /*
        THE AIR SET. EVERY EXTRACTION COLUMN IS OFF, and the vertical one is worth explaining
        because these clips move up and down more than anything else in the table.

        None of that motion is the character flying. She is authored jumping and landing ON THE
        SPOT, and the 3.2 units of real flight belong to Stage, which applies ARCHER_JUMP_SPEED
        against ARCHER_GRAVITY. What the hips do here is compress and extend: the tuck in
        Jump_ToAir, the stand-up in Jump_FromAir, the deep absorb in FallingIdle_ToLanding.
        Extracting the lift would pin all of it to bind height and leave four clips of nothing.

        The one place that reasoning does NOT reach is the head of FallingIdle_ToLanding, which
        genuinely falls 0.52 world units before its feet land - and that is handled by entering the
        clip at its contact frame instead, see Puppet::clip_entry. A flag could not have fixed it:
        the same axis is a fall for 0.3s and then a performance for 0.8s.
    */
    { "Jump_ToAir",          false, false,  false, false, false },
    { "Falling_Idle",        true,  false,  false, false, false },
    { "Jump_FromAir",        false, false,  false, false, false },
    { "FallingIdle_ToLanding",false,false,  false, false, false },
    /*
        THE RUNNING JUMP, and the only air clip whose horizontal comes off the bone.

        2.150 rig units of travel, 4.34 in the world over 0.933s - which is 4.65 world units a
        second, about half the 9.0 the rules move her at. That gap is not a problem the way it
        would be on the ground: her feet are not planted, so it reads as a jump rather than as
        skating, and f_extract_move hands the whole question to Stage anyway.

        The lift stays on the bone with the rest of the air set. Its hips arc 0.408 -> 0.636 ->
        0.412, which is the push and the tuck, not the 3.2 units of real flight.
    */
    { "Running_Jump",        false, false,  false, true,  false },
    //A held pose - hip within 0.003 across the whole 2.367s - so it loops and there is nothing in
    //it to extract. Replaces the idle that MODE_HANG had been standing in with.
    { "Hanging_Braced",      true,  false,  false, false, false },
    /*
        AND THE ROPE, which is a different pose for the same idea: a ledge is braced against with
        the arms bent and the feet on the wall, a rope is hung from with both hands overhead.

        NOTHING IS EXTRACTED, and here that is not the usual reason. Everywhere else the answer is
        "Stage already covers this ground, so hand it over"; on the rope Stage covers no ground at
        all - reactphysics3d owns the body and Stage::TickArcher stands aside. The app reads the
        solver's position straight onto the model every tick, so whatever the clip's hips do is
        sway ON a rope that is already where it is, and pinning it to bind would only flatten the
        performance out of it.
    */
    { "Hanging_Rope",        true,  false,  false, false, false },
    /*
        THE RUN-TO-STOP, and its horizontal comes off the bone for the usual reason - Stage is
        already covering the ground. It has to, and by a wide margin: the clip travels 0.396 rig
        units (0.80 world) coming to rest, where the rules cover 0.267. Leaving it on would slide
        her three times too far and then snap her back.

        Its yaw stays, though its -46.6 degrees is the largest of anything not marked f_turns. That
        is her squaring up as she plants, not a change of facing - the rules never turned her - so
        extracting it would spin the character at the end of every stop.
    */
    { "Running_ToStop",      false, false,  false, true,  false },
    /*
        The spinning kick, which is the clip that used to be called Kick_Front - -347.1 degrees of
        net hip yaw, a genuine pivot, and the export renamed it when a non-spinning kick took the
        old name. Nothing selects it yet. It is the one clip in the file that WANTS f_turns, and it
        can have it safely because it pivots on the spot (0.005 units of travel).
    */
    { "Kick_FrontSpin",      false, false,  true,  false, false },
    { "Kick_Front_2",        false, false,  false, false, false },
    { "Kick_Front_3",        false, false,  false, false, false },
    //Set dressing. It travels and spins 320 degrees, so it keeps both on the bone and plays exactly
    //as animated - the same treatment, and for the same reason, as Twirl.
    { "Walk_ToHandstand",    false, false,  false, false, false },
    //The same jump baked whole, kept for comparison against the four pieces above. Nothing selects
    //it - see the note on CLIP_JUMP_RISE for why the composable set won.
    { "Jumping_InPlace",     false, false,  false, false, false },
    /*
        A SECOND whole travelling jump, slower and longer than Running_Jump: 0.776 rig units over
        2.000s against 2.150 over 0.933s. Nothing selects it - Running_Jump won the running-jump
        slot because its climb fits the game's almost exactly - but it is a floatier arc and is
        worth keeping to compare against.

        Its horizontal comes off the bone for the same reason Running_Jump's does, and is marked
        now rather than when it is wired, because the flag describes the clip and getting it right
        later is how a clip arrives already drifting. f_travels stays off: its speed is a jump's,
        not a gait's, and measuring it would put a meaningless number beside the ladder's.
    */
    { "Jump_Forward",        false, false,  false, true,  false },
    /*
        And the draw, which has only HALF a home.

        It is a WHOLE-BODY clip for something that has to happen while she is also walking, running
        or falling, so playing it as one more state means she stops moving to draw. That is what
        step 2's upper-body mask layer is for.

        Until that exists it is selected in ONE case only - drawing while standing still - because
        there her legs doing a draw is exactly right. See the TEMPORARY note in Puppet::Choose;
        that branch goes away when the mask layer lands rather than growing to cover running.

        Its -29.2 degrees of net hip yaw is her squaring up to aim, which is performance rather
        than a turn - so f_turns stays off with the rest.
    */
    { "Standing_DrawArrow",  false, false,  false, false, false },
    { "Stretching2",         true,  false,  false, false, false },
    //Held at full draw, looping for as long as she holds - it starts on the draw's last pose. Its
    //bow sways -1.5..+2.7 degrees and the hand stays within 0.04 of the string.
    { "Standing_AimArrowIdle", true, false, false, false, false },
    //Pulled past full draw - the string goes to 1.40. Preview only for now.
    { "Standing_OverdrawArrow", false, false, false, false, false },
    //The kneel. Nothing extracted: the hips drop and rise on the spot, and that drop IS the move.
    { "Stand_ToKneel",       false, false,  false, false, false },
    { "Kneel_Idle",          true,  false,  false, false, false },
    { "Kneel_ToStand",       false, false,  false, false, false },
    //The level entry. Nothing extracted: the hips rise 0.05 -> 0.48 on the spot (within 0.07 of it
    //across the floor) and end 0.03 under Idle's, so she stands up where the rules have her.
    { "Laying_StandingUp",   false, false,  false, false, false },
    /*
        THE ROPE CLIMB CLIMBS - its hips rise 0.28 rig units a cycle - but NOTHING is extracted,
        and that is not an oversight. Extraction pins an axis to the BIND pose, and the bind hips are
        well below where either rope clip holds them, so switching between Hanging_Rope and this
        would drop her. Instead its playhead is pinned to the distance climbed and the app lowers the
        model by the pose's own measured rise (PuppetChoice::lift) - relative to the clip's FIRST
        frame, which sits within 0.03 of the hang.
    */
    { "Rope_Climbing",       true,  false,  false, false, false },
};

bool Puppet::IsDrawPose(int clip){
    return clip == CLIP_DRAW || clip == CLIP_AIM_IDLE;
}

const int PUPPET_LOCOMOTION[PUPPET_LOCOMOTION_COUNT] = { CLIP_WALK, CLIP_RUN_SLOW, CLIP_RUN_FAST };

const int PUPPET_KICK_CLIP[KICK_KIND_COUNT] = { CLIP_KICK, CLIP_KICK_2, CLIP_KICK_3 };

void DescribeArcher(const Stage& stage, ArcherAnimParams& out){
    out.ground_speed = fabsf(stage.vel.x);
    //Signed along FACING rather than along +X, so "backing up" is a negative number whichever way
    //she happens to be pointing. That sign is what the blend space will run on (step 1), and it is
    //the reason a reverse does not need a turn clip.
    out.speed        = stage.vel.x * stage.facing;
    out.vel_y        = stage.vel.y;
    out.facing       = stage.facing;
    out.f_on_ground  = stage.f_on_ground;
    out.mode         = stage.mode;
    out.aim_deg      = stage.ShotAimDeg();  //the sway included, so the bow sways with the arc
    out.aim_side_deg = stage.AimSwaySideDeg();
    out.draw_power   = stage.DrawPower();
    out.kneel_phase  = (stage.mode == MODE_KNEEL) ? stage.kneel_phase : -1;
    out.rope_climb   = (stage.mode == MODE_ROPE) ? stage.rope_climb : 0;
    out.rope_climbed = (stage.mode == MODE_ROPE) ? stage.rope_climbed : 0.0f;
    out.rope_pump = (stage.mode == MODE_ROPE) ? stage.rope_pump : 0.0f;
    out.f_free_hang = (stage.mode == MODE_HANG && stage.hang_branch >= 0);

    /*
        The action, and its phase.

        Order matters: the kick is checked first because it is the one the rules time to the tick,
        and a kick thrown while hanging off a ledge is not a thing that can happen - MODE_HANG has
        no kick in it. Drawing comes last because it is the one that OVERLAPS everything else, and
        overlapping is precisely the thing an upper-body layer exists to stop being a conflict.
    */
    out.action = ACTION_NONE;
    out.action_phase = 0.0f;
    if (stage.kick_ticks > 0){
        out.action = ACTION_KICK;
        out.kick_kind = stage.kick_kind;
        out.action_phase = (float)stage.kick_ticks / (float)stage.Kick().ticks;
    }else if (stage.mode == MODE_CLIMB){
        out.action = ACTION_CLIMB;
        //climb_ticks counts DOWN, so the phase is its complement.
        out.action_phase = 1.0f - ((float)stage.climb_ticks / (float)LEDGE_CLIMB_TICKS);
    }else if (stage.mode == MODE_HANG){
        out.action = ACTION_HANG;
    }else if (stage.bow_mode == BOW_DRAWING){
        out.action = ACTION_DRAW;
        //Progress through the WHOLE draw, reach and nock included - not DrawPower, which only
        //moves during the pull. 1 is full draw, where the held loop takes over.
        out.action_phase = (float)stage.draw_ticks / (float)BOW_DRAW_TICKS;
    }
    if (out.action_phase < 0.0f){ out.action_phase = 0.0f; }
    if (out.action_phase > 1.0f){ out.action_phase = 1.0f; }
}

float Puppet::TargetYaw(float facing){
    return (facing >= 0.0f) ? PUPPET_YAW_RIGHT : PUPPET_YAW_LEFT;
}

float Puppet::WorldClipSpeed(int clip) const{
    if (clip < 0 || clip >= CLIP_COUNT){
        return 0.0f;
    }
    return clip_speed[clip] * model_scale;
}

float Puppet::WorldClipStride(int clip) const{
    if (clip < 0 || clip >= CLIP_COUNT){
        return 0.0f;
    }
    return WorldClipSpeed(clip) * clip_duration[clip];
}

/*
    What a blend of two cycles covers per second.

    NOT the interpolation of their speeds, which is the obvious thing to write and is wrong. The
    pair share one normalised playhead and the group advances at the interpolated DURATION, so in
    one group cycle the character covers the interpolated STRIDE over that interpolated duration.
    Those two answers agree only when both clips happen to have the same cycle length.

    Concretely, on this export: the walk is 1.00s covering 1.58 world units, the slow run 0.767s
    covering 2.24. Halfway between them the naive answer is (1.58 + 2.92) / 2 = 2.25 units/s; the
    real one is a 1.91-unit stride over a 0.884s cycle = 2.16. Only 4% apart here, but it is 4% of
    foot slide introduced by the very thing meant to remove it, and it grows with how different the
    two cycle lengths are.
*/
float Puppet::BlendedSpeed(int clip, int blend_clip, float blend) const{
    if (blend_clip < 0 || blend_clip >= CLIP_COUNT || blend_clip == clip){
        return WorldClipSpeed(clip);
    }
    float stride = WorldClipStride(clip)
                 + (WorldClipStride(blend_clip) - WorldClipStride(clip)) * blend;
    float duration = clip_duration[clip]
                   + (clip_duration[blend_clip] - clip_duration[clip]) * blend;
    if (duration < 0.0001f){
        return 0.0f;
    }
    return stride / duration;
}

/*
    What Running_ToStop is actually allowed to play at. Its own function because UpdateAir needs
    the same number to work out how long to hold the clip for, and the two drifting apart would
    leave the stop held for the wrong number of ticks.
*/
float Puppet::StopRate() const{
    if (stop_plant <= 0.01f || PUPPET_STOP_TIME <= 0.0f){
        return 1.0f;
    }
    float rate = stop_plant / PUPPET_STOP_TIME;
    return (rate > PUPPET_ACTION_RATE_MAX) ? PUPPET_ACTION_RATE_MAX : rate;
}

PuppetChoice Puppet::Choose(const ArcherAnimParams& in) const{
    PuppetChoice out;

    /*
        WHAT IS AUTHORED TODAY, and what is standing in for what is not.

        Everything the archer can DO now has a clip of its own: standing, walking, two runs, the
        kick, the climb, the four-piece air set, the running jump, the run-to-stop, the ledge hang
        and the rope. Drawing the bow is the one thing still half-homed, and for a reason rather
        than for want of an export - Standing_DrawArrow is a whole-body clip for something that has
        to happen WHILE she runs, so it waits on step 2's mask layer. It IS selected while she
        stands still, where whole-body is the correct answer; see the TEMPORARY note in the idle
        branch below. f_placeholder is not a failure mode, it is that list, and the panel shows it
        live.
    */
    if (in.action == ACTION_KICK){
        int kind = (in.kick_kind >= 0 && in.kick_kind < KICK_KIND_COUNT) ? in.kick_kind : KICK_FRONT;
        out.clip = PUPPET_KICK_CLIP[kind];
        /*
            THE ONE PLACE WHERE THE CLIP WON. Everywhere else in this file the rules set a window
            and the clip is stretched to fill it; the kick could not be, because a kick with no
            wind-up does not read as a kick, and 14 ticks against a 1.6-second clip needed 7.1x.
            So KICK_TICKS moved to the clip instead, and the fit below now comes out at 1.00.

            THE ARITHMETIC STAYS ANYWAY, and it is not dead code: it is what makes the mismatch
            VISIBLE when the clip is re-exported at a different length. wanted_rate is reported
            live, the app warns with the number to type, and until that number is typed this is
            what keeps the move the length the rules think it is.
        */
        float window = (float)KICK_SPECS[kind].ticks * ARCHER_DT;
        if (clip_duration[out.clip] > 0.01f && window > 0.0f){
            out.wanted_rate = clip_duration[out.clip] / window;
            out.rate = out.wanted_rate;
            if (out.rate > PUPPET_ACTION_RATE_MAX){ out.rate = PUPPET_ACTION_RATE_MAX; }
        }
        return out;
    }

    /*
        The climb: PINNED to the rules' progress, not played at a rate. Stage carries the body
        along the clip's own hip path (LEDGE_CLIMB_UP/ACROSS), so the hands stay on the lip only if
        the pose is at the same point of the move as the body - which a rate cannot promise past a
        crossfade, and a pin does to the tick. So any LEDGE_CLIMB_TICKS plays it through once.

        ONE TICK AHEAD, because the pose on screen is the one pinned the tick before (the engine
        poses the rig before the tick runs - see climb_lift_posed) while the body is placed this
        tick. climb_ticks falls by exactly one a tick, so next tick's phase is known.
    */
    if (in.mode == MODE_CLIMB){
        out.clip = CLIP_CLIMB;
        float window = (float)LEDGE_CLIMB_TICKS * ARCHER_DT;
        float dur = clip_duration[CLIP_CLIMB];
        if (dur > 0.01f && window > 0.0f){
            float shown = in.action_phase + 1.0f / (float)LEDGE_CLIMB_TICKS;
            if (shown > 1.0f){ shown = 1.0f; }
            //Short of the very end, where a clip that does not loop counts as over.
            out.pinned_time = shown * dur;
            if (out.pinned_time > dur - 0.001f){ out.pinned_time = dur - 0.001f; }
            out.rate = 0.0f;
            //Not applied - the playhead is set, not advanced - but it is the number to argue over.
            out.wanted_rate = dur / window;
        }
        return out;
    }

    /*
        HANGING, both kinds. They are two clips rather than one because they are two grips: a ledge
        is braced against with bent arms and the feet on the wall, a rope is hung from with both
        hands overhead and the feet loose.

        WHICH WAY UP SHE HANGS IS NOT DECIDED HERE, and could not be. On the rope she swings, so
        her own tilt comes off the body the solver is swinging - a view quantity the rules never
        see and never should. See the roll in ApplicationArcher::SyncArcherAnimation.
    */
    //A branch is the third: hung from like the rope, both hands over the top, feet loose.
    if (in.mode == MODE_HANG){
        out.clip = in.f_free_hang ? CLIP_ROPE : CLIP_HANG;
        return out;
    }
    if (in.mode == MODE_ROPE){
        float dur = clip_duration[CLIP_ROPE_CLIMB];
        if (f_rope_climbing && f_climb_playhead && dur > 0.0f){
            float cycle = floorf(climb_playhead / dur);
            out.clip = CLIP_ROPE_CLIMB;
            out.pinned_time = climb_playhead - cycle * dur;
            out.lift_base = cycle * climb_cycle_rise;
            out.lift = in.rope_climbed - out.lift_base;
            out.rate = 0.0f;
            //What the pin amounts to as a playback rate, for the panel: the rules' climb speed over
            //the clip's own. Not applied - the playhead is set, not advanced.
            float native = (clip_duration[CLIP_ROPE_CLIMB] > 0.0f) ?
                           climb_cycle_rise / clip_duration[CLIP_ROPE_CLIMB] : 0.0f;
            out.wanted_rate = (native > 0.0f && in.rope_climb != 0) ? ROPE_CLIMB_SPEED / native : 0.0f;
            return out;
        }
        out.clip = CLIP_ROPE;
        return out;
    }

    /*
        GETTING UP, the level entry: the whole clip, from its first frame, fitted to GETUP_TICKS -
        which IS its length, so this reads 1.00 until a re-export changes it and the app says so.
        Unlike the kneel's transitions there is no settle to stop at: it is meant to play out.
    */
    if (in.mode == MODE_GETUP){
        out.clip = CLIP_LAYING_UP;
        out.start_time = 0.0f;
        float window = (float)GETUP_TICKS * ARCHER_DT;
        if (clip_duration[CLIP_LAYING_UP] > 0.01f && window > 0.0f){
            out.wanted_rate = clip_duration[CLIP_LAYING_UP] / window;
            out.rate = out.wanted_rate;
            if (out.rate > PUPPET_ACTION_RATE_MAX){ out.rate = PUPPET_ACTION_RATE_MAX; }
        }
        return out;
    }

    /*
        KNEELING: the legs only - the arms are the upper layer's for the whole kneel, since all
        three clips hold a rifle. The two transitions are FITTED to the rules' windows like the
        kick - but only the part of each clip where she actually moves (clip_settle), which is
        what the windows were measured from, so the fit reads 1.00 until a re-export moves it.
        Each starts from its first frame.
    */
    if (in.mode == MODE_KNEEL){
        if (in.kneel_phase == KNEEL_HELD){
            out.clip = CLIP_KNEEL_IDLE;
            return out;
        }
        bool f_down = (in.kneel_phase != KNEEL_RISING);
        out.clip = f_down ? CLIP_KNEEL_DOWN : CLIP_KNEEL_UP;
        out.start_time = 0.0f;
        float window = (float)(f_down ? KNEEL_DOWN_TICKS : KNEEL_UP_TICKS) * ARCHER_DT;
        float used = (clip_settle[out.clip] > 0.01f) ? clip_settle[out.clip] : clip_duration[out.clip];
        if (used > 0.01f && window > 0.0f){
            out.wanted_rate = used / window;
            out.rate = out.wanted_rate;
            if (out.rate > PUPPET_ACTION_RATE_MAX){ out.rate = PUPPET_ACTION_RATE_MAX; }
        }
        return out;
    }

    /*
        AIRBORNE: two clips and one number decides between them, vel_y.

        Rising plays Jumping_Up from its measured launch, fitted to the game's rise. Falling loops
        Falling_Idle, which is a held float pose and has nothing to fit. There is no apex clip and
        no landing clip yet, so the fall simply holds until the ground arrives - see the air-set
        table in animation_plan.md for what that costs and what is still missing.

        THE RISE IS FITTED, THE FALL IS NOT, and that asymmetry is real rather than an omission:
        a rise always takes v/g seconds and a fall takes as long as the drop is tall.
    */
    /*
        AIRBORNE: two clips, and vel_y alone decides between them.

        NEITHER IS FITTED TO A WINDOW, which is the difference between this and the kick, the climb
        or the earlier one-clip jump. Jump_ToAir is a TRANSITION - standing into the airborne pose,
        0.267s of it - so it is played at its own speed and then holds the pose it arrives at. The
        rise it covers lasts 0.390s, so the clip finishes about two thirds of the way up and holds;
        stretching it to fill the rise would just be a slower tuck. And because the pose it holds
        is the pose Falling_Idle loops, the handover at the apex is between two frames that already
        match, so the crossfade there has almost nothing to blend.

        A cut jump rises for as little as 0.18s, so the clip is often not finished when the apex
        arrives. That is fine and is the right way round: the readable part of a tuck is the start.
    */
    if (!in.f_on_ground){
        /*
            A RUNNING JUMP IS ONE WHOLE ARC, so it neither splits on vel_y nor needs the fall loop.
            It was latched at takeoff (see air_clip) and simply runs; if she is still airborne when
            it ends, a non-looping clip holds its last frame, which is already a descending
            pre-landing pose and a better thing to hold than a static float.

            Fitted to the RISE only - its climb is 0.367s against the game's 0.390s, so 0.94x. Its
            descent is 0.566s against the game's 0.327s and no single rate can serve both; the rise
            is the half with the push in it and the half whose length the rules guarantee. At 0.94x
            a full jump lands about two thirds of the way through the clip, which is mid-descent.
        */
        if (air_clip == CLIP_RUN_JUMP){
            out.clip = CLIP_RUN_JUMP;
            out.start_time = 0.0f;      //every flight starts at the takeoff frame
            if (run_jump_rise > 0.01f && PUPPET_RISE_TIME > 0.0f){
                out.wanted_rate = run_jump_rise / PUPPET_RISE_TIME;
                out.rate = out.wanted_rate;
                if (out.rate > PUPPET_ACTION_RATE_MAX){ out.rate = PUPPET_ACTION_RATE_MAX; }
            }
            return out;
        }
        /*
            THE LANDING, BEFORE IT HAPPENS. Once the forecast touchdown is nearer than the landing
            clip's contact frame, the clip plays its lead-in - the legs reaching for the floor -
            with the playhead PINNED so the contact frame falls on the contact tick: contact frame
            less the time still to fall. Pinned rather than started and left to run, so a forecast
            that moves moves the playhead with it, and one that goes (she steered off the edge)
            simply drops back to the fall below. One tick ahead, because the pose on screen is the
            one pinned the tick before - see the ledge climb.

            The standing set only. A running jump is one authored arc whose last stretch already
            reaches for the ground, and cutting it for Jump_FromAir's lead-in would throw that away.
        */
        int lead = LeadInClip(in);
        if (lead >= 0){
            float entry = clip_entry[lead];
            float t = entry - (float)(in.land_in_ticks - 1) * ARCHER_DT;
            if (t < 0.0f){ t = 0.0f; }
            out.clip = lead;
            out.pinned_time = t;
            out.start_time = t;
            out.rate = 0.0f;
            return out;
        }
        out.clip = (in.vel_y > PUPPET_RISE_VEL) ? CLIP_JUMP_RISE : CLIP_FALL;
        return out;
    }

    /*
        LANDING: an event rather than a state, so UpdateAir owns it and this only reports it.

        It beats the locomotion ladder below but loses to the air above, which is the order that
        survives a landing on the same tick as walking off the next ledge.
    */
    if (settle_ticks > 0 && settle_clip >= 0){
        out.clip = settle_clip;
        out.start_time = clip_entry[settle_clip];
        if (settle_clip == CLIP_STOP){
            /*
                The stop is the one settle that is FITTED, because unlike a landing it has to keep
                pace with something the rules are still doing. Its plant wants to land on the tick
                she actually comes to rest, and PUPPET_STOP_TIME says that is 0.075s away.

                It does not fit, and by a mile. The gap is reported rather than hidden - the same
                arrangement as the kick and the climb, and the same conversation: either the clip
                is trimmed to its plant or ARCHER_RUN_FRICTION comes down.
            */
            out.wanted_rate = (stop_plant > 0.01f) ? (stop_plant / PUPPET_STOP_TIME) : 1.0f;
            out.rate = StopRate();
        }
        return out;
    }

    if (in.ground_speed < PUPPET_IDLE_SPEED){
        /*
            DRAWING WHILE STANDING STILL: the draw's own LEGS on the base.

            Since the upper-body layer (animation_plan.md, Step 2) the draw is carried by the
            layer in every stance, and this branch only decides what her LEGS do while she stands
            and draws: the draw clip's own, so the standing draw looks exactly as authored - hips
            squaring up included - with the layer sampling the same clip over it. Once she moves,
            the ladder below takes her legs and the layer carries on. This used to be marked
            TEMPORARY, to be deleted when the layer landed; it stayed, for that reason.

            FITTED TO THE RULES' WINDOW, like the kick and the climb above it. The clip runs
            1.067s and BOW_DRAW_TICKS gives the draw 36 ticks - 0.600s - so it wants 1.78x, which
            is inside PUPPET_ACTION_RATE_MAX. It is a one-shot, so on reaching the end it holds
            its last frame, which is the full-draw pose - exactly what a held draw should look
            like. The same number drives the bow's bend in ApplicationArcher::SyncBow, so the pose
            and the bend reach full together.
        */
        /*
            AT FULL DRAW, THE HELD LOOP. Standing_AimArrowIdle begins on exactly the pose the draw
            ends on - bow angle, hand on the string and grip agree to three decimals - so the hand
            over needs no blend (see the zero blend time set for it in BuildArcherModel), and it
            loops for as long as she holds. Played at 1.0: it is a breath, not a timed action.
        */
        if (in.action == ACTION_DRAW && in.action_phase >= 1.0f){
            out.clip = CLIP_AIM_IDLE;
            return out;
        }
        if (in.action == ACTION_DRAW){
            out.clip = CLIP_DRAW;
            //Every draw starts from the first frame - the same as the running jump. Without it, a
            //redraw inside the crossfade out of the last one rewinds into THAT draw and resumes
            //it at full.
            out.start_time = 0.0f;
            float window = (float)BOW_DRAW_TICKS * ARCHER_DT;
            if (clip_duration[CLIP_DRAW] > 0.01f && window > 0.0f){
                out.wanted_rate = clip_duration[CLIP_DRAW] / window;
                out.rate = out.wanted_rate;
                if (out.rate > PUPPET_ACTION_RATE_MAX){ out.rate = PUPPET_ACTION_RATE_MAX; }
            }
            return out;
        }
        out.clip = CLIP_IDLE;
        return out;
    }

    /*
        Moving: the clip from the ladder that needs the LEAST stretching, then stretched.

        Picked on the ratio rather than on the difference - "which clip's stride is nearest this
        speed in proportion" rather than "in units per second" - because a stretch is multiplicative.
        At 3 units a second the walk would need 1.9x and the slow run 1.03x; on a difference the
        walk would look nearer, and it is not.

        THIS IS THE DISCRETE PRECURSOR TO THE BLEND SPACE, and choosing it this way is what makes
        the blend space a small change later: step 1 keeps this search and, instead of taking the
        best clip, takes the two either side of the speed and mixes them. Same measurements, same
        function, one more clip in the answer.

        wanted_rate is what planting the feet would take; rate is what is allowed. Whatever is left
        between them is foot slide, and it is reported rather than hidden.
    */
    /*
        THE BLEND SPACE. Find the two rungs of the ladder that BRACKET this speed and mix them by
        where the speed falls between their strides; outside the ladder, take the end rung alone
        and stretch it.

        This is what replaces a transition with a parameter. Between a walk and a slow run there is
        no longer an event to interrupt or to be caught halfway through - both clips are playing,
        and the weight is just a number that moves. The two clips are also brought into step by
        clip_phase, so the blend mixes a left-foot-down pose with a left-foot-down pose instead of
        with whatever the other clip happened to be doing.

        IDLE IS NOT A RUNG, deliberately. A shared normalised phase makes both clips complete a
        cycle together, which is exactly right for two gaits and nonsense for a 12-second ambient
        idle against a 1-second walk - the idle would play twelve times too fast. Standing up and
        moving off stays an ordinary crossfade, which is a rare, slow, forgiving transition; the
        skating this is here to fix happens between two CYCLES.
    */
    int lower = -1;
    int upper = -1;
    for (int i = 0; i < PUPPET_LOCOMOTION_COUNT; i++){
        int clip = PUPPET_LOCOMOTION[i];
        float native = WorldClipSpeed(clip);
        if (native <= 0.01f){
            continue;       //not measured, or not in the export - it cannot be judged
        }
        if (native <= in.ground_speed){
            if (lower < 0 || native > WorldClipSpeed(lower)){ lower = clip; }
        }
        if (native >= in.ground_speed){
            if (upper < 0 || native < WorldClipSpeed(upper)){ upper = clip; }
        }
    }
    if (lower < 0 && upper < 0){
        out.clip = CLIP_WALK;       //nothing measured at all; fall back rather than divide by it
        return out;
    }
    if (lower < 0){
        out.clip = upper;           //slower than the slowest gait: that gait, played slower
    }else if (upper < 0 || upper == lower){
        out.clip = lower;           //faster than the fastest: that gait, played faster
    }else{
        /*
            Between two rungs. Weighted on STRIDE SPEED, which is the quantity the feet care
            about - a speed halfway between a walk and a run should look half walk and half run.
        */
        float low_speed = WorldClipSpeed(lower);
        float high_speed = WorldClipSpeed(upper);
        float span = high_speed - low_speed;
        out.clip = lower;
        out.blend_clip = upper;
        out.blend = (span > 0.0001f) ? ((in.ground_speed - low_speed) / span) : 0.0f;
        if (out.blend < 0.0f){ out.blend = 0.0f; }
        if (out.blend > 1.0f){ out.blend = 1.0f; }
        //Bring the follower's footfalls onto the leader's. The difference between the two clips'
        //measured plant phases IS the correction; nothing has to be re-authored to line them up.
        out.blend_phase_offset = clip_phase[upper] - clip_phase[lower];
    }

    /*
        And the rate, matched against what the BLEND actually covers.

        Not the interpolation of the two clips' speeds: the pair share one normalised playhead and
        advance at the interpolated DURATION, so what they cover per second is the interpolated
        stride over the interpolated duration. Those differ whenever the two clips have different
        cycle lengths, which is always. See BlendedSpeed.
    */
    float native = BlendedSpeed(out.clip,out.blend_clip,out.blend);
    if (f_match_feet && native > 0.01f){
        out.wanted_rate = in.ground_speed / native;
        out.rate = out.wanted_rate;
        if (out.rate < PUPPET_RATE_MIN){ out.rate = PUPPET_RATE_MIN; }
        if (out.rate > PUPPET_RATE_MAX){ out.rate = PUPPET_RATE_MAX; }
    }
    /*
        Backing up plays the walk BACKWARDS rather than turning around.

        Object::SetAnimationRate takes a signed rate and nothing rewinds, so this costs one minus
        sign and no second clip. It is also the honest reading of the input: holding left while
        facing right is a request to move left, not a request to turn - the turn is a separate
        decision the rules make, and while they have not made it she should not be walking on the
        spot.
    */
    if (in.speed < 0.0f){
        out.rate = -out.rate;
    }
    return out;
}

/*
    Both ends of a flight. See the note on the declaration for why this is the Puppet's only piece
    of remembered state.
*/
void Puppet::UpdateAir(const ArcherAnimParams& in){
    /*
        TAKEOFF: choose the air set once, from the speed she LEFT THE GROUND at.

        Latched rather than derived, because ARCHER_AIR_FRICTION would otherwise decide it again
        halfway through the arc - see air_clip. Fires for walking off a ledge as well as for
        jumping, because the animation cannot tell those apart and should not try: what it needs to
        know is how fast she is travelling, not why.
    */
    if (!in.f_on_ground && f_was_on_ground){
        air_clip = (in.ground_speed >= PUPPET_RUN_JUMP_SPEED) ? CLIP_RUN_JUMP : -1;
    }
    if (in.f_on_ground){
        air_clip = -1;
    }
    //Which landing's lead-in is playing, remembered for the contact below. After air_clip, which
    //it reads.
    if (!in.f_on_ground){
        lead_clip = LeadInClip(in);
    }
    //The fall pose, eased toward where the fall speed puts it - see fall_weight.
    {
        float target = FallPoseTarget(in);
        float step = 1.0f / (float)PUPPET_FALL_BLEND_TICKS;
        if (fall_weight < target){ fall_weight = (fall_weight + step > target) ? target : fall_weight + step; }
        else if (fall_weight > target){ fall_weight = (fall_weight - step < target) ? target : fall_weight - step; }
    }

    //A kneel pressed at a run brakes her exactly like letting go, which would otherwise read as a
    //run-to-stop and play it the moment she stood back up. A restart mid-run does the same thing
    //to the get-up.
    if (in.mode == MODE_KNEEL || in.mode == MODE_GETUP){
        settle_ticks = 0;
    }
    if (settle_ticks > 0){
        settle_ticks--;
        //Taken back by the player. The rules never stopped her moving, so neither does this.
        if (in.ground_speed >= PUPPET_IDLE_SPEED || !in.f_on_ground){
            settle_ticks = 0;
        }
    }

    /*
        THE RUN-TO-STOP, fired on the FIRST tick of the deceleration rather than at the end of it.

        Waiting until she is at rest would start a plant-and-settle after the settling was over,
        and would also mean the blend space had already raced Running_Fast -> Running_Slow ->
        Walking -> Idle in the five ticks it takes to stop. Firing on the first tick replaces that
        scramble with one authored clip.

        AND THE SIZE OF THE DROP IS WHAT SAYS IT WAS A STOP AT ALL. Friction can remove at most
        PUPPET_FRICTION_STEP in a tick, so a drop of about that much is her letting go of the key,
        and a bigger one is something being in the way - a wall zeroes the speed outright, a crate
        clamps it to ARCHER_PUSH_SPEED. That is the discriminator, and it is a fact about the rules
        rather than a tuned threshold. The wall's branch is empty because the clip does not exist
        yet; it is one line when it does.
    */
    float drop = last_ground_speed - in.ground_speed;
    if (in.f_on_ground && settle_ticks == 0 && last_ground_speed >= PUPPET_STOP_FROM_SPEED &&
        drop > PUPPET_FRICTION_STEP * 0.5f){
        if (drop < PUPPET_FRICTION_STEP * 1.5f){
            settle_clip = CLIP_STOP;
            settle_ticks = (int)(clip_duration[CLIP_STOP] * ARCHER_TPS / StopRate());
        }else{
            //Stopped by something rather than by letting go. No clip authored for it yet, so the
            //ladder carries on down to the idle exactly as it did before.
            settle_clip = -1;
        }
    }

    if (in.f_on_ground && !f_was_on_ground){
        /*
            THE IMPACT IS LAST TICK'S vel_y, not this one's. Stage resolves the contact and zeroes
            the velocity in the same tick that sets f_on_ground, so by the time a landing is
            visible the number that decides how hard it was has already been thrown away. Reading
            it one tick late is the whole reason last_vel_y exists.
        */
        float impact = -last_vel_y;
        //Already chosen in the air if a lead-in played: that clip is at its contact frame now, and
        //choosing again from a speed read a tick apart could swap it on the tick she touches down.
        settle_clip = (lead_clip >= 0) ? lead_clip : LandingClipFor(impact);
        lead_clip = -1;
        settle_ticks = 0;
        if (settle_clip >= 0 && in.ground_speed < PUPPET_IDLE_SPEED){
            //The clip runs from its contact frame, so the part still to play is what is left after
            //it - counting the whole duration would hold the landing long after it had finished.
            float left = clip_duration[settle_clip] - clip_entry[settle_clip];
            settle_ticks = (int)(left * ARCHER_TPS);
        }
    }

    f_was_on_ground = in.f_on_ground;
    last_vel_y = in.vel_y;
    last_ground_speed = in.ground_speed;
}

int Puppet::LandingClipFor(float speed){
    if (speed >= PUPPET_HARD_LAND_VEL){ return CLIP_LAND_HARD; }
    if (speed >= PUPPET_LAND_VEL){ return CLIP_LAND_SOFT; }
    return -1;
}

int Puppet::LeadInClip(const ArcherAnimParams& in) const{
    if (in.f_on_ground || air_clip == CLIP_RUN_JUMP || in.land_in_ticks <= 0){
        return -1;
    }
    int clip = LandingClipFor(in.land_speed);
    if (clip < 0 || clip_entry[clip] <= 0.0f){
        return -1;      //no landing at all, or one entered at its first frame - nothing to lead in
    }
    //In the window once the time still to fall - less the tick the pose shows late - fits in front
    //of the contact frame.
    if ((float)(in.land_in_ticks - 1) * ARCHER_DT > clip_entry[clip]){
        return -1;
    }
    return clip;
}

float Puppet::FallPoseTarget(const ArcherAnimParams& in) const{
    if (in.f_on_ground || in.mode != MODE_AIR || air_clip == CLIP_RUN_JUMP || in.vel_y >= 0.0f ||
        LeadInClip(in) >= 0 || clip_entry[CLIP_LAND_HARD] <= 0.0f){
        return 0.0f;    //the last: no measured hard landing, so nothing to lay over the fall
    }
    float x = -in.vel_y / PUPPET_FALL_POSE_VEL;
    if (x > 1.0f){ x = 1.0f; }
    return x * x * (3.0f - 2.0f * x);
}

void Puppet::UpdateRope(const ArcherAnimParams& in){
    if (in.mode != MODE_ROPE){
        f_rope_climbing = false;
    }else if (in.rope_climb != 0){
        f_rope_climbing = true;
    }
    float dur = clip_duration[CLIP_ROPE_CLIMB];
    float lift = 0.0f;
    float t = (f_rope_climbing && dur > 0.0f) ? ClimbTimeAt(in.rope_climbed,lift) : -1.0f;
    if (t < 0.0f){
        f_climb_playhead = false;
        return;
    }
    //Whole cycles by floorf, which agrees with ClimbTimeAt's wrap on either side of zero.
    climb_target = floorf(in.rope_climbed / climb_cycle_rise) * dur + t;
    /*
        FOLLOWED, NOT SET. Where the distance's curve is flat the target leaps - a whole pause in
        one tick - and the playhead goes after it at the capped rate instead, catching up on the
        steep part that follows. A lag of more than half a cycle is not a pause being played out
        but a jump in the distance itself, and is taken at once.
    */
    float average = (climb_cycle_rise > 0.0f) ? ROPE_CLIMB_SPEED * dur / climb_cycle_rise : 0.0f;
    float cap = fmaxf(PUPPET_CLIMB_RATE_MAX,1.5f * average) * ARCHER_DT;
    float gap = climb_target - climb_playhead;
    if (!f_climb_playhead || fabsf(gap) > 0.5f * dur){
        climb_playhead = climb_target;
    }else if (gap > cap){
        climb_playhead += cap;
    }else if (gap < -cap){
        climb_playhead -= cap;
    }else{
        climb_playhead = climb_target;
    }
    f_climb_playhead = true;
}

float Puppet::ClimbTimeAt(float climbed, float& lift) const{
    lift = 0.0f;
    size_t n = climb_times.size();
    if (n < 2 || climb_rise.size() != n || climb_cycle_rise <= 1e-5f){
        return -1.0f;
    }
    float d = fmodf(climbed,climb_cycle_rise);
    if (d < 0.0f){
        d += climb_cycle_rise;
    }
    lift = d;
    //The first keyframe the rise reaches d at, then between it and the one before. Monotonic, so
    //a flat stretch - the hips pausing - maps to its first frame and is passed over as she climbs.
    size_t hi = 1;
    while (hi < n - 1 && climb_rise[hi] < d){
        hi++;
    }
    float r0 = climb_rise[hi - 1];
    float r1 = climb_rise[hi];
    float k = (r1 - r0 > 1e-6f) ? (d - r0) / (r1 - r0) : 0.0f;
    if (k < 0.0f){ k = 0.0f; }
    if (k > 1.0f){ k = 1.0f; }
    return climb_times[hi - 1] + (climb_times[hi] - climb_times[hi - 1]) * k;
}

void Puppet::Tick(const ArcherAnimParams& in){
    UpdateAir(in);
    UpdateRope(in);
    choice = Choose(in);
    //The fall pose is eased state, so it goes on here rather than in Choose, which stays pure. Its
    //frame is the hard landing's first: the airborne opening its own lead-in starts from.
    if (fall_weight > 0.0f){
        choice.overlay_clip = CLIP_LAND_HARD;
        choice.overlay_time = 0.0f;
        choice.overlay_weight = fall_weight;
    }

    /*
        The turnaround, as a yaw slew (animation_plan.md section 7, option 1).

        She only ever faces +X or -X, so the two targets are 180 degrees apart and there is no
        shortest-arc question to get wrong - but there IS a choice of which way round, and going
        through zero rather than through 180 turns her TOWARD the camera. That reads as a person
        changing their mind; the other way reads as a person showing you their back.

        A plain move-toward does that on its own, because both targets sit either side of zero.
    */
    float target = TargetYaw(in.facing);
    float step = 180.0f / (float)PUPPET_TURN_TICKS;
    if (yaw_deg < target){
        yaw_deg += step;
        if (yaw_deg > target){ yaw_deg = target; }
    }else if (yaw_deg > target){
        yaw_deg -= step;
        if (yaw_deg < target){ yaw_deg = target; }
    }

    /*
        The upper layer: on while she draws, in any stance, and for the whole of a kneel - eased
        both ways. The clip it plays is latched so that when it goes off the layer fades out of the
        pose she was in, rather than snapping to "no clip" and leaving the weight nothing to fade.
    */
    ChooseUpper(in,choice);
    bool f_drawing = (in.action == ACTION_DRAW);
    if (choice.upper_clip >= 0){
        upper_latched = choice.upper_clip;
    }
    float upper_step = 1.0f / (float)PUPPET_UPPER_BLEND_TICKS;
    if (choice.upper_clip >= 0){
        upper_weight += upper_step;
        if (upper_weight > 1.0f){ upper_weight = 1.0f; }
    }else{
        upper_weight -= upper_step;
        if (upper_weight <= 0.0f){
            upper_weight = 0.0f;
            upper_latched = -1;
        }
        //Fading out: keep showing the hold she let go of, on its own clock.
        choice.upper_clip = upper_latched;
        choice.upper_phase = -1.0f;
    }

    /*
        And the crossfade, when the layer's pose would JUMP with its weight still on - see
        upper_mix. Judged against what was shown last tick, and only while the layer was showing.
    */
    bool f_jump = false;
    if (upper_prev_clip >= 0 && choice.upper_clip >= 0){
        if (choice.upper_clip != upper_prev_clip){
            f_jump = !(upper_prev_clip == CLIP_DRAW && choice.upper_clip == CLIP_AIM_IDLE);
        }else if (choice.upper_phase >= 0.0f && upper_prev_phase >= 0.0f){
            f_jump = (choice.upper_phase < upper_prev_phase - 0.001f);
        }
    }
    if (f_jump){
        upper_from = upper_prev_clip;
        upper_mix = 0.0f;
        upper_xfade_serial++;
    }
    if (upper_mix < 1.0f){
        upper_mix += upper_step;
        if (upper_mix >= 1.0f){
            upper_mix = 1.0f;
            upper_from = -1;
        }
    }
    choice.upper_from_clip = (upper_mix < 1.0f) ? upper_from : -1;
    upper_prev_clip = (upper_weight > 0.0f) ? choice.upper_clip : -1;
    upper_prev_phase = choice.upper_phase;

    /*
        The aim's hold on her body, eased - see aim_weight. From the NOCK, not from the start of the
        draw: the aim neutral is measured live off the posed bow (animation_plan.md, Step 2), which
        is only meaningful once the bow is up with an arrow on it. Before that she brings the bow
        up with the animation's own motion.
    */
    float aim_target = (f_drawing && IsNocked(in)) ? 1.0f : 0.0f;
    float aim_step = 1.0f / (float)PUPPET_AIM_BLEND_TICKS;
    if (aim_weight < aim_target){
        aim_weight += aim_step;
        if (aim_weight > aim_target){ aim_weight = aim_target; }
    }else if (aim_weight > aim_target){
        aim_weight -= aim_step;
        if (aim_weight < aim_target){ aim_weight = aim_target; }
    }

    /*
        The loose legs - see leg_weight. On the rope, EXCEPT while the climb is moving: Rope_Climbing
        grips the rope with the feet, and a leg the clip is placing on the rope is not a leg to let
        swing. Stopped mid-climb she holds its pose, and the legs come loose about it.
    */
    float leg_target = (in.mode == MODE_ROPE && in.rope_climb == 0) ? 1.0f : 0.0f;
    float leg_step = 1.0f / (float)PUPPET_LEG_BLEND_TICKS;
    //Snapped within a step: fifteen steps of 1/15 leave a 1e-7 that never reads as off.
    if (fabsf(leg_weight - leg_target) <= leg_step + 1e-4f){
        leg_weight = leg_target;
    }else if (leg_weight < leg_target){
        leg_weight = fminf(leg_weight + leg_step,leg_target);
    }else{
        leg_weight = fmaxf(leg_weight - leg_step,leg_target);
    }
    leg_lead_deg = (in.mode == MODE_ROPE) ? in.rope_pump * PUPPET_LEG_PUMP_DEG : 0.0f;
    //Once she has climbed, the pose held is the climb's, feet on the rope; before, the hang's.
    leg_gravity = f_rope_climbing ? PUPPET_LEG_GRAVITY_GRIP : PUPPET_LEG_GRAVITY_HANG;
}

void Puppet::ChooseUpper(const ArcherAnimParams& in, PuppetChoice& out){
    out.upper_clip = -1;
    out.upper_phase = -1.0f;
    if (in.action != ACTION_DRAW){
        //Kneeling at rest: the draw's first frame - bow lowered in the left hand, the pose every
        //draw starts from, so drawing from a kneel needs no blend. No clip of its own yet.
        if (in.mode == MODE_KNEEL){
            out.upper_clip = CLIP_DRAW;
            out.upper_phase = 0.0f;
        }
        return;
    }
    if (in.action_phase >= 1.0f){
        out.upper_clip = CLIP_AIM_IDLE;         //the hold, looping on its own clock
    }else{
        out.upper_clip = CLIP_DRAW;
        out.upper_phase = (in.action_phase < 0.0f) ? 0.0f : in.action_phase;
    }
}

bool Puppet::IsNocked(const ArcherAnimParams& in){
    return in.action == ACTION_DRAW &&
           in.action_phase * (float)BOW_DRAW_TICKS >= (float)BOW_NOCK_TICKS - 0.001f;
}

//See the declaration. Field by field, in the members' own order.
void Puppet::HashState(StateHash& h) const{
    h.Begin("puppet");
    h.Add(f_rope_climbing); h.Add(f_climb_playhead); h.Add(climb_playhead); h.Add(climb_target);
    h.Add(f_was_on_ground); h.Add(last_vel_y); h.Add(last_ground_speed);
    h.Add(settle_ticks); h.Add(settle_clip); h.Add(air_clip); h.Add(lead_clip);
    h.Add(fall_weight); h.Add(run_jump_rise); h.Add(stop_plant);
    h.Add(yaw_deg); h.Add(aim_weight); h.Add(upper_weight); h.Add(upper_latched);
    h.Add(leg_weight); h.Add(leg_lead_deg); h.Add(leg_gravity);
    h.Add(upper_mix); h.Add(upper_xfade_serial); h.Add(upper_from);
    h.Add(upper_prev_clip); h.Add(upper_prev_phase);
    const PuppetChoice& c = choice;
    h.Add(c.clip); h.Add(c.blend_clip); h.Add(c.blend); h.Add(c.blend_phase_offset);
    h.Add(c.rate); h.Add(c.wanted_rate); h.Add(c.start_time); h.Add(c.f_placeholder);
    h.Add(c.upper_clip); h.Add(c.upper_phase); h.Add(c.upper_from_clip);
    h.Add(c.pinned_time); h.Add(c.lift); h.Add(c.lift_base);
    h.Add(c.overlay_clip); h.Add(c.overlay_time); h.Add(c.overlay_weight);
}

//See the declaration. Every value is the member's own initialiser in Puppet.h.
void Puppet::Reset(float facing){
    f_rope_climbing = false;
    f_climb_playhead = false;
    climb_playhead = 0.0f;
    climb_target = 0.0f;
    f_was_on_ground = true;
    last_vel_y = 0.0f;
    last_ground_speed = 0.0f;
    settle_ticks = 0;
    settle_clip = -1;
    air_clip = -1;
    lead_clip = -1;
    fall_weight = 0.0f;
    yaw_deg = (facing < 0.0f) ? PUPPET_YAW_LEFT : PUPPET_YAW_RIGHT;
    aim_weight = 0.0f;
    upper_weight = 0.0f;
    upper_latched = -1;
    leg_weight = 0.0f;
    leg_lead_deg = 0.0f;
    leg_gravity = PUPPET_LEG_GRAVITY_HANG;
    upper_mix = 1.0f;
    upper_xfade_serial = 0;
    upper_from = -1;
    upper_prev_clip = -1;
    upper_prev_phase = -1.0f;
    choice = PuppetChoice();
}
