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

    //--- Base: a static slab. This Object IS the base (own mesh/collider, not a child) - same
    //role as DozerCharacter itself being its own chassis body.
    const float base_half_x = 1.0f, base_half_y = 0.3f, base_half_z = 1.0f;
    if (Object* base_visual = MakeCraneBox(assetmanager,base_half_x*2.0f,base_half_y*2.0f,base_half_z*2.0f)){
        SetMesh(base_visual->GetMesh());
        material_names = base_visual->material_names;
        SetScale(base_visual->GetScale());
        delete base_visual; //borrowed for mesh/material/scale only, never added to the scene - same pattern as AddTestSceneObjects' own cube_ref
    }
    SetPosition(base_position + vec3(0,base_half_y,0));
    AddPhysics(physicsworld);
    if (physics){
        physics->AddBoxCollider(vec3(base_half_x,base_half_y,base_half_z),vec3(0,0,0),quat().identity());
        physics->SetStatic(true);
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
    //boom-local z=-1.2 to +1.8 (the boom itself spans -2..+2), so the retracted part is hidden
    //inside the boom's opaque box and only what telescopes past the tip shows.
    if (boom){
        const float ext_length = 3.0f, ext_width = 0.22f, ext_thickness = 0.22f;
        const float ext_mass = 20.0f;
        const float ext_retracted_centre = 0.3f; //boom-local z of the extension's centre when fully retracted
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

void CraneCharacter::UpdatePhysicsState(){
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
    if (boom && piston_visual){
        //STATE_ACCESS_PHYSICS, not the default RENDERER - this runs on the physics thread, same
        //reasoning as ApplicationTank's own UpdateBuggyWheelSpinParticles comment on the same
        //distinction, just the opposite direction (physics code reading stale render state would
        //be a tick behind the boom's actual current pose here).
        vec3 boom_anchor_world = boom->GetWorldPosition(STATE_ACCESS_PHYSICS) + (boom->GetWorldRotation() * piston_boom_anchor_local);
        vec3 base_anchor_world = GetWorldPosition(STATE_ACCESS_PHYSICS) + (GetWorldRotation() * piston_base_anchor_local);
        vec3 delta = boom_anchor_world - base_anchor_world;
        float span = delta.length();
        if (span > 0.0001f){
            vec3 dir = delta * (1.0f / span);
            piston_visual->SetPosition(base_anchor_world + dir * (span * 0.5f));
            //The piston box's long axis is its local +Z - point that along dir. No assumption
            //about which plane the mechanism lies in, so this survives the base being moved or
            //yawed (the box is symmetric, so which end is which doesn't matter).
            piston_visual->SetRotation(quat::getquat(vec3(0,0,1),dir));
            piston_visual->SetScale(vec3(piston_radius * 2.0f,piston_radius * 2.0f,span));
        }
    }
    Object::UpdatePhysicsState();
}
