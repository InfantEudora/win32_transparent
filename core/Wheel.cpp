#include "Wheel.h"

namespace WheelSuspension{

void UpdateVisual(Wheel& wheel,const WheelTuning& tuning,float timestep){
    float rest_length = tuning.rest_length;
    //Runs every tick regardless of contact state - an airborne wheel still spins by whatever
    //angular_velocity currently holds (the constraint lets it freewheel; a tank overrides it
    //with its track's speed), instead of freezing the moment contact is lost. See
    //Wheel::angular_velocity's own comment.
    wheel.roll_angle += wheel.angular_velocity * timestep;

    //Normalized here (not trusted from the caller/a debug-UI drag), same as Vehicle::
    //MakeWheelSettings does before handing the axis to the constraint - shared by both
    //wheel.visual's position and wheel.suspension_visual's orientation/scale below.
    vec3 axis_local = wheel.suspension_axis;
    float axis_length = axis_local.length();
    if (axis_length > 0.0001f){
        axis_local = axis_local * (1.0f / axis_length);
    }else{
        axis_local = vec3(0,-1,0);
    }

    if (wheel.visual){
        //The hub hangs (rest_length - compression) from the anchor along the suspension axis -
        //which for a plain vertical strut (0,-1,0) is a simple downward bob, and for an angled
        //one also slides the wheel fore/aft as the spring works.
        vec3 visual_pos = wheel.local_offset + axis_local * (rest_length - wheel.compression);
        wheel.visual->SetPosition(visual_pos);
        //Negated for a mirrored wheel - see Wheel::visual_mirrored for why a rotation-based
        //mirror needs this to keep the mirrored wheel's APPARENT rolling direction matching the
        //other side, even though roll_angle itself is the same, correct, unmirrored value either way.
        float visual_roll = wheel.visual_mirrored ? -wheel.roll_angle : wheel.roll_angle;
        wheel.visual->SetRotation(wheel.visual_base_rotation * quat(vec3(1,0,0),visual_roll));
        //Grows/shrinks the mesh to match whatever radius actually governs the raycast/physics -
        //see Wheel::visual_natural_radius. Left alone (no SetScale call at all) if that was never
        //probed, rather than forcing an assumed 1:1 scale that could be wrong for a mesh nobody
        //measured.
        if (wheel.visual_natural_radius > 0.0f){
            float scale = tuning.radius / wheel.visual_natural_radius;
            wheel.visual->SetScale(vec3(scale,scale,scale));
        }
    }

    if (wheel.suspension_visual){
        wheel.suspension_visual->SetPosition(wheel.local_offset); //the anchor - the modeled spring's own origin, see Wheel's comment
        wheel.suspension_visual->SetRotation(wheel.suspension_visual_rotation); //fixed, authored - see Wheel's comment on why this isn't derived from axis_local
        //Object::SetScale applies in the object's own local (pre-rotation) axes - see Object's
        //"scale, rotate, translate" transform order - so scaling local Y here shortens the mesh
        //along whatever direction suspension_visual_rotation just pointed it, regardless of
        //which direction that is.
        float extension_fraction = rest_length > 0.0f ? clamp((rest_length - wheel.compression) / rest_length,0.0f,1.0f) : 1.0f;
        wheel.suspension_visual->SetScale(vec3(1,extension_fraction,1));
    }
}

}
