#ifndef _PINBALL_TABLE_H_
#define _PINBALL_TABLE_H_

#include <stdint.h>

/*
    ORBIT OUTPOST - the table, as numbers.

    No engine type appears in this header, and that is the whole point of it. Every coordinate on
    the machine lives here exactly once, and BOTH halves of the app read it: the render objects
    that stage 0 puts on screen, and the primitive colliders stage 1 onwards puts underneath them.
    apps/pinball/pinball_design.md 2.1 is blunt about the failure this avoids - art and collider
    are different objects that merely happen to occupy the same place, so the only thing stopping
    them drifting apart is that they are built from one source. This is that source.

    It also means the layout can be argued about without opening a .cpp.

    --- REVISION 2, 2026-09-13: THE TABLE IS 9.2 LONG, NOT 13.2 --------------------------------
    The first build used the design document's 13.2 x 5.8 deck, which is a real cabinet's OUTER
    dimensions (a 42" playfield is 10.7 units long at this scale; the reference artwork's cabinet
    is squatter still, 1.37:1). On screen it was a long thin box with the middle third of it
    empty except for the ramps. The deck is now 9.2 x 5.8 - the cabinet comes out 10.4 x 6.9,
    almost exactly 3:2 - and the window is 3:2 portrait to match. The play area keeps its real
    5.10 width, so every lane, bat and bumper is still full size; only the spacing between the
    feature rows came down, and it was the spacing that was wrong.

    Shortening it forced a proper look at whether a BALL fits, which the first draft's own
    clearance numbers did not check: they were measured centre-to-centre and ignored every wall's
    thickness. Counted properly the inlane was 0.24 wide against a 0.27 ball, and the orbit's
    left return lane - the ONLY route from the plunger into the play area - was 0.28. So this
    revision also moves things that were not about length:

      - INLANES deliver onto the flipper. The divider used to stop 0.65 outboard and 0.45
        down-table of the pivot, so a ball leaving the inlane passed behind the bat into the
        drain. It now bends inboard and ends a ball's width up-table of the pivot (PIN_DIVIDER_*).
      - SLINGSHOTS are triangles, not a single face whose top sat on the inlane divider and
        sealed the inlane shut. The outer side is what defines the inlane now (PIN_SLING_*).
      - The OUTLANE GUIDE starts at the cabinet wall. It used to start in the open, leaving a
        0.43 strip along the cabinet the ball could wander down for the whole length of the table.
      - The ORBIT RETURN LANE is 0.48 wide (1.5 balls clear) and the UPPER FLIPPER's pivot sits
        under it, so the ball comes off the orbit onto the bat rather than past it.
      - RAMPS climb along the walls and return along the walls, over the return lane on the left
        and over the plunger chute on the right, the way the reference artwork draws them. The
        first draft's habitrails came back down the MIDDLE of the table as opaque 0.46-wide
        slabs and covered most of the deck.
      - NEST GUIDES and the spinner's own posts are gone; the ramps' mouths and the saucers'
        rims bound the bumper nest, and the spinner hangs in the ramp's rails.
      - STANDUPS face the player from just below the pops. On the right wall they were under the
        right ramp; there is no wall on this table that is not under a ramp or an orbit lane.

    All of it was checked with tools/pinball_plan.py, which fattens every wall by the ball's
    radius, floods from the plunger and reports what the ball cannot reach. RUN IT AFTER MOVING
    ANYTHING. It reads the running app, not a copy of these numbers, so it cannot drift.

    --- THE FRAME --------------------------------------------------------------------------------
        +X right, +Y up out of the playfield, +Z toward the player (down-table, where the drain is).
        The playfield surface is the plane y = 0. Up-table is -Z.

    The real cabinet is tilted ~6.5 degrees and THIS ONE IS NOT: the gravity vector is tilted
    instead, which is exactly equivalent because the whole machine - ramps included - would rotate
    together. See PIN_TILT_DEGREES. What it buys is that every wall is an axis-aligned box with no
    rotation to get wrong, and "how high is this ramp" is just y.

    --- THE SCALE --------------------------------------------------------------------------------
        1 world unit = 10 cm. The machine is modelled TEN TIMES life size.

    Not an aesthetic choice. PhysicsWorld's persistentContactDistanceThreshold is 0.03 world units
    (engine/PhysicsWorld.h:129), and a life-size 27 mm ball would have a radius of 0.0135 - HALF
    the solver's own contact tolerance. At 10x the ball's radius is 4.5x that threshold, which is
    the right side of it. Gravity is scaled by the same factor (PIN_GRAVITY below), so a ball still
    crosses the table in the number of seconds a real one would, which is what makes it feel right.
*/

//--- Scale and the constants that follow from it -------------------------------------------------

//1 unit = 10 cm. Here so that anything quoting a real-world dimension can convert honestly rather
//than by a magic 10 in the middle of an expression.
#define PIN_UNITS_PER_METRE     10.0f

//A 27 mm pinball. Everything about clearance on this table is measured in these.
#define PIN_BALL_RADIUS         0.135f
#define PIN_BALL_DIAMETER       (PIN_BALL_RADIUS * 2.0f)

//9.81 m/s^2 at 10x. Applied as a TILTED vector, never by rotating the machine - see the header.
#define PIN_GRAVITY             98.1f
#define PIN_TILT_DEGREES        6.5f

/*
    240 ticks per second, not the engine's 50.

    rp3d 0.10 has no continuous collision detection - grepped for it, there is none - so the only
    thing standing between a pinball and the far side of a wall is how far it moves in one tick.
    At the 80 u/s a hard flipper shot reaches, 50 Hz is 1.6 units of travel per tick, which is six
    ball diameters and would pass clean through the entire flipper assembly between two frames.

    240 Hz brings that to 0.33 units - still 2.4x the ball's own radius, which is why the three
    mitigations in pinball_design.md 2.2 exist and why none of them is optional. 240 is the point
    where those mitigations get cheap rather than the point where the problem goes away.

    NOTE: the Engine panel's "Target Physics TPS" slider is clamped to 200. SetPhysicsTPS itself is
    not, so the app STARTS correctly - but touching that slider silently drops the table to 200 Hz
    and the tunnelling gets worse with nothing on screen saying so.
*/
#define PIN_TPS                 240.0f

//The top speed a ball is allowed to reach. A real pinball never legitimately travels faster than a
//hard flipper shot; an uncapped solver occasionally produces a body that has been squeezed between
//two constraints and is doing 400. Mitigation 3 of pinball_design.md 2.2.
#define PIN_MAX_BALL_SPEED      90.0f

//--- The slab ------------------------------------------------------------------------------------

//The deck, as the ART sees it: the full painted surface including the plunger lane. 5.8 x 9.2.
#define PIN_DECK_MIN_X          (-2.90f)
#define PIN_DECK_MAX_X          ( 2.90f)
#define PIN_DECK_MIN_Z          (-4.60f)
#define PIN_DECK_MAX_Z          ( 4.60f)
#define PIN_DECK_THICKNESS      0.80f       //collider only; the ball never sees the underside

//The play area proper, as the BALL sees it: inside the cabinet walls and left of the chute divider.
#define PIN_PLAY_MIN_X          (-2.85f)
#define PIN_PLAY_MAX_X          ( 2.25f)
//The centre line of the play area, which is the axis the bottom of the table mirrors about. Not
//zero, because the plunger lane eats the right-hand strip.
#define PIN_CENTRE_X            (-0.30f)

//Mirror a left-hand x about the play centre line. Every "mirrored" entry in pinball_design.md 1.5
//is this function rather than a second set of hand-typed numbers that can disagree with the first.
//Note that it takes the left CABINET wall (-2.85) to the chute DIVIDER (+2.25): on the right-hand
//side the divider is the wall, and everything mirrored lands against it correctly.
#define PIN_MIRROR_X(x)         (2.0f * PIN_CENTRE_X - (x))

/*
    The OUTER walls' collision boxes are this thick.

    Mitigation 1 of pinball_design.md 2.2: 0.6 units is twice the worst-case per-tick travel at
    240 Hz, so a ball cannot be on both sides of a wall on consecutive ticks without the solver
    having seen it inside. The cabinet walls genuinely are this thick, and so is anything with dead
    space behind it.

    IT DOES NOT APPLY TO INTERIOR RAILS, and the design document's suggestion that they be "thin to
    look at and fat in collision" does not survive arithmetic: an inlane is 0.40 clear between two
    rails, and fattening each by 0.23 a side closes it. `python tools/pinball_plan.py --collider
    0.6` shows what is left of the table under that rule, which is the plunger lane. Interior
    rails get colliders at their visual thickness; the tunnel guard (mitigation 2, the swept
    raycast) is what protects them, and it is not optional for exactly this reason.
*/
#define PIN_WALL_THICKNESS      0.60f
//...and what a rail looks like. Roughly the ratio between a real table's wire guide and the lane
//it defines. This is ALSO the collider thickness for interior rails - see above.
#define PIN_RAIL_VISUAL_THICK   0.14f
#define PIN_RAIL_HEIGHT         0.44f       //tall enough that a ball cannot ride up over one

//The cabinet walls, outside the play area. Their INNER faces are what the numbers above describe.
//The front wall is lower - the glass slopes down to meet it and the plunger comes through it -
//and has to be, or no camera down-table of the machine could see the drain.
#define PIN_CABINET_HEIGHT      1.30f
#define PIN_CABINET_FRONT_HEIGHT 0.55f

//--- Levels --------------------------------------------------------------------------------------
//pinball_design.md 1.4. Three, plus a backbox that is not playable.
#define PIN_LEVEL_DECK          0.0f
#define PIN_LEVEL_SUBWAY        (-0.70f)    //under-playfield; never seen, entirely about the delay
#define PIN_LEVEL_RAMP_MIN      0.40f
#define PIN_LEVEL_RAMP_MAX      0.90f

//--- Bottom: the flipper end ---------------------------------------------------------------------

//The drain. A trigger, not a hole - the ball is removed and re-served to the plunger.
#define PIN_DRAIN_X             PIN_CENTRE_X
#define PIN_DRAIN_Z             4.50f

/*
    The flippers.

    ANGLE CONVENTION, because it is the one thing here that is easy to get backwards: a flipper's
    angle is a rotation about +Y applied to a bat whose local forward is +X. Rotating +X about +Y
    takes it toward -Z, so a POSITIVE angle points the tip UP-TABLE and a negative one points it
    down-table. Rest is therefore negative and "flipped" is positive, for the left flipper and for
    the mirrored right one alike - the mirror is in the position and the bat's own direction, not
    in the sign of the angle.
*/
#define PIN_FLIPPER_LENGTH      0.80f
#define PIN_FLIPPER_REST_DEG    (-32.0f)
#define PIN_FLIPPER_UP_DEG      ( 32.0f)
#define PIN_FLIPPER_THICKNESS   0.20f       //vertical; a flipper bat is a shallow slab
#define PIN_FLIPPER_WIDTH       0.22f       //at the pivot; the bat tapers toward the tip

/*
    The pivots are 1.70 apart. Two 0.80 bats at -32 degrees then leave 1.70 - 2 * 0.80 * cos(32)
    = 0.343 between their tips, about 1.27 ball diameters, which is the generous end of the normal
    range. (The design document's 1.50 left 0.143 - half a ball - and a machine with no way to
    lose.) 1.20 up-table of the front wall, which is where a pivot sits on a real machine once the
    apron has had its 0.45.

    The flipper LENGTH is left alone deliberately: 0.80 is 2.96 ball diameters, almost exactly a
    real 3-inch bat against a 1-1/16-inch ball, and the design document names it as a feel
    parameter. The pivots are a position, and positions are what stage 0 is for moving.
*/
#define PIN_FLIPPER_L_X         (-1.15f)
#define PIN_FLIPPER_L_Z         ( 3.40f)
#define PIN_FLIPPER_R_X         PIN_MIRROR_X(PIN_FLIPPER_L_X)
#define PIN_FLIPPER_R_Z         PIN_FLIPPER_L_Z

/*
    THE UPPER-LEFT FLIPPER. pinball_design.md 1.5 lists two flippers; the answer to open question
    3 asks for "2 + 1", so this one's numbers are ours.

    It sits AT THE LEFT WALL, in the mouth of the orbit's return lane: the lane is 0.48 wide
    between the cabinet (-2.85) and the return rail (-2.37), so its centre is -2.61, and the pivot
    is put directly under that. A ball coming off the orbit therefore lands on the ROOT of the bat
    and rolls out along it, which is what an upper flipper is for; the first draft had the pivot
    0.35 inboard of the lane and the ball went past the bat's back into the outlane.

    Rest is -38, a little steeper than the main pair's -32, so that the bat's tip at rest sits
    just clear of the left ramp's climb, which passes 0.6 above this corner of the deck; up is +22,
    a 60 degree sweep that carries a ball from the bumpers across to the wormholes and the drop
    bank. Shorter than the main pair because an upper flipper is a placement shot, not a power
    shot. The plan tool notes the tip as LOW under the ramp; that is 1.3 balls of air over a bat
    a ball is never on top of, and it is the arrangement half the real machines with an upper
    flipper have.
*/
#define PIN_FLIPPER_U_X         (-2.60f)
#define PIN_FLIPPER_U_Z         (-1.65f)
#define PIN_FLIPPER_U_LENGTH    0.62f
#define PIN_FLIPPER_U_REST_DEG  (-38.0f)
#define PIN_FLIPPER_U_UP_DEG    ( 22.0f)

/*
    Slingshots, as the TRIANGLE they are on a real machine: a kicking face A->B on the hypotenuse,
    an outer side C->A parallel to the inlane, and a short bottom B->C. Walking A -> B, the kicking
    face is on the left, and for the left slingshot that normal points right and up-table - back
    into play, which is the whole job.

    The OUTER SIDE is what makes an inlane. The first draft had only the face, with its top corner
    sitting on the inlane divider - which sealed the inlane at the top and left a pocket a ball
    could only enter from below. With the outer side at x = -1.25 and the divider at -1.80 the
    inlane is 0.55 centre to centre and 0.40 clear once both thicknesses are taken off: 1.5 balls,
    which is what a real inlane is.

    B, the bottom corner, is 0.35 inboard of the flipper pivot and 0.35 up-table of it, so the
    ball coming off the inlane passes under it onto the bat with 0.57 to spare.
*/
#define PIN_SLING_L_AX          (-1.25f)    //top, on the inlane side
#define PIN_SLING_L_AZ          ( 2.20f)
#define PIN_SLING_L_BX          (-0.80f)    //bottom, over the flipper
#define PIN_SLING_L_BZ          ( 3.05f)
#define PIN_SLING_L_CX          (-1.25f)    //bottom, on the inlane side
#define PIN_SLING_L_CZ          ( 2.95f)
#define PIN_SLING_THICKNESS     0.16f
#define PIN_SLING_HEIGHT        0.34f

/*
    The inlane / outlane divider. Straight down-table from its top to the BEND, then diagonally
    inboard to its END, which is a ball's width up-table of the flipper pivot and just outboard of
    it. That diagonal is the whole fix for the inlane: the ball leaves the lane at x >= -1.245 and
    the bat's root is at -1.15, so it lands ON the flipper. A ball in the OUTLANE passes the other
    side of this end, behind the pivot, and is funnelled to the drain - which is what an outlane is.
*/
#define PIN_DIVIDER_L_X         (-1.80f)
#define PIN_DIVIDER_MIN_Z       ( 1.95f)
#define PIN_DIVIDER_BEND_Z      ( 2.95f)
#define PIN_DIVIDER_END_X       (-1.45f)
#define PIN_DIVIDER_END_Z       ( 3.35f)
/*
    The outlane's OUTER wall, which STARTS AT THE CABINET. From the wall it runs diagonally in to
    its knee and then straight down-table, so there is no strip between it and the cabinet for a
    ball to wander down - the first draft left one 0.43 wide running the length of the table.
    Outlane: 0.55 centre to centre from the divider, 0.41 clear, 1.5 balls.
*/
#define PIN_OUTLANE_L_X         (-2.35f)
#define PIN_OUTLANE_TOP_Z       ( 2.10f)    //where it leaves the cabinet wall
#define PIN_OUTLANE_KNEE_Z      ( 2.55f)
#define PIN_OUTLANE_MAX_Z       ( 3.95f)
//The inlane rollover switch, in the middle of the 0.40 the lane is clear.
#define PIN_INLANE_L_X          (-1.53f)
#define PIN_INLANE_L_Z          ( 2.60f)
//The ball-save kicker in the outlane. Fires an up-table impulse, but ONLY while the save is lit -
//an outlane that always saved would not be an outlane.
#define PIN_SAVE_L_X            (-2.07f)
#define PIN_SAVE_L_Z            ( 3.75f)

/*
    The apron: the panel that covers the outhole, in two plates with the drain mouth between them.

    The mouth is centred on the play centre line and is 0.84 wide, so it comfortably swallows a
    ball coming down the middle while still reading as a mouth rather than as a missing panel. It
    is wider than the 0.343 between the flipper tips on purpose - a ball that grazes a tip on the
    way down has to still go in.
*/
#define PIN_APRON_GAP_MIN_X     (-0.72f)
#define PIN_APRON_GAP_MAX_X     ( 0.12f)
#define PIN_APRON_MIN_Z         ( 4.15f)

//--- Right edge: the launcher --------------------------------------------------------------------

//The lane the plunger fires up. Its left wall is the chute divider; its right wall is the cabinet.
//0.60 centre to centre, 0.51 clear: 1.9 balls, and the right ramp's habitrail comes home over it.
#define PIN_CHUTE_X             ( 2.575f)   //centre line of the lane
#define PIN_CHUTE_DIVIDER_X     ( 2.25f)
#define PIN_CHUTE_DIVIDER_THICK 0.18f
#define PIN_CHUTE_DIVIDER_HEIGHT 0.55f
#define PIN_CHUTE_MIN_Z         (-3.35f)    //where it opens into the top orbit
#define PIN_CHUTE_MAX_Z         ( 4.30f)

#define PIN_PLUNGER_X           PIN_CHUTE_X
#define PIN_PLUNGER_Z           ( 4.30f)
#define PIN_PLUNGER_TRAVEL      0.90f       //along -Z; a slider joint with a spring return

//Three skill-shot rollovers up the lane. Release at the right moment and the top one lights.
#define PIN_SKILL_Z_0           (-1.50f)
#define PIN_SKILL_Z_1           (-2.40f)
#define PIN_SKILL_Z_2           (-3.00f)

//The one-way gate at the mouth of the lane: into the orbit, never back down it.
#define PIN_GATE_X              ( 2.35f)
#define PIN_GATE_Z              (-3.25f)

//--- Top: the orbit and the wormholes ------------------------------------------------------------

/*
    The top orbit, as a circle rather than as three points.

    The horseshoe's ends are at (+-2.30, -3.35) - the right one where the plunger chute opens into
    it, the left one where the return lane begins - and its apex is at z = -4.10, 0.50 short of the
    top wall. That is a chord of 4.60 with a sagitta of 0.75, and the circle through it is
        R = h/2 + c^2/(8h) = 0.375 + 21.16/6.00 = 3.902
    centred at (0, -4.10 + R) = (0, -0.198). Kept as centre-and-radius because that is what a
    sampler wants and because rounding the three points would leave a curve that is not quite a
    circle - which a ball riding it at 60 u/s would find.

    Note the centre is x = 0, not PIN_CENTRE_X: the orbit is symmetric about the CABINET, while the
    bottom of the table is symmetric about the play area. That asymmetry is real and is what makes
    the right-hand orbit exit line up with the plunger lane.

    This arc is the INNER guide of the horseshoe; the outer boundary is the cabinet wall itself, so
    the lane is the gap between them: 0.50 at the apex less half a rail, 0.43 clear - 1.6 balls.
    (0.45 was tried: at 1.4 balls the orbit was the one lane on the table without the margin every
    other lane has.) The rail is continuous, so the top lanes below it are fed from the bumpers,
    not from the orbit; a ball that goes round comes down the left return.
*/
#define PIN_ORBIT_CX            ( 0.0f)
#define PIN_ORBIT_CZ            (-0.198f)
#define PIN_ORBIT_RADIUS        ( 3.902f)
#define PIN_ORBIT_START_DEG     (-126.12f)  //the left-hand end, at (-2.30,-3.35)
#define PIN_ORBIT_END_DEG       ( -53.88f)  //the right-hand end, at (+2.30,-3.35)
#define PIN_ORBIT_RAIL_HEIGHT   0.46f

/*
    The orbit's LEFT RETURN: the lane a ball runs down after the horseshoe, between the cabinet
    and this rail, onto the upper flipper. 0.48 centre to centre, 0.41 clear, 1.5 balls. The first
    draft's was 0.35, which is one ball exactly and, with the rail's end cap, none.

    It stops 0.65 up-table of the upper flipper's pivot so the ball has left the rail before it
    meets the bat, and because the bat's own swing at +42 degrees reaches z = -2.06.
*/
#define PIN_RETURN_X            (-2.37f)
#define PIN_RETURN_MIN_Z        (-3.35f)
#define PIN_RETURN_MAX_Z        (-2.30f)

/*
    The three "FUEL" rollover lanes, under the top of the orbit. Complete the word for a bonus
    multiplier step. Four dividers make three lanes 1.15 apart; the outer two dividers are as far
    out as the horseshoe lets them go - at x = -2.03 the orbit rail is at z = -3.50, and the
    divider tops at -3.45 just clear it. Ball reaches a lane from below; the orbit rail closes the
    top.
*/
#define PIN_FUEL_Z              (-3.25f)
#define PIN_FUEL_X_0            (-1.45f)
#define PIN_FUEL_X_1            (-0.30f)
#define PIN_FUEL_X_2            ( 0.85f)
#define PIN_FUEL_PITCH          1.15f
#define PIN_FUEL_WALL_X_0       (PIN_FUEL_X_0 - PIN_FUEL_PITCH * 0.5f)   //-2.025; the other three step by the pitch
#define PIN_FUEL_WALL_MIN_Z     (-3.45f)
#define PIN_FUEL_WALL_MAX_Z     (-3.05f)

/*
    The wormhole saucers: hole triggers down to the subway, hold a ball in one to lock it. In a row
    0.5 below the FUEL lanes - 0.26 clear between the lane dividers and the rims - and 0.24 above
    the low pop bumper's skirt, which is the reason the bumpers are where they are.
*/
#define PIN_WORM_0_X            (-1.90f)
#define PIN_WORM_0_Z            (-2.55f)
#define PIN_WORM_1_X            (-0.30f)
#define PIN_WORM_1_Z            (-2.55f)
#define PIN_WORM_2_X            ( 1.30f)
#define PIN_WORM_2_Z            (-2.55f)
#define PIN_SAUCER_RADIUS       0.24f

//--- Middle: the scoring cluster -----------------------------------------------------------------

//Pop bumpers. Two radii, and the gap between them is deliberate: the SKIRT is what the player sees
//and what the art has to clear, the COLLIDER is what the ball meets. A collider inside the skirt
//means the ball is already under the cap when it is thrown, which is what a pop bumper looks like.
#define PIN_BUMPER_SKIRT_RADIUS 0.42f
#define PIN_BUMPER_RADIUS       0.30f
//0.52 for the body, with the cap on top reaching 0.63 - about 2.3 ball diameters, where a real
//one sits. Nothing passes over the nest any more, so nothing else constrains it.
#define PIN_BUMPER_HEIGHT       0.52f
/*
    An inverted triangle - two up, one low and central - centred on the play area. The two upper
    ones are 1.50 apart (skirts 0.66 apart, 2.4 balls, so a ball falls between them onto the low
    one rather than sticking) and the ramps' climbs pass either side of the nest with 0.2 to spare.
    With no nest guides, the ramps and the saucer rims are what keep the ball rattling.
*/
#define PIN_BUMPER_0_X          (-1.05f)
#define PIN_BUMPER_0_Z          (-0.90f)
#define PIN_BUMPER_1_X          ( 0.45f)
#define PIN_BUMPER_1_Z          (-0.90f)
#define PIN_BUMPER_2_X          (-0.30f)
#define PIN_BUMPER_2_Z          (-1.65f)

/*
    The MISSION drop target bank, on the right wall - backing onto the chute divider, facing -X,
    hittable off both main flippers and off the upper one. Hit one and it drops below the deck;
    clear all three and the bank resets together.

    It stops 0.35 short of where the chute opens into the orbit, so a ball that fails to make it
    round the horseshoe and comes back down onto the closed gate has somewhere to fall.
*/
#define PIN_DROP_X              ( 2.05f)
#define PIN_DROP_Z_0            (-1.60f)
#define PIN_DROP_Z_1            (-2.20f)
#define PIN_DROP_Z_2            (-2.80f)
#define PIN_DROP_WIDTH          0.40f       //along Z
#define PIN_DROP_HEIGHT         0.30f
#define PIN_DROP_DEPTH          0.12f       //along X

/*
    Standup targets. Static; pure switches. They face the PLAYER (+Z), just below the two upper
    pop bumpers and either side of the nest's centre, so they are shot straight up the middle off
    either flipper. On a wall they would be under a ramp: every wall on this table has a ramp or an
    orbit lane over it.
*/
#define PIN_STANDUP_0_X         (-0.95f)
#define PIN_STANDUP_0_Z         (-0.05f)
#define PIN_STANDUP_1_X         ( 0.35f)
#define PIN_STANDUP_1_Z         (-0.05f)
#define PIN_STANDUP_WIDTH       0.40f       //along X, the face
#define PIN_STANDUP_DEPTH       0.10f
#define PIN_STANDUP_HEIGHT      0.32f

/*
    The spinner: a free hinge - no motor - counted by revolutions rather than by contacts. It
    hangs IN THE MOUTH of the left ramp, from the ramp's own rails, 0.30 down-table of where the
    floor starts to climb. The ramp path begins flat for that reason: a spinner needs something to
    hang from and the rails are already there.
*/
#define PIN_SPINNER_X           PIN_RAMP_L_ENTRY_X
#define PIN_SPINNER_Z           (PIN_RAMP_L_ENTRY_Z - 0.10f)
#define PIN_SPINNER_WIDTH       0.34f
#define PIN_SPINNER_HEIGHT      0.24f

//The gravity-well saucer: an eject hole that STARTS the mission the drop targets selected. In the
//middle of the table, reachable off both flippers, where the reference artwork puts its mission
//display; the standups are 0.31 above it and the ramp mouths 0.9 either side.
#define PIN_WELL_X              PIN_CENTRE_X
#define PIN_WELL_Z              ( 0.55f)

//--- Ramps ---------------------------------------------------------------------------------------

//A ramp's floor is this wide, and its rails stand this proud of it. Both are one ball diameter and
//change, which is the usual rule: narrower and the ball binds, wider and it rattles.
#define PIN_RAMP_WIDTH          0.46f
#define PIN_RAMP_RAIL_HEIGHT    0.30f
//The wire a habitrail is made of, once the ramp is in the air. Two of these, a ball's width apart.
#define PIN_HABITRAIL_RADIUS    0.035f

/*
    Where the ramps start: their mouths, at deck level, the thing a player aims at. Both are
    0.9 either side of the gravity well, level with each other, a whisker up-table of the sling
    tops. The full centrelines are in ApplicationPinball.cpp, where they are sampled into geometry;
    the plan tool checks their headroom over everything they cross.
*/
#define PIN_RAMP_L_ENTRY_X      (-1.60f)
#define PIN_RAMP_L_ENTRY_Z      ( 1.25f)
#define PIN_RAMP_R_ENTRY_X      ( 1.10f)
#define PIN_RAMP_R_ENTRY_Z      ( 1.25f)

//--- Posts ---------------------------------------------------------------------------------------
//The loose posts, placed where a ball needs deflecting. The two beside the ramp mouths stand in
//the strip between a mouth's outer rail and the wall, so a ball running down the side meets a
//rubber rather than the rail's end; the one below the drop bank guards its lowest target's
//corner. 0.36 tall - a real post is about an inch plus its rubber - and short enough that the
//habitrails returning along the walls pass over them with air to spare.
#define PIN_POST_RADIUS         0.075f
#define PIN_RUBBER_RADIUS       0.105f      //what the ball actually meets
#define PIN_POST_HEIGHT         0.36f
#define PIN_POST_MOUTH_L_X      (-2.15f)
#define PIN_POST_MOUTH_L_Z      ( 0.60f)
#define PIN_POST_MOUTH_R_X      ( 1.65f)
#define PIN_POST_MOUTH_R_Z      ( 0.60f)
#define PIN_POST_DROP_X         ( 1.85f)
#define PIN_POST_DROP_Z         (-1.15f)

//--- Backbox -------------------------------------------------------------------------------------
//Not playable. Standing behind the top wall; how tall it is decides how much room the "machine"
//camera shot has to leave above the table.
#define PIN_BACKBOX_Z           (PIN_DECK_MIN_Z - PIN_WALL_THICKNESS - 0.35f)
#define PIN_BACKBOX_HEIGHT      4.10f
#define PIN_BACKBOX_DEPTH       0.55f

//--- Feature descriptors, for anything that wants to walk the table ------------------------------
/*
    A tagged list of everything on the machine, so the debug UI, the MCP layout tool and (from
    stage 2) the switch log can all iterate the SAME table rather than each keeping its own list
    that falls behind. Positions live in the #defines above; this adds identity and kind.

    Deliberately a flat array of PODs and not a class hierarchy. Nothing here needs behaviour -
    the behaviour is in the app - and a flat array is what both a JSON dump and an ImGui table
    want.
*/
enum PinFeatureKind{
    PIN_KIND_DRAIN = 0,
    PIN_KIND_FLIPPER,
    PIN_KIND_SLINGSHOT,
    PIN_KIND_ROLLOVER,      //an inlane, a FUEL lane or a skill-shot lane: a switch you roll over
    PIN_KIND_KICKER,        //ball-save kickers and the subway ejector
    PIN_KIND_PLUNGER,
    PIN_KIND_GATE,
    PIN_KIND_BUMPER,
    PIN_KIND_DROP_TARGET,
    PIN_KIND_STANDUP,
    PIN_KIND_SPINNER,
    PIN_KIND_SAUCER,
    PIN_KIND_POST,
    PIN_KIND_RAMP_ENTRY,
    PIN_KIND_COUNT
};

struct PinFeature{
    const char* name;       //stable id; the MCP tools and the rules both address features by this
    int         kind;
    float       x;
    float       y;          //0 on the deck; the ramps are the only entries that are not
    float       z;
};

//Defined in Table.cpp. Not a header-level array: one definition, one place to add a feature.
extern const PinFeature PIN_FEATURES[];
extern const int PIN_FEATURE_COUNT;

const char* PinFeatureKindName(int kind);

#endif
