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
    //                     loops  travels  turns
    { "Idle",                true,  false,  false },
    { "Idle_LookingAround",  true,  false,  false },
    { "Walking",             true,  true,   false },
    { "Walking2",            true,  true,   false },
    { "Running_Slow",        true,  true,   false },
    { "Running_Fast",        true,  true,   false },
    //THE ONE CLIP THAT IS A TURN. Its hip yaw is the whole point of it, where every cycle above
    //has hip yaw that is just the gait - see f_turns.
    { "Running_TurnAround",  false, false,  true  },
    { "Kick_Front",          false, false,  false },
    //THE CLIMB TRAVELS BUT IS NOT LOCOMOTION - its hip track goes a metre UP as well as forward,
    //because it is a mantle rather than a stride. Measuring its horizontal speed would produce a
    //number that looks like a walking pace and means nothing, so it is not marked as travelling.
    { "Climb",               false, false,  false },
    { "Crouch",              false, false,  false },
    { "Stretching",          true,  false,  false },
    { "WarmUp",              true,  false,  false },
    { "Dance",               true,  false,  false },
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
    */
    { "Twirl",               true,  true,   false },
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

    if (!in.f_on_ground){
        out.clip = CLIP_IDLE;
        out.f_placeholder = true;
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
    out.clip = CLIP_WALK;
    float best_score = 0.0f;
    for (int i = 0; i < PUPPET_LOCOMOTION_COUNT; i++){
        int clip = PUPPET_LOCOMOTION[i];
        float native = WorldClipSpeed(clip);
        if (native <= 0.01f){
            continue;       //not measured, or not in the export - it cannot be judged
        }
        float ratio = in.ground_speed / native;
        //Distance from 1.0 in RATIO space, so being half as fast and twice as fast score the same.
        float score = (ratio >= 1.0f) ? ratio : (1.0f / ratio);
        if (best_score <= 0.0f || score < best_score){
            best_score = score;
            out.clip = clip;
        }
    }
    float native = WorldClipSpeed(out.clip);
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
