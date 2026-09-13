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

    It also means the layout can be argued about without opening a .cpp. Every number below is a
    FIRST DRAFT from pinball_design.md 1.5, meant to be dragged around once it is on screen; the
    ones that are not are marked, and the two that were invented rather than taken from the design
    are marked louder than that.

    --- WHAT CHANGED FROM pinball_design.md 1.5, AND WHY -----------------------------------------
    Ten coordinates. Each is a case of two features wanting the same piece of table, which is
    exactly the class of mistake stage 0 exists to find and exactly why the design document calls
    its own numbers a guess until they are on screen. Every one is argued for where it is defined;
    this is the index.

      1. FLIPPER PIVOTS moved apart, -1.05 -> -1.15 (and its mirror). At 1.50 apart, two 0.80 bats
         at -32 degrees leave a 0.144-unit gap between their tips - HALF a ball. Nothing could ever
         drain. See PIN_FLIPPER_L_X.
      2. SLINGSHOT FACES moved inboard by 0.20. The published face crossed the inlane/outlane
         divider it is supposed to sit beside, leaving an inlane 0.19 wide - a ball is 0.27.
      3. INLANE ROLLOVER -1.55 -> -1.60, to sit in the middle of the lane 2 produces.
      4. BALL-SAVE KICKER -2.50 -> -2.05, which is where the outlane actually is once 5 gives it a
         wall.
      5. OUTLANE OUTER GUIDE added. The divider alone left an outlane 1.05 wide, nearly four balls.
      6. FUEL LANES -5.00 -> -5.15, and
      7. WORMHOLE SAUCERS up-table by ~0.5. As published these two rows overlapped: the centre
         saucer's rim was 0.10 from the middle FUEL lane's wall.
      8. LOWER STANDUP TARGET -0.60 -> -2.55, out from under the right ramp, which crests directly
         over where it was and would have made it unhittable.
      9. GRAVITY-WELL SAUCER (+1.55,+1.20) -> (-0.40,+0.70). Same problem as 8 and worse - a hole
         in the deck under a ramp is a hole no ball can reach. The new spot is the middle of the
         table, which is also where the reference artwork puts its mission display.
     10. MISSION DROP TARGET BANK moved from the left wall to the right, x -2.30 -> +2.05 and
         z -1.20/-0.60/0.00 -> -3.30/-3.90/-4.50. The left ramp climbs along that exact strip of
         wall and roofed the whole bank over with 0.14 of headroom. See PIN_DROP_X.

    The other three dozen numbers are the design document's, unchanged.

    --- HOW 8, 9 AND 10 WERE FOUND, WHICH IS THE REUSABLE PART -----------------------------------
    By eye, twice, and then properly. All three are the same fault - a deck feature underneath a
    ramp - and the first two were spotted by reading coordinates while the third was not, because
    on a 3D table "is this under that" is a question about a path, not about a pair of numbers.

    What found the rest was a script: sample both ramp centrelines densely, and for every feature
    on the deck ask whether its footprint falls inside the 0.46-wide ribbon and, if so, how much
    room is left between the ramp's underside and the top of the feature. It flagged twelve
    things, of which eight were real and are fixed here. It is kept, because the next person to
    nudge a coordinate will need it: tools/pinball_clearance.py. Run it after moving a ramp path,
    a feature or a post.

    One exception it reports and should: the right ramp's loop passes over the SKIRT of the right
    pop bumper with 0.13 to spare. That is the design asking for a ramp that "loops over the bumper
    nest", and a ball has no business rolling over a bumper cap - the test there is that nothing
    intersects, not that a ball fits through.

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

//The deck, as the ART sees it: the full painted surface including the plunger lane.
#define PIN_DECK_MIN_X          (-2.90f)
#define PIN_DECK_MAX_X          ( 2.90f)
#define PIN_DECK_MIN_Z          (-6.60f)
#define PIN_DECK_MAX_Z          ( 6.60f)
#define PIN_DECK_THICKNESS      0.80f       //collider only; the ball never sees the underside

//The play area proper, as the BALL sees it: inside the cabinet walls and left of the chute divider.
#define PIN_PLAY_MIN_X          (-2.85f)
#define PIN_PLAY_MAX_X          ( 2.25f)
//The centre line of the play area, which is the axis the bottom of the table mirrors about. Not
//zero, because the plunger lane eats the right-hand strip.
#define PIN_CENTRE_X            (-0.30f)

//Mirror a left-hand x about the play centre line. Every "mirrored" entry in pinball_design.md 1.5
//is this function rather than a second set of hand-typed numbers that can disagree with the first.
#define PIN_MIRROR_X(x)         (2.0f * PIN_CENTRE_X - (x))

/*
    Every STATIC collision box is at least this thick.

    Mitigation 1 of pinball_design.md 2.2, and the cheapest of the three: 0.6 units is twice the
    worst-case per-tick travel at 240 Hz, so a ball cannot be on both sides of a wall on
    consecutive ticks without the solver having seen it inside. The outer walls genuinely are this
    thick. Interior guide rails are thin TO LOOK AT and fat in collision, with the difference
    hidden behind the art - which is only possible because the render mesh and the collider are
    separate objects to begin with.
*/
#define PIN_WALL_THICKNESS      0.60f
//...and what a rail looks like. A quarter of its collider, which is roughly the ratio between a
//real table's wire guide and the lane it defines.
#define PIN_RAIL_VISUAL_THICK   0.14f
#define PIN_RAIL_HEIGHT         0.44f       //tall enough that a ball cannot ride up over one

//The cabinet walls, outside the play area. Their INNER faces are what the numbers above describe.
#define PIN_CABINET_HEIGHT      1.30f

//--- Levels --------------------------------------------------------------------------------------
//pinball_design.md 1.4. Three, plus a backbox that is not playable.
#define PIN_LEVEL_DECK          0.0f
#define PIN_LEVEL_SUBWAY        (-0.70f)    //under-playfield; never seen, entirely about the delay
#define PIN_LEVEL_RAMP_MIN      0.40f
#define PIN_LEVEL_RAMP_MAX      0.90f

//--- Bottom: the flipper end ---------------------------------------------------------------------

//The drain. A trigger, not a hole - the ball is removed and re-served to the plunger.
#define PIN_DRAIN_X             PIN_CENTRE_X
#define PIN_DRAIN_Z             6.50f

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
    CHANGE 1 of 9: the pivots are 1.70 apart, not the published 1.50.

    The design document puts them at -1.05 and its mirror +0.45. Work out where the tips then are:
    a bat at rest reaches pivot + L*(cos t, -sin t) with t = -32 degrees, so each tip is
    0.80 * cos(32) = 0.6785 inboard of its pivot, and the gap between the two tips is

        1.50 - 2 * 0.6785 = 0.143

    against a ball 0.27 across. The ball cannot fit, so nothing can ever drain down the middle -
    which is not a tuning problem, it is a machine with no way to lose. Widening the pivots to 1.70
    opens it to 0.343, about 1.27 ball diameters, which is the generous end of the normal range and
    the right end to start from.

    The flipper LENGTH is left alone deliberately: 0.80 is 2.96 ball diameters, almost exactly a
    real 3-inch bat against a 1-1/16-inch ball, and the design document names it as a feel
    parameter. The pivots are a position, and positions are what stage 0 is for moving.
*/
#define PIN_FLIPPER_L_X         (-1.15f)
#define PIN_FLIPPER_L_Z         ( 5.35f)
#define PIN_FLIPPER_R_X         PIN_MIRROR_X(PIN_FLIPPER_L_X)
#define PIN_FLIPPER_R_Z         PIN_FLIPPER_L_Z

/*
    THE UPPER-LEFT FLIPPER, AND THE FACT THAT ITS POSITION IS INVENTED.

    pinball_design.md 1.5 lists two flippers; the answer to open question 3 asks for "2 + 1". So
    this one has no coordinate to inherit and the numbers below are mine, chosen to give it shots
    worth making rather than to fill a gap:

      - up-table and right, it covers the FUEL rollover lanes (z = -5.00) and the right-hand
        wormhole saucer at (+1.30,-4.40);
      - flat and right, it covers the two standup targets on the right wall;
      - it is fed by a ball coming down the LEFT side of the top orbit, which is the classic feed
        for an upper flipper and costs no new geometry.

    It sits just up-table of the MISSION drop target bank (x = -2.30, z = -1.20..0.00) so the two
    do not fight for the same wall, and its swept arc clears the left pop bumper's skirt by about
    0.24 units. Shorter than the main pair because there is less room and because an upper flipper
    is a placement shot, not a power shot.

    This is the number most likely to move once the table is on screen. It is deliberately the
    easiest one here to move.
*/
#define PIN_FLIPPER_U_X         (-2.35f)
#define PIN_FLIPPER_U_Z         (-1.75f)
#define PIN_FLIPPER_U_LENGTH    0.62f
#define PIN_FLIPPER_U_REST_DEG  (-20.0f)
#define PIN_FLIPPER_U_UP_DEG    ( 42.0f)

/*
    Slingshots, given as the FACE the ball bounces off rather than as a centre point: it is the
    face's normal that decides which way the kick goes, so two endpoints carry information a
    position cannot. Walking A -> B, the kicking face is on the left, and for the left slingshot
    that normal comes out pointing right and up-table - back into play, which is the whole job.

    CHANGE 2 of 9: both endpoints are 0.20 further inboard than published (-1.95/-1.35).

    The published face passes x = -1.84 at z = 4.30, and the inlane/outlane divider below it is at
    x = -1.80: the slingshot crosses the wall it is supposed to sit beside. Even where it does not
    quite cross, it leaves the inlane 0.19 wide against a 0.27 ball, so the lane the artwork draws
    arrows down is one a ball cannot enter. Shifting the face inboard opens the lane to 0.39, about
    1.4 balls, and puts the slingshot's lower end 0.40 directly above the flipper pivot - which is
    where a slingshot goes.
*/
#define PIN_SLING_L_AX          (-1.75f)
#define PIN_SLING_L_AZ          ( 4.15f)
#define PIN_SLING_L_BX          (-1.15f)
#define PIN_SLING_L_BZ          ( 4.95f)

//Inlane / outlane dividers. The wall that separates the two, running down-table.
#define PIN_DIVIDER_L_X         (-1.80f)
#define PIN_DIVIDER_MIN_Z       ( 3.60f)
#define PIN_DIVIDER_MAX_Z       ( 5.80f)
/*
    CHANGE 5 of 9: the outlane's OUTER wall, which the design document does not have.

    Without it the outlane is everything between the divider and the cabinet - 1.05 units, nearly
    four ball diameters. That is not a lane, it is the left third of the table, and a ball wandering
    down it would drain by accident rather than by a shot going wrong. This closes it to 0.50, just
    under two balls, and turns the strip outboard of it into the dead space where the posts, the
    lamp inserts and the artwork live on a real machine.
*/
#define PIN_OUTLANE_L_X         (-2.30f)
#define PIN_OUTLANE_MIN_Z       ( 3.40f)
#define PIN_OUTLANE_MAX_Z       ( 5.95f)
//The inlane rollover switch. CHANGE 3 of 9: -1.55 -> -1.60, the middle of the lane change 2 makes.
#define PIN_INLANE_L_X          (-1.60f)
#define PIN_INLANE_L_Z          ( 4.60f)
//The ball-save kicker in the outlane, outboard of it. Fires an up-table impulse, but ONLY while
//the save is lit - an outlane that always saved would not be an outlane.
//CHANGE 4 of 9: -2.50 -> -2.05, which is the middle of the outlane once change 5 gives it one.
#define PIN_SAVE_L_X            (-2.05f)
#define PIN_SAVE_L_Z            ( 5.70f)

/*
    The apron: the panel that covers the outhole, in two plates with the drain mouth between them.

    The mouth is centred on the play centre line and is 0.84 wide, so it comfortably swallows a
    ball coming down the middle while still reading as a mouth rather than as a missing panel. It
    is wider than the 0.343 between the flipper tips on purpose - a ball that grazes a tip on the
    way down has to still go in.
*/
#define PIN_APRON_GAP_MIN_X     (-0.72f)
#define PIN_APRON_GAP_MAX_X     ( 0.12f)
#define PIN_APRON_MIN_Z         ( 6.15f)

//--- Right edge: the launcher --------------------------------------------------------------------

//The lane the plunger fires up. Its left wall is the chute divider; its right wall is the cabinet.
#define PIN_CHUTE_X             ( 2.575f)   //centre line of the lane
#define PIN_CHUTE_DIVIDER_X     ( 2.25f)
#define PIN_CHUTE_MIN_Z         (-5.40f)    //where it opens into the top orbit
#define PIN_CHUTE_MAX_Z         ( 6.30f)

#define PIN_PLUNGER_X           PIN_CHUTE_X
#define PIN_PLUNGER_Z           ( 6.30f)
#define PIN_PLUNGER_TRAVEL      0.90f       //along -Z; a slider joint with a spring return

//Three skill-shot rollovers up the lane. Release at the right moment and the top one lights.
#define PIN_SKILL_Z_0           (-3.00f)
#define PIN_SKILL_Z_1           (-4.20f)
#define PIN_SKILL_Z_2           (-5.00f)

//The one-way gate at the mouth of the lane: into the orbit, never back down it.
#define PIN_GATE_X              ( 2.35f)
#define PIN_GATE_Z              (-5.30f)

//--- Top: the orbit and the wormholes ------------------------------------------------------------

/*
    The top orbit, as a circle rather than as three points.

    pinball_design.md 1.5 gives it as (+2.30,-5.40) -> (0,-6.20) -> (-2.30,-5.40), which is a chord
    4.60 wide with a sagitta of 0.80. The circle through those is
        R = h/2 + c^2/(8h) = 0.40 + 21.16/6.40 = 3.706
    centred at (0, -6.20 + R) = (0, -2.494). Kept as centre-and-radius because that is what a
    sampler wants and because rounding the three points would leave a curve that is not quite a
    circle - which a ball riding it at 60 u/s would find.

    Note the centre is x = 0, not PIN_CENTRE_X: the orbit is symmetric about the CABINET, while the
    bottom of the table is symmetric about the play area. That asymmetry is real and is what makes
    the right-hand orbit exit line up with the plunger lane.

    This arc is the INNER guide of the horseshoe. The outer boundary is the cabinet wall itself, so
    the lane is the gap between them: 0.40 at the apex, which is 1.5 ball diameters - a proper
    orbit, tight enough to hold a ball round it.
*/
#define PIN_ORBIT_CX            ( 0.0f)
#define PIN_ORBIT_CZ            (-2.494f)
#define PIN_ORBIT_RADIUS        ( 3.706f)
#define PIN_ORBIT_START_DEG     (-128.4f)   //the left-hand end, at (-2.30,-5.40)
#define PIN_ORBIT_END_DEG       ( -51.6f)   //the right-hand end, at (+2.30,-5.40)

/*
    The three "FUEL" rollover lanes. Complete the word for a bonus multiplier step.

    CHANGES 6 and 7 of 9, which are one problem: as published the FUEL lanes were at z = -5.00 and
    the centre wormhole saucer at z = -4.90 with a 0.24 rim, so the saucer's edge came within 0.10
    of the middle lane's wall. Two features in the same square inch.

    The room they have to share is narrow and the orbit is what makes it so. The lane walls have to
    stay inside the horseshoe, and the horseshoe closes in fast: at z = -5.35 the orbit rail is at
    x = +-2.36, at z = -5.85 it is at +-1.57, which is not wide enough for three lanes at all. So
    the lanes went DOWN-table into the widest band they fit in (walls z = -5.35 .. -4.95, needing
    +-2.10) and the saucers went up-table to clear them, leaving about half a unit - two balls -
    between the two rows.
*/
#define PIN_FUEL_Z              (-5.15f)
#define PIN_FUEL_X_0            (-1.50f)
#define PIN_FUEL_X_1            (-0.30f)
#define PIN_FUEL_X_2            ( 0.90f)
#define PIN_FUEL_WALL_MIN_Z     (-5.35f)
#define PIN_FUEL_WALL_MAX_Z     (-4.95f)

//The wormhole saucers. Hole triggers down to the subway; hold a ball in one to lock it.
#define PIN_WORM_0_X            (-1.90f)
#define PIN_WORM_0_Z            (-4.10f)
#define PIN_WORM_1_X            (-0.30f)
#define PIN_WORM_1_Z            (-4.20f)
#define PIN_WORM_2_X            ( 1.30f)
#define PIN_WORM_2_Z            (-4.10f)
#define PIN_SAUCER_RADIUS       0.24f

//--- Middle: the scoring cluster -----------------------------------------------------------------

//Pop bumpers. Two radii, and the gap between them is deliberate: the SKIRT is what the player sees
//and what the art has to clear, the COLLIDER is what the ball meets. A collider inside the skirt
//means the ball is already under the cap when it is thrown, which is what a pop bumper looks like.
#define PIN_BUMPER_SKIRT_RADIUS 0.42f
#define PIN_BUMPER_RADIUS       0.30f
/*
    How tall a pop bumper stands. 0.52 for the body, with the cap on top of it reaching 0.63 - about
    2.3 ball diameters, which is where a real one sits.

    It started at 0.62 (0.74 with the cap) and had to come down, because the right ramp's loop
    passes over this nest and at 0.74 the ramp floor went THROUGH the cap. Between lowering the
    bumper and raising the ramp, the bumper loses less: the ramp is already near the 0.90 ceiling
    the level plan gives it, and a slightly shorter bumper changes nothing about how it plays.
*/
#define PIN_BUMPER_HEIGHT       0.52f
#define PIN_BUMPER_0_X          (-1.35f)
#define PIN_BUMPER_0_Z          (-2.55f)
#define PIN_BUMPER_1_X          ( 0.15f)
#define PIN_BUMPER_1_Z          (-2.55f)
#define PIN_BUMPER_2_X          (-0.60f)
#define PIN_BUMPER_2_Z          (-3.45f)

/*
    The MISSION drop target bank. Hit one and it drops below the deck; clear all three and the bank
    resets together.

    CHANGE 10 of 10: the right-hand wall, not the left one the design document names.

    The left ramp enters at (-2.05,+2.00) and climbs up-table hugging that wall, passing x = -2.27
    at z = +0.10 and x = -2.30 at z = -1.00 - which is the bank's strip of wall, exactly. The
    clearance under the ramp floor over the three targets came out at 0.40, 0.22 and 0.04 against a
    ball 0.27 across: the top target was unreachable and the other two barely better.

    The left wall has nowhere else to offer. Up-table of the ramp's crest belongs to the upper
    flipper and the orbit return; down-table of its entry is the outlane, and a bank that feeds the
    outlane is a bank that punishes you for hitting it. The right wall between the standup pair and
    the orbit is genuinely empty - the right ramp has turned inboard long before it - and a bank
    there is reachable off both main flippers and off the upper one.

    It is the biggest departure from the published layout in this file and the one most worth
    arguing about.
*/
#define PIN_DROP_X              ( 2.05f)
#define PIN_DROP_Z_0            (-3.30f)
#define PIN_DROP_Z_1            (-3.90f)
#define PIN_DROP_Z_2            (-4.50f)
#define PIN_DROP_WIDTH          0.40f       //along Z
#define PIN_DROP_HEIGHT         0.30f
#define PIN_DROP_DEPTH          0.12f       //along X

/*
    Standup targets on the right. Static; pure switches.

    CHANGE 8 of 9: the lower one moved from z = -0.60 to z = -2.55, so the pair is now a vertical
    bank rather than straddling the right ramp. The right ramp crests at (+1.70,-0.60) - the same
    x, the same z, 0.70 up - so as published the target sat directly under the ramp's highest point
    with a ramp floor roofing it over. It would have been visible, labelled, and impossible to hit.
    By z = -2.55 the ramp's habitrail has swung inboard to about x = 0, and the corridor is clear.
*/
#define PIN_STANDUP_0_X         ( 1.75f)
#define PIN_STANDUP_0_Z         (-1.90f)
#define PIN_STANDUP_1_X         ( 1.75f)
#define PIN_STANDUP_1_Z         (-2.55f)

//The spinner, sitting in the mouth of the left ramp. A free hinge - no motor - counted by
//revolutions rather than by contacts.
#define PIN_SPINNER_X           (-2.05f)
#define PIN_SPINNER_Z           ( 1.60f)
#define PIN_SPINNER_WIDTH       0.34f
#define PIN_SPINNER_HEIGHT      0.26f

/*
    The gravity-well saucer: an eject hole that STARTS the mission the drop targets selected.

    CHANGE 9 of 9, and the worst of the collisions: (+1.55,+1.20) is underneath the right ramp's
    climb, which passes x = 1.55 at that z with 0.22 of air under it. A standup target roofed over
    is merely unhittable; a HOLE roofed over is a hole no ball can fall into, so the shot that
    starts every mission on the machine could never be made.

    The middle of the table is where it wants to be anyway. Nothing else is there - the ramps pass
    to either side, the bumpers are a long way up-table - it is reachable off both main flippers,
    and it is where the reference artwork puts the mission display.
*/
#define PIN_WELL_X              (-0.40f)
#define PIN_WELL_Z              ( 0.70f)

//--- Ramps ---------------------------------------------------------------------------------------

//A ramp's floor is this wide, and its rails stand this proud of it. Both are one ball diameter and
//change, which is the usual rule: narrower and the ball binds, wider and it rattles.
#define PIN_RAMP_WIDTH          0.46f
#define PIN_RAMP_RAIL_HEIGHT    0.30f

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
