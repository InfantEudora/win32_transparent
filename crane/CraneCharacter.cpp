#include "CraneCharacter.h"
#include "type_helpers.h"
#include <cmath>
#include "Debug.h"

static Debugger* debug = new Debugger("Crane",DEBUG_INFO);

//Builds a box Object from the "cube" asset, scaled to (width,thickness,length) along its own
//local (X,Y,Z) - the same convention ApplicationTank's ramps/speed-bump use, so local +Z is the
//axis anything below tips to an angle around.
static Object* MakeCraneBox(AssetManager* assetmanager,float width,float thickness,float length){
    Object* box = assetmanager->GetObjectFromAsset("cube");
    if (!box){
        return NULL;
    }
    vec3 raw_extent = box->GetMesh() ? box->GetMesh()->GetExtents() : vec3(1,1,1);
    if (raw_extent.x > 0.0001f && raw_extent.y > 0.0001f && raw_extent.z > 0.0001f){
        box->SetScale(vec3(width / raw_extent.x,thickness / raw_extent.y,length / raw_extent.z));
    }
    return box;
}

//A rotation about world X by -angle_from_horizontal sends local +Z to (0, sin(angle),
//cos(angle)) - verified against this engine's quat(axis,angle) convention while building the
//ramps/speed-bump earlier, so every box here whose "long" axis needs to point at a given
//elevation angle reuses this same trick.
static quat ElevationRotation(float angle_from_horizontal){
    return quat(vec3(1,0,0),-angle_from_horizontal);
}
//Where a box's local (0,0,z_local) point ends up in world space once the box is rotated by
//ElevationRotation(angle) and centred at the world origin - same corner-mapping math the ramps
//use, just centred on the box's own origin instead of a corner.
static vec3 PointOnAngledAxis(float angle,float z_local){
    return vec3(0,z_local * sinf(angle),z_local * cosf(angle));
}

CraneCharacter::CraneCharacter(AssetManager* assetmanager, PhysicsWorld* physicsworld, Scene* target_scene, const vec3& base_position):Object(){
    name = "Crane";
    physics_world = physicsworld; //the magnet creates and destroys joints long after this returns

    //--- Base: a slab that can turn on the spot. This Object IS the base (own mesh/collider,
    //not a child) - same role as DozerCharacter itself being its own chassis body.
    const float base_half_x = 1.0f, base_half_y = 0.3f, base_half_z = 1.0f;
    if (Object* base_visual = MakeCraneBox(assetmanager,base_half_x*2.0f,base_half_y*2.0f,base_half_z*2.0f)){
        SetMesh(base_visual->GetMesh());
        SetMaterialNames(base_visual->GetMaterialNames());
        SetScale(base_visual->GetScale());
        delete base_visual; //borrowed for mesh/material/scale only, never added to the scene - same pattern as AddTestSceneObjects' own cube_ref
    }
    SetPosition(base_position + vec3(0,base_half_y,0));
    AddPhysics(physicsworld);
    if (physics){
        physics->AddBoxCollider(vec3(base_half_x,base_half_y,base_half_z),vec3(0,0,0),quat().identity());
        //KINEMATIC, not STATIC - see this class's header comment on the slew axis. Infinite mass
        //either way, so the boom still cannot push the base; the difference is that the solver
        //sees the slew as a velocity it can carry the hinged boom along with. The collider is
        //centred on this body's origin, so the angular velocity turns it about its own centre.
        physics->SetBodyType(rp3d::BodyType::KINEMATIC);
        //A kinematic body that stopped for a moment would otherwise be allowed to fall asleep,
        //and a sleeping body ignores the angular velocity set on it - the slew would simply not
        //start again. Same reason every dynamic body below does this.
        if (physics->body && physics->body->rigidbody){
            physics->body->rigidbody->setIsAllowedToSleep(false);
        }
    }

    //--- Geometry: elevation angle from horizontal, boom length, and where the boom's hinge and
    //the piston's two ends anchor. Computed rather than hand-transcribed - see the piston_visual
    //setup further down for where piston_boom_anchor_local comes from.
    const float elevation_angle = 45.0f * TYPE_PI / 180.0f;
    const float boom_length = 4.0f;
    const float boom_width = 0.3f, boom_thickness = 0.3f;
    const vec3 boom_hinge_anchor = base_position + vec3(0,0.9f,-0.6f);
    //(0, 0.7, -0.2) above base_position, expressed relative to this object's origin, which sits
    //base_half_y above base_position (see SetPosition above).
    piston_base_anchor_local = vec3(0,0.7f - base_half_y,-0.2f);
    const float piston_boom_reach = 1.2f; //distance along the boom from ITS hinge to where the piston visually attaches
    piston_boom_anchor_local = vec3(0,0,-boom_length * 0.5f + piston_boom_reach);

    //--- Boom: a single real HingeJoint to the base, no closed loop - see this class's own
    //header comment for why the earlier piston-as-a-real-joint version was scrapped. Raised/
    //lowered by the hinge's own motor (SetPistonSpeed) - see the joint setup below.
    boom = MakeCraneBox(assetmanager,boom_width,boom_thickness,boom_length);
    if (boom){
        boom->name = "Crane Boom";
        vec3 root_offset = PointOnAngledAxis(elevation_angle,-boom_length * 0.5f);
        boom->SetPosition(boom_hinge_anchor - root_offset);
        boom->SetRotation(ElevationRotation(elevation_angle));
        boom->AddPhysics(physicsworld);
        if (Physics* p = boom->GetPhysics()){
            //Mass comes from density*volume, and so does the inertia tensor - rp3d's setMass()
            //does NOT touch the inertia tensor, and update*FromColliders() only ever look at the
            //collider density, so density is the one knob that keeps mass and inertia consistent.
            const float boom_mass = 40.0f;
            float boom_density = boom_mass / (boom_width * boom_thickness * boom_length);
            p->AddBoxCollider(vec3(boom_width,boom_thickness,boom_length) * 0.5f,vec3(0,0,0),quat().identity(),boom_density);
            p->SetGravityEnabled(true);
            p->SetStatic(false);
            p->body->rigidbody->setIsAllowedToSleep(false);
        }
        target_scene->AddObject(boom);

        rp3d::HingeJointInfo info(GetRigidBody(),boom->GetRigidBody(),(rp3d::Vector3&)boom_hinge_anchor,rp3d::Vector3(1,0,0));
        //rp3d::JointInfo::isCollisionEnabled defaults to true - the boom's root and the base
        //physically overlap right at the hinge anchor by design (that's where the boom is
        //pinned), so left at the default the two bodies were also colliding with each other on
        //top of the joint constraint: real depenetration forces fighting the hinge every tick,
        //worst wherever the overlap geometry changes fastest (i.e. exactly at the ends of a
        //swing) - the actual cause of "spins out of control", more than the angle limit alone.
        info.isCollisionEnabled = false;
        //Angle is relative to THIS starting pose (elevation_angle), not absolute. The motor below
        //caps the boom's speed, so it can never arrive here carrying much momentum and the hard
        //limit is a gentle stop rather than an impact.
        info.isLimitEnabled = true;
        info.minAngleLimit = boom_min_angle;
        info.maxAngleLimit = boom_max_angle;
        //The motor IS the piston: a velocity-controlled actuator with a force rating. Speed 0
        //holds the boom against gravity (closed valve), nonzero moves it at a bounded rate, and
        //maxMotorTorque is what it can exert before it sags under load.
        info.isMotorEnabled = true;
        info.motorSpeed = 0.0;
        info.maxMotorTorque = boom_max_motor_torque;
        boom_hinge = dynamic_cast<rp3d::HingeJoint*>(physicsworld->rp_world->createJoint(info));
    }

    //--- Extension: a thinner box nested inside the boom, on a SliderJoint whose axis is the
    //boom's own +Z (toward the tip). Fully retracted at creation: it sits inside the boom from
    //boom-local z=-0.8 to +2.2 (the boom itself spans -2..+2), so the retracted part is hidden
    //inside the boom's opaque box, only what telescopes past the tip shows, and the tip itself
    //always pokes out far enough that the hook's swivel (hung off it below) never sits inside
    //the boom's collider.
    if (boom){
        const float ext_length = 3.0f, ext_width = 0.22f, ext_thickness = 0.22f;
        const float ext_mass = 20.0f;
        const float ext_retracted_centre = 0.7f; //boom-local z of the extension's centre when fully retracted
        extension = MakeCraneBox(assetmanager,ext_width,ext_thickness,ext_length);
        if (extension){
            extension->name = "Crane Boom Extension";
            vec3 ext_position = boom->GetPosition() + PointOnAngledAxis(elevation_angle,ext_retracted_centre);
            extension->SetPosition(ext_position);
            extension->SetRotation(ElevationRotation(elevation_angle));
            extension->AddPhysics(physicsworld);
            if (Physics* p = extension->GetPhysics()){
                float ext_density = ext_mass / (ext_width * ext_thickness * ext_length);
                p->AddBoxCollider(vec3(ext_width,ext_thickness,ext_length) * 0.5f,vec3(0,0,0),quat().identity(),ext_density);
                p->SetGravityEnabled(true);
                p->SetStatic(false);
                p->body->rigidbody->setIsAllowedToSleep(false);
            }
            target_scene->AddObject(extension);

            //body1 = boom, body2 = extension, so getTranslation() (= (anchor2-anchor1).axis) grows
            //as the extension moves out along the boom, and a positive motor speed extends.
            vec3 slider_axis_world = PointOnAngledAxis(elevation_angle,1.0f);
            rp3d::SliderJointInfo sinfo(boom->GetRigidBody(),extension->GetRigidBody(),(rp3d::Vector3&)ext_position,(rp3d::Vector3&)slider_axis_world);
            sinfo.isCollisionEnabled = false; //nested inside the boom by design
            sinfo.isLimitEnabled = true;
            sinfo.minTranslationLimit = extension_min;
            sinfo.maxTranslationLimit = extension_max;
            sinfo.isMotorEnabled = true;
            sinfo.motorSpeed = 0.0;
            sinfo.maxMotorForce = extension_max_motor_force;
            extension_slider = dynamic_cast<rp3d::SliderJoint*>(physicsworld->rp_world->createJoint(sinfo));
        }

        //--- Hook chain: swivel on a ball-and-socket just past the extension's tip, hook on a
        //slider ("cable") hanging straight down from the swivel - see the header comment.
        if (extension){
            const float swivel_size = 0.2f, swivel_mass = 2.0f;
            const float hook_width = 0.2f, hook_height = 0.3f, hook_mass = 10.0f;
            //Pivot a little beyond the extension's tip face so the swivel box hangs clear of it.
            vec3 pivot_world = extension->GetPosition() + PointOnAngledAxis(elevation_angle,ext_length * 0.5f + 0.25f);

            swivel = MakeCraneBox(assetmanager,swivel_size,swivel_size,swivel_size);
            if (swivel){
                swivel->name = "Crane Hook Swivel";
                swivel->SetPosition(pivot_world); //pinned at its own centre - a pure pivot
                swivel->AddPhysics(physicsworld);
                if (Physics* p = swivel->GetPhysics()){
                    p->AddBoxCollider(vec3(swivel_size,swivel_size,swivel_size) * 0.5f,vec3(0,0,0),quat().identity(),swivel_mass / (swivel_size * swivel_size * swivel_size));
                    p->SetGravityEnabled(true);
                    p->SetStatic(false);
                    p->body->rigidbody->setIsAllowedToSleep(false);
                }
                target_scene->AddObject(swivel);

                rp3d::BallAndSocketJointInfo binfo(extension->GetRigidBody(),swivel->GetRigidBody(),(rp3d::Vector3&)pivot_world);
                binfo.isCollisionEnabled = false; //the swivel sits right against the tip face
                hook_pivot = dynamic_cast<rp3d::BallAndSocketJoint*>(physicsworld->rp_world->createJoint(binfo));
            }

            hook = swivel ? MakeCraneBox(assetmanager,hook_width,hook_height,hook_width) : NULL;
            if (hook){
                hook->name = "Crane Hook";
                hook->SetPosition(pivot_world - vec3(0,hook_cable_min_length,0));
                hook->AddPhysics(physicsworld);
                if (Physics* p = hook->GetPhysics()){
                    p->AddBoxCollider(vec3(hook_width,hook_height,hook_width) * 0.5f,vec3(0,0,0),quat().identity(),hook_mass / (hook_width * hook_height * hook_width));
                    p->SetGravityEnabled(true);
                    p->SetStatic(false);
                    p->body->rigidbody->setIsAllowedToSleep(false);
                }
                target_scene->AddObject(hook);

                //body1 = swivel, body2 = hook, axis = world down at creation (= the swivel's local
                //-Y from then on, so the cable always hangs along whatever way the swivel points),
                //so getTranslation() grows as the hook drops and a positive motor speed lowers it.
                rp3d::SliderJointInfo cinfo(swivel->GetRigidBody(),hook->GetRigidBody(),(rp3d::Vector3&)pivot_world,rp3d::Vector3(0,-1,0));
                cinfo.isCollisionEnabled = false;
                cinfo.isLimitEnabled = true;
                cinfo.minTranslationLimit = hook_min;
                cinfo.maxTranslationLimit = hook_max;
                cinfo.isMotorEnabled = true;
                cinfo.motorSpeed = 0.0;
                cinfo.maxMotorForce = hook_max_motor_force;
                hook_slider = dynamic_cast<rp3d::SliderJoint*>(physicsworld->rp_world->createJoint(cinfo));

                //--- The magnet's field: a second collider on the HOOK's body, a sphere sitting
                //over its bottom face, marked as a TRIGGER so it reports overlaps without ever
                //pushing anything. This is the "sphere cast" - see the class header comment.
                magnet_local_offset = vec3(0,-hook_height * 0.5f,0);
                if (Physics* p = hook->GetPhysics()){
                    //Density near zero, NOT the hook's: Physics::AddSphereCollider ends with
                    //updateMassPropertiesFromColliders(), which sums every collider on the body
                    //whether or not it is a trigger. At the hook's own density this sensor would
                    //silently add tens of kg to the thing the winch has to lift.
                    p->AddSphereCollider(magnet_radius,magnet_local_offset,quat().identity(),0.0001f);
                    if (p->body){
                        magnet_collider = p->body->last_collider;
                        if (magnet_collider){
                            magnet_collider->setIsTrigger(true);
                        }
                    }
                }

                cable_visual = MakeCraneBox(assetmanager,cable_radius * 2.0f,cable_radius * 2.0f,hook_cable_min_length);
                if (cable_visual){
                    cable_visual->name = "Crane Cable (visual)";
                    target_scene->AddObject(cable_visual);
                }
            }
        }
    }

    //--- Piston: purely visual, no physics/joint - see this class's own header comment. Sized
    //once here (natural/reference length = the CURRENT base-to-boom distance at construction
    //time); UpdatePhysicsState rescales it every tick to whatever that distance actually is.
    if (boom){
        vec3 piston_boom_anchor_world = boom->GetPosition() + PointOnAngledAxis(elevation_angle,-boom_length * 0.5f + piston_boom_reach);
        float initial_span = (piston_boom_anchor_world - (GetPosition() + piston_base_anchor_local)).length(); //base is unrotated at construction
        piston_visual = MakeCraneBox(assetmanager,piston_radius*2.0f,piston_radius*2.0f,max(initial_span,0.01f));
        if (piston_visual){
            piston_visual->name = "Crane Piston (visual)";
            target_scene->AddObject(piston_visual);

        }
    }
}

CraneCharacter::~CraneCharacter(){
}

void CraneCharacter::SetPistonSpeed(float speed){
    boom_speed_command = clamp(speed,-1.0f,1.0f);
}

void CraneCharacter::SetExtensionSpeed(float speed){
    extension_speed_command = clamp(speed,-1.0f,1.0f);
}

void CraneCharacter::SetHookSpeed(float speed){
    hook_speed_command = clamp(speed,-1.0f,1.0f);
}

void CraneCharacter::SetSlewSpeed(float speed){
    slew_speed_command = clamp(speed,-1.0f,1.0f);
}

void CraneCharacter::SetMagnetEnabled(bool enabled){
    magnet_enabled = enabled;
}

float CraneCharacter::GetGrabbedMass() const{
    if (!grabbed_object){
        return 0.0f;
    }
    Physics* p = grabbed_object->GetPhysics();
    return p ? p->GetMass() : 0.0f;
}

vec3 CraneCharacter::GetMagnetWorldPosition(){
    if (!hook){
        return GetWorldPosition();
    }
    //Only ever read from the physics thread, and GetWorldPosition is now live on it - there is a
    //single ObjectState, so this is the hook's current pose and not a tick-old render copy.
    return hook->GetWorldPosition() + (hook->GetWorldRotation() * magnet_local_offset);
}

//Inside the physics step - record only. See the class header comment.
void CraneCharacter::OnMagnetFieldOverlap(rp3d::Collider* other_collider){
    if (!other_collider){
        return;
    }
    //Recorded even while the magnet is off or already holding something. Nothing acts on the
    //list in those cases, but it still feeds magnet_in_field, and "what can the magnet see right
    //now" is exactly what you want to read while working out why it did not pick something up.
    rp3d::Body* body = other_collider->getBody();
    if (!body){
        return;
    }
    //Every Object with physics stamps itself here - see Object::AddPhysics. A body without it
    //is not something this scene knows how to hold on to.
    Object* candidate = (Object*)body->getUserData();
    if (candidate){
        magnet_candidates.push_back(candidate);
    }
}

bool CraneCharacter::IsGrabbable(Object* candidate) const{
    if (!candidate || candidate == this || candidate == boom || candidate == extension ||
        candidate == swivel || candidate == hook){
        return false; //the crane's own parts - welding the hook to them would lock the mechanism
    }
    Physics* p = candidate->GetPhysics();
    if (!p || !p->body || !p->body->rigidbody){
        return false;
    }
    //STATIC and KINEMATIC bodies have infinite mass. A FixedJoint to the terrain does not lift
    //the terrain, it pins the hook - and through the hook, the whole boom - to the world.
    if (p->body->rigidbody->getType() != rp3d::BodyType::DYNAMIC){
        return false;
    }
    float mass = p->GetMass();
    return mass > 0.0f && mass <= magnet_max_mass;
}

//Between physics steps (called from UpdatePhysicsState), which is the only point in the tick at
//which a joint may be created or destroyed - see the class header comment.
void CraneCharacter::UpdateMagnet(){
    if (!physics_world || !physics_world->rp_world){
        magnet_candidates.clear();
        return;
    }

    //Switched off (or the load stopped being holdable) - drop it.
    if (magnet_joint && !magnet_enabled){
        physics_world->rp_world->destroyJoint(magnet_joint);
        magnet_joint = NULL;
        grabbed_object = NULL;
    }

    magnet_in_field = (int)magnet_candidates.size();
    magnet_grabbable_in_field = 0;
    for (Object* candidate:magnet_candidates){
        if (IsGrabbable(candidate)){
            magnet_grabbable_in_field++;
        }
    }

    if (magnet_enabled && !magnet_joint && hook && !magnet_candidates.empty()){
        //Nearest to the pad wins. Several bodies can be in the field at once, and the one the
        //operator is aiming at is the one they have lowered the magnet onto.
        vec3 pad = GetMagnetWorldPosition();
        Object* best = NULL;
        float best_distance = 0.0f;
        for (Object* candidate:magnet_candidates){
            if (!IsGrabbable(candidate)){
                continue;
            }
            float distance = (candidate->GetWorldPosition() - pad).length();
            if (!best || distance < best_distance){
                best = candidate;
                best_distance = distance;
            }
        }
        if (best){
            //Anchored at the pad rather than at either body's origin, so the load hangs from the
            //face that picked it up and keeps whatever pose it was lying in - a magnet does not
            //snap its load square.
            vec3 anchor = pad;
            rp3d::FixedJointInfo info(hook->GetRigidBody(),best->GetRigidBody(),(rp3d::Vector3&)anchor);
            //Same reason the boom hinge disables it: the load is held right against the hook's
            //own box collider, so left enabled the two would be resolving a real penetration
            //every tick on top of the joint holding them together - the pair fight, and it shows
            //up as the load buzzing against the pad.
            info.isCollisionEnabled = false;
            magnet_joint = dynamic_cast<rp3d::FixedJoint*>(physics_world->rp_world->createJoint(info));
            if (magnet_joint){
                grabbed_object = best;
                //A body that had settled and gone to sleep would otherwise stay asleep, joint or
                //no joint, and be dragged along a tick behind everything else.
                if (Physics* p = best->GetPhysics()){
                    p->WakeUp();
                }
            }
        }
    }

    //Consumed, whether or not anything was grabbed: these are this tick's overlaps. Cleared here
    //rather than at the start of onTrigger because onTrigger is not called at all on a tick with
    //no overlaps anywhere in the world, which would leave a stale candidate to grab later.
    magnet_candidates.clear();
}

float CraneCharacter::GetSlewAngle(){
    //From the base's own forward vector, not quat::get_yaw() - that one is an asin(), so it folds
    //back on itself past +-90 degrees, and this axis is explicitly allowed to turn all the way
    //round. atan2 of the heading gives the full +-180. 0 = the crane's spawn heading, since the
    //base is created unrotated and only ever turned about Y from there.
    vec3 forward = GetWorldRotation() * vec3(0,0,-1);
    return atan2f(-forward.x,-forward.z);
}

//Stretches a cosmetic box (its long axis = local +Z) between two world points.
static void SpanVisual(Object* visual,const vec3& from,const vec3& to,float radius){
    vec3 delta = to - from;
    float span = delta.length();
    if (span <= 0.0001f){
        return;
    }
    vec3 dir = delta * (1.0f / span);
    visual->SetPosition(from + dir * (span * 0.5f));
    visual->SetRotation(quat::getquat(vec3(0,0,1),dir));
    visual->SetScale(vec3(radius * 2.0f,radius * 2.0f,span));
}

void CraneCharacter::UpdatePhysicsState(){
    if (physics){
        //The slew "motor". Set unconditionally, including at 0 - a kinematic body keeps whatever
        //velocity it was last given forever (nothing damps it, and no force acts on it), so
        //writing it only when the command is nonzero would leave the crane spinning after the
        //key came up. Purely angular: the base turns, it never travels.
        physics->SetAngularVelocity(vec3(0,slew_speed_command * slew_max_rate,0));
    }
    if (boom_hinge){
        //Negative rotation about the hinge's X axis increases elevation (see ElevationRotation),
        //so a POSITIVE command needs a NEGATIVE motor speed to raise the boom, and raising moves
        //the angle DOWN toward boom_min_angle.
        float angle = boom_hinge->getAngle();
        float remaining = boom_speed_command > 0.0f ? angle - boom_min_angle : boom_max_angle - angle;
        //Taper to 0 over the last boom_limit_margin so the motor stops driving into the hard
        //limit - a cylinder running out of stroke, rather than a valve held open against an end
        //stop. Velocity-level, so unlike a torque ramp it can't oscillate.
        float taper = clamp(remaining / boom_limit_margin,0.0f,1.0f);
        boom_hinge->setMotorSpeed(-boom_speed_command * boom_max_rate * taper);
    }
    if (extension_slider){
        //Same velocity-level limit taper as the boom. Positive command = positive motor speed =
        //translation increasing toward extension_max (see the joint's construction comment).
        float translation = extension_slider->getTranslation();
        float remaining = extension_speed_command > 0.0f ? extension_max - translation : translation - extension_min;
        float taper = clamp(remaining / extension_limit_margin,0.0f,1.0f);
        extension_slider->setMotorSpeed(extension_speed_command * extension_max_rate * taper);
    }
    if (hook_slider){
        //Winch: same taper again. Positive command = positive motor speed = more cable paid out.
        float translation = hook_slider->getTranslation();
        float remaining = hook_speed_command > 0.0f ? hook_max - translation : translation - hook_min;
        float taper = clamp(remaining / hook_limit_margin,0.0f,1.0f);
        hook_slider->setMotorSpeed(hook_speed_command * hook_max_rate * taper);
    }
    UpdateMagnet();
    if (swivel && hook && cable_visual){
        vec3 hook_top = hook->GetWorldPosition() + (hook->GetWorldRotation() * vec3(0,0.15f,0));
        SpanVisual(cable_visual,swivel->GetWorldPosition(),hook_top,cable_radius);
    }
    if (boom && piston_visual){
        //Runs on the physics thread, and reads the boom's current pose: with one ObjectState per
        //object there is no longer a tick-old render copy to accidentally read instead.
        vec3 boom_anchor_world = boom->GetWorldPosition() + (boom->GetWorldRotation() * piston_boom_anchor_local);
        vec3 base_anchor_world = GetWorldPosition() + (GetWorldRotation() * piston_base_anchor_local);
        //No assumption about which plane the mechanism lies in, so this survives the base being
        //moved or yawed (the box is symmetric, so which end is which doesn't matter).
        SpanVisual(piston_visual,base_anchor_world,boom_anchor_world,piston_radius);
    }
    Object::UpdatePhysicsState();
}
