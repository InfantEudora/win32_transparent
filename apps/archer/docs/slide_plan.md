# Slide Plan

What she looks like on a slope she is sliding down: the two new clips, `Surfing_Idle` and
`Sliding`, played on the ramps and the leaf instead of the run cycle. Talked through 2026-10-02 and
BUILT the same day, with the user's answers to the open questions: the slope alone picks the clip,
she faces as she always does, she can draw the bow on either, and a slow skid at a slide's foot
plays the surf. See "As built" at the end; the sections below are the plan as agreed.

**Today she has no slide pose.** On a slope past its slip angle (`Stage::SlideAccel` non-zero), the
Puppet sees only her speed and picks the run, so she slides down a ramp running in place. The
only sign of a slide is her turning icy blue (`ar_archer_slide`), the stand-in since the slide
gallery went in (`plant_mechanics_plan.md`, "Sliding"). Nothing in the animation knows about
slopes: she is drawn upright whatever she stands on.

---

## The clips, measured

Read out of archer.glb (2026-10-02), by posing the skeleton from the clips' own keyframes. They
cannot be previewed in the game yet, because a clip has to be a row in `ARCHER_CLIPS` first.

| | Surfing_Idle | Sliding |
|---|---|---|
| length | 1.37 s, loops | 2.03 s, loops |
| root motion | none: hips move under 0.03 | none |
| pose | standing side-on, knees bent, arms out fore and aft | down on one hip, propped on the left hand, legs out ahead |
| hips | 0.71 up at her scale (Idle's are 0.91) | 0.18 above the lowest point, the hand |
| feet | 0.67 apart **along the way she travels**, left foot ahead | out ahead, toes 0.58 to 1.00 in front of the hips |
| chest faces | **-X** in the clip, square to her travel | **+X**, square to her travel, turned up a little |

Her travel axis is the clip's +Z, as for every clip: the run's root motion goes that way.

---

## Two clips, two treatments

**The tilt should follow what she rests on, so the two clips need different treatments.**
- **Sliding** lies on the slope, so all of her should take the slope's angle: hips, legs, torso
  and head, as if the floor she was animated on had been tipped.
- **Surfing** stands on the slope. Her feet should follow it, but a person standing on a slope
  does not lean out square to it. Her **legs take the slope and her torso stays mostly upright**,
  which is how someone rides a board down a face.

Rotating "at least her hips", as you suggested, is the surfing half of this. The legs hang off the
hips, so turning the hips turns both legs with them, rigidly, and the feet end up on a line at
the slope's angle with their soles flat on it. The two refinements are:
- **The pivot is the feet, not the hip joint.** Turned about the hips, the feet swing off the
  surface by the hip height times the angle: 0.30 at 25 degrees. So the body is turned about the
  point between her feet, and that point stays on the surface.
- **The torso turns back** by most of the angle, so she does not lean out square to the slope.
  Down the spine it is shared over Spine, Spine1 and Spine2, as the aim override shares its turn.
  The share kept upright is a tuning value (about 0.7). Some lean into the slope reads as weight.

For `Sliding` it is the same tilt about a different pivot (her seat), and no turn back.

**Why no IK.** On a straight slope a stance tilted rigidly is already correct: both soles flat on
the line, knees as authored. Leg IK earns its place where the ground is not one straight line, such
as walking up a gentle slope, steps, or the terrain's lumps. That is a later, separate piece, not
needed for sliding.

**It fits the model as it is.** `ArcherModel::ApplyAnimation` already poses in stages: base, upper
layer, loose legs, then the aim override, which turns bones about her side axis after the clips.
The slope is one more stage, before the aim:
1. The tilt is a turn of the whole model about her side axis (the play plane's normal side-on),
   about the clip's pivot. It goes on the model's transform, not a bone.
2. The turn back is a turn of the spine bones about the same axis, the aim's way.

Turns about one shared axis add up, and the aim measures the pose it finds before turning
(`aim_pose_deg`). So **a bow drawn on a slope still lands on the rules' angle**, with no change to
the aim.

---

## When each plays

The rules already know when she slides (`SlideAccel() != 0`) and how steep it is
(`SlopeUnderFeetDeg`). The Puppet needs both. These are three new fields in `ArcherAnimParams`,
filled from the rules by `DescribeArcher`, view-only:
- `f_sliding`;
- the slope along her travel, signed: + is nose-down;
- which way she is sliding.

| slope she slides on | clip | why |
|---|---|---|
| slip angle (14) .. 30 | `Surfing_Idle` | a slope you can ride standing |
| over 30 | `Sliding` | too steep to stand on; down onto a hip |
| not sliding | as today | the ladder, by speed |

- **The switch has hysteresis**: down to the hip past 32, back up below 28. A leaf bending under
  her, or a slope at exactly 30, would otherwise flicker between the two.
- **In the slide gallery:** the 18 and 25 hills and the long run (25) are ridden standing; the 35
  and 50 hills go down onto a hip. The drop (25) rides off into the air, and the air set takes over
  as it does today.
- **On the leaf** she rides standing until it bends past 30 near its tip.

**30 is a first guess,** to be tried in the gallery. One alternative is a Down press to drop to the
hip on any slope, but that is input and so a rules change, and the first pass is view-only.

---

## Which way she faces

**Sliding goes feet first, downhill:** the model is turned to the way she is sliding, not to
`facing`. If she slides backwards (facing uphill to brake), she still goes down feet first. Its
chest faces the clip's +X, so sliding right she is seen from behind, and left from the front. A
lying pose seen from behind reads well enough. The alternative is a mirrored export of the clip.

**Surfing always faces the camera.** The stance is side-on, so which way she rides decides only
which foot leads. Turned so her chest is toward the camera whichever way she slides, she rides
with her left foot leading going right, and switch (right foot leading) going left. That is a real
thing riders do, and it keeps her face on screen.

**Drawing the bow while surfing needs one more turn.** The upper-body draw layer is relative to her
hips, and in the surf stance her hips face the camera, so the bow would point at the camera. While
the layer is on over a surfing base, the spine turns back by the stance's 90 degrees about her up
axis (a rider twisting to look down the slope), so the draw faces along her travel. The aim override
then lands it as always. Drawing while down on a hip is a question: see below.

---

## Easing and the ends

- **The tilt eases** in and out over about 6 ticks. The slope under her changes in one tick at
  every ramp's ends (0 to 25 and back), and a pose that snaps to the slope is the pop the eye
  catches first.
- **Run to surf, surf to hip,** and back are the base crossfade (9 ticks), with the tilt easing
  across it.
- **The foot of a slide is the weak end.** She stops dead at the foot of the long run, the open
  point in `plant_mechanics_plan.md`, and from a hip that is a pop to Idle. The skid proposed there (a
  slide's speed bleeding off on the flat) is a rules change that would give the clip time to end;
  for the hip, a short get-up (the second half of `Kneel_ToStand`, or the end of
  `Laying_StandingUp`) on top.

---

## Replays and tests

**Puppet.** The new choice is part of the Puppet's hash, but archer_test never stands on a slope
(it stays in x -6 .. 28, with no ramps and no leaf), so its baselines do not move. make rules gains
checks in TestPuppet:
- on each of the gallery's hills it picks surf or hip as the table says;
- crossing 30 back and forth does not flicker;
- a slope below the slip angle keeps the ladder;
- the yaw follows the slide, not the facing.

**ArcherModel.** The tilt and the turn back are drawing only, checked in the game with the turntable
and the gallery:
- **the feet stay on the slope:** the measured pivot on the surface at every angle in the gallery;
- **the drawn aim still matches the rules:** `aim_drawn_deg` against `aim_deg` while surfing, as the
  aim override is checked today.

---

## Order

1. **The clips as rows.** `ARCHER_CLIPS` and `ArcherClip`, both in place and looping, and both
   previewable on the turntable. At load, measure each one's pivot (the point between the feet,
   and the seat) and its stance yaw, rather than typing them, the MeasureKickClip way, and warn
   when a re-export moves them.
2. **The Puppet's choice.** The slide fields in `ArcherAnimParams`, surf or hip by slope with the
   hysteresis, and the yaw by slide direction. TestPuppet as above. The icy-blue stand-in goes.
3. **The tilt.** The slope stage in `ArcherModel`: the whole model about the clip's pivot, the
   spine's turn back for the surf stance, eased in and out. Judged in the gallery at each angle.
4. **The bow while surfing.** The spine's counter-yaw under the draw layer; checked against the rules'
   aim.
5. **The ends.** Coming out of a hip slide at a slope's foot, with the skid if the rules take it.
6. **Later, separately:** leg IK for walking on slopes and uneven ground.

---

## Open questions

- **Where she goes down onto a hip:** 30 degrees, or a Down press on any slope (rules), or both?
- **Surfing faces the camera both ways** (switch going left), or turns with her facing?
- **Drawing while down on a hip:** allowed (aiming lying back), or her hands are full there (a rules
  change, like the rope)?
- **The skid** at a slide's foot (rules), so the clips can end rather than snap.
- **Sliding seen from behind** going right: fine, or a mirrored export?

---

## As built (2026-10-02)

**Answers taken:**
- **No button:** the slope alone picks the clip.
- **No facing trick:** surfing turns with her facing like everything else, so going left she
  shows the camera her back.
- **The bow on either clip.**
- **A slow skid at a slide's foot, played on the surf.**

**The clips:** `CLIP_SURF` and `CLIP_SLIDE`, the last two rows of ARCHER_CLIPS, so no existing
clip's index moves. Both loop in place at 1x, and both are now previewable with `archer_anim`.

**The choice (Puppet):**
- `ArcherAnimParams` gained `f_sliding`, `f_skidding`, `slope_deg` and `slide_dir`, filled by
  DescribeArcher.
- On the ground and sliding, `Choose` plays the surf, or the hip slide once `f_slide_down` is set.
  It is set at PUPPET_SLIDE_DOWN_DEG (32) and cleared at PUPPET_SLIDE_UP_DEG (28), with the
  hysteresis kept in Tick. The branch sits above the landing's settle and the teeter, so a landing
  onto a slope goes straight into the slide.
- The skid plays the surf.
- On the hip she faces the slide's direction (feet first) **unless drawing**, when she faces her shot.
- The hip state is hashed only while it is on.

**The tilt (app, view-only):**
- On a slide, the whole model is turned about the world's Z by the slope, eased at 5 degrees a
  tick. The pivot is read each tick off the pose on screen in the model's own frame: between the
  feet when surfing, under the hips on the hip slide.
- When surfing, ARCHER_SLOPE_UPRIGHT (0.7) of the tilt is turned back out of Spine, Spine1 and
  Spine2 (`ArcherModel::slope_turn_deg`), between the loose legs and the aim.
- `archer_state.slide` reports `skidding`, `tilt_drawn` and `torso_turn`.
- The icy-blue stand-in now tints only the debug box, and the model only if the export lacks the clips.

**The bow needed nothing new.** The upper layer builds its targets in model space from the draw
clip's own hips (LayerClipModel), so its torso faces along her travel whatever the base's hips
do. The aim override measures the pose it finds, so it lands on the rules' angle on a tilted
body too.
- **Surfing:** within 0.2 degrees once the draw is full.
- **On the hip:** within 0.16 degrees, drawn on the hill's top and carried down it. She sits up
  on the slope to shoot, legs out downhill.

**The skid (rules, `Stage::f_skidding`):** a slide that hands her onto flat ground at
SKID_MIN_SPEED (2) or more starts one.
- With no input it bleeds off at **SKID_DECEL (12 u/s^2)**: about a second and seven units from
  the long run's 12.7, where the run's friction used to stop her in a tenth of a second.
- Pushing against it, at SKID_BRAKE (40).
- Pushing along it never speeds it up. It hands her back to the run at running pace.
- A jump out of it keeps its speed and ends it.
- It ends at SKID_END_SPEED (0.8), on leaving the ground, on a new slide, or kneeling.
- It is hashed only while it is on.

**Checked in the game** (release, rope level, paused and stepped):
- **18 degrees:** surfing, tilted 18, torso turned back 12.6; both feet on the slope line, upright,
  arms out.
- **35 degrees:** the hip slide, lying along the slope feet first, tilt 35. At the foot it eases
  off as she gets up into the surf and skids 1.2 across the flat before the 50 hill, from 5.1 u/s,
  then Idle.
- **25 degrees with the bow:** surfing, drawn within 0.2 of the rules' aim.

**Tests:** make rules **1030 checks, 0 failures**. TestSlides has 14:
- **The skid** on a ramp of its own:
  - it rides out at SKID_DECEL (5.15 against 5.25 by v^2/2a);
  - it brakes harder against input;
  - it runs on at running pace along it;
  - a jump keeps its speed.
- **The gallery's hills:** 8 and 14 hold, 18 and 25 surf, 35 and 50 go down on a hip.
- **The line's hysteresis:** no flicker between 29 and 31.
- **Feet first** on the hip, and turned to the shot when drawing.
- **The skid's surf.**

**archer_test** has the rules parts and every sound **the same**; only `objects` differs from
tick 0. That is the new export's two skinned parts under her armature (`archer_leggings`,
`archer_legs`) and the outfit code that shows them, not anything here: no slope is on the
recording's route. It should be re-baselined with the export, not before.

**Left open:**
- **The gallery's long run** now skids her off its foot (12.7 u/s) across its 5-unit run-out and
  onto the drop. That is physical, and it changes how the gallery plays.
- **Getting up from the hip at a slope's foot** is a 9-tick crossfade into the surf. It reads
  well enough as a scramble up; a get-up clip would be better.
- **Leg IK for walking slopes** is still later.

---

## Status

| # | Piece | State |
|---|---|---|
| 1 | Clip rows + measurements | **built** 2026-10-02: both rows; pivots read live off the pose rather than measured at load |
| 2 | Puppet choice | **built**: slope with 28/32 hysteresis, feet first on the hip, the surf for the skid |
| 3 | Slope tilt | **built**: whole model about the clip's pivot, torso 0.7 upright surfing, eased |
| 4 | Bow while sliding | **built**: free from the layer's model-space targets; aim lands within 0.2 |
| 5 | Slide ends | **built**: the skid (rules) on the surf; the hip's get-up is the crossfade |
| 6 | Leg IK for slopes | later |
