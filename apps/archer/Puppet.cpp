#include "Puppet.h"

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
    { "Kick_Front",          false, false,  false, false, false },
    //THE CLIMB TRAVELS BUT IS NOT LOCOMOTION - its hip track goes a metre UP as well as forward,
    //because it is a mantle rather than a stride. Measuring its horizontal speed would produce a
    //number that looks like a walking pace and means nothing, so it is not marked as travelling -
    //but both axes ARE the character moving, and Stage carries her up and across the ledge at the
    //same time, so both come off the bone or she is moved twice.
    { "Climb",               false, false,  false, true,  true  },
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
        THE AIR SET, added 2026-09-22. Both answer "no" to every extraction column, and the
        vertical one is the answer worth explaining.

        Jumping_Up's hips rise 0.273 rig units between the crouch and the apex - but that is the
        body COMPRESSING AND EXTENDING, not the character leaving the ground. She is authored
        jumping on the spot and lands where she started; the 3.2 units of actual flight belong to
        Stage, which applies ARCHER_JUMP_SPEED against ARCHER_GRAVITY. Extracting the lift would
        pin the hips at bind height and delete the crouch, the push and the landing absorb - which
        is the entire clip. Same reasoning as a gait's footfall bob, an order of magnitude bigger.

        Falling_Idle is a HELD POSE: its hip moves 0.0005 units across the whole 0.733s. Looping it
        is free and there is nothing in it to extract.
    */
    { "Jumping_Up",          false, false,  false, false, false },
    { "Falling_Idle",        true,  false,  false, false, false },
    /*
        And the draw, which arrived in the same export and has no home yet.

        It is a WHOLE-BODY clip for something that has to happen while she is also walking, running
        or falling, so playing it as one more state would mean she stops moving to draw. That is
        what step 2's upper-body mask layer is for, and until that exists this is previewable and
        nothing selects it. Its -29.2 degrees of net hip yaw is her squaring up to aim, which is
        performance rather than a turn - so f_turns stays off with the rest.
    */
    { "Standing_DrawArrow",  false, false,  false, false, false },
};

const int PUPPET_LOCOMOTION[PUPPET_LOCOMOTION_COUNT] = { CLIP_WALK, CLIP_RUN_SLOW, CLIP_RUN_FAST };

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
    out.aim_deg      = stage.aim_deg;
    out.draw_power   = stage.DrawPower();

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
        out.action_phase = (float)stage.kick_ticks / (float)KICK_TICKS;
    }else if (stage.mode == MODE_CLIMB){
        out.action = ACTION_CLIMB;
        //climb_ticks counts DOWN, so the phase is its complement.
        out.action_phase = 1.0f - ((float)stage.climb_ticks / (float)LEDGE_CLIMB_TICKS);
    }else if (stage.mode == MODE_HANG){
        out.action = ACTION_HANG;
    }else if (stage.bow_mode == BOW_DRAWING){
        out.action = ACTION_DRAW;
        out.action_phase = stage.DrawPower();
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

PuppetChoice Puppet::Choose(const ArcherAnimParams& in) const{
    PuppetChoice out;

    /*
        WHAT IS AUTHORED TODAY, and what is standing in for what is not.

        Standing, walking, two runs, the kick and the climb are all real answers. Being airborne,
        hanging off a ledge, swinging on the rope and drawing the bow are not: they fall through to
        the idle and are marked f_placeholder. That flag is not a failure mode, it is the list the
        next animation pass works from, and the panel shows it live.
    */
    if (in.action == ACTION_KICK){
        out.clip = CLIP_KICK;
        /*
            The kick is the one clip the RULES time, and the two do not agree: KICK_TICKS is 14
            ticks (0.23s) from press to recovered, and Kick_Front runs 1.13s. Fitting it needs
            nearly 5x, which is a blur, so it is clamped and the gap is reported instead. That gap
            is a real decision waiting to be made - either the clip gets trimmed to its impact
            window or KICK_TICKS grows - and it is a decision about how the GAME feels, not a bug.
        */
        float window = (float)KICK_TICKS * ARCHER_DT;
        if (clip_duration[CLIP_KICK] > 0.01f && window > 0.0f){
            out.wanted_rate = clip_duration[CLIP_KICK] / window;
            out.rate = out.wanted_rate;
            if (out.rate > PUPPET_ACTION_RATE_MAX){ out.rate = PUPPET_ACTION_RATE_MAX; }
        }
        return out;
    }

    /*
        The climb has a clip, and the two disagree even more than the kick does: Climb runs 2.8s
        and LEDGE_CLIMB_TICKS gives it 18 ticks - 0.30s. Fitting that needs over 9x. It is fitted
        as far as the clamp allows and the rest is reported, because the real answer is almost
        certainly that a 0.3s mantle was too fast to be a mantle and the RULE should move.
    */
    if (in.mode == MODE_CLIMB){
        out.clip = CLIP_CLIMB;
        float window = (float)LEDGE_CLIMB_TICKS * ARCHER_DT;
        if (clip_duration[CLIP_CLIMB] > 0.01f && window > 0.0f){
            out.wanted_rate = clip_duration[CLIP_CLIMB] / window;
            out.rate = out.wanted_rate;
            if (out.rate > PUPPET_ACTION_RATE_MAX){ out.rate = PUPPET_ACTION_RATE_MAX; }
        }
        return out;
    }

    if (in.mode == MODE_HANG || in.mode == MODE_ROPE){
        out.clip = CLIP_IDLE;
        out.f_placeholder = true;
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
    if (!in.f_on_ground){
        if (in.vel_y > PUPPET_RISE_VEL){
            out.clip = CLIP_JUMP_UP;
            out.start_time = jump_launch;
            /*
                Launch-to-apex against the time the game spends climbing. On this export that is
                0.467s of clip into 0.390s of jump - 1.20x, comfortably inside the clamp, which is
                the first thing in this file that has fitted a one-shot without hitting its limit.
            */
            float span = jump_apex - jump_launch;
            if (span > 0.01f && PUPPET_RISE_TIME > 0.0f){
                out.wanted_rate = span / PUPPET_RISE_TIME;
                out.rate = out.wanted_rate;
                if (out.rate > PUPPET_ACTION_RATE_MAX){ out.rate = PUPPET_ACTION_RATE_MAX; }
            }
        }else{
            out.clip = CLIP_FALL;
        }
        return out;
    }

    if (in.ground_speed < PUPPET_IDLE_SPEED){
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

void Puppet::Tick(const ArcherAnimParams& in){
    choice = Choose(in);

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
}
