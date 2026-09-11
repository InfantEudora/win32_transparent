#ifndef _APPLICATION_ANIMATION_H_
#define _APPLICATION_ANIMATION_H_

#include "Application.h"
#include "ship/Asteroid.h"
#include "ship/ShipCharacter.h"
#include "ship/HingedDoor.h"
#include "ship/Pickup.h"
#include "ship/ShipCollisionMasks.h"
#include "AsteroidExplosion.h"
#include "tinygltf/json.hpp"
using json = nlohmann::json;

//SimCommand types this app adds to core's set, numbered from SIM_CMD_LAST up - the same
//convention the input keycodes use with INPUT_LAST. See core/SimCommand.h for the
//command-is-data / handler-is-code split that lets an app do this without core knowing.
//
//Create an Asteroid at the command's position and scale, with its velocity and angular velocity.
//Not SIM_CMD_OBJECT_SPAWN_ASSET, because an Asteroid is more than a mesh: it picks one of three
//models, carries a sphere collider, gravity off, its own damping and mass, and the ship app's
//collision masks. That is exactly the case SimCommand.h's SPAWN_ASSET comment points at when it
//says an app wanting a collider on what it spawns registers its own type.
//
//Every value the spawn uses travels IN the command, including ones a caller picked at random -
//same reason TANK_CMD_VEHICLE_RESET carries its pose rather than looking it up. The command then
//says everything about what it does, and a replay reproduces this asteroid and not a fresh roll.
#define SHIP_CMD_SPAWN_ASTEROID (SIM_CMD_LAST + 0)
//Stand a hinged door panel up: `position` is where the post goes, `rotation` is which way the leaf
//faces (a rotation about world up - the handler takes the yaw out of it). The door creates two
//rigid bodies AND a hinge joint between them, so like the asteroid it has no business being built
//from the render or MCP thread.
#define SHIP_CMD_SPAWN_DOOR (SIM_CMD_LAST + 1)
//Float a pickup capsule at `position`, spinning at `angular_velocity`, scaled by `scale`.
//`subtype` is the PickupKind and value[0] the amount it is worth. A third type rather than a
//variant of SPAWN_ASSET for the same reason as the other two: a Pickup is a body with a TRIGGER
//collider and its own collision masks, none of which a plain asset spawn sets up.
#define SHIP_CMD_SPAWN_PICKUP (SIM_CMD_LAST + 2)

class ApplicationShip : public Application, public rp3d::EventListener{
public:
    ApplicationShip();

    void Init(void) override;
    void RunLogic() override;

    void DrawImGuiUI(void) override;

    DirectionalLight* sun = NULL;
    ShipCharacter* ship_character = NULL;

    //Raymarched volume material - see shaders/raymarch_volume.frag. The volume is an ordinary
    //Object whose mesh is tagged MESH_MODE_SHADER, so it goes through the renderer's custom
    //shader pass and the march happens only on the pixels the box covers. Its transform IS the
    //box the shader marches, so moving/scaling/rotating this object moves the volume.
    Shader* volume_shader = NULL;
    //Where volume_shader sits in Renderer::custom_shaders, and therefore what goes in the
    //volume mesh's custom_shader_index. Kept so a shader reload can replace that entry
    //instead of appending a second one.
    int volume_shader_index = 0;
    Object* volume = NULL;
    //A cube of its OWN, not assetmanager's "cube": Mesh pointers are shared between every
    //object built from an asset (see AssetManager::GetObjectFromAsset), so tagging the shared
    //cube MESH_MODE_SHADER would turn the hinged door panels into volumes too.
    Mesh* BuildVolumeCube();
    //Pushed to volume_shader through Shader::uniform_callback, i.e. called by the renderer on
    //the render thread with the shader already bound.
    void SetVolumeUniforms(void);
    //Recompiles raymarch_volume.frag from disk so the shader can be iterated on without a
    //restart. Careful: Shader's compile errors go through debug->Fatal, which exits - so a
    //typo in the shader takes the app down with it.
    void ReloadVolumeShader(void);
    //Runs shaders/noise3d.comp once to fill volume_noise with tileable 3D worley. Called from
    //Init on the render thread, which is the only thread that may touch GL.
    void BuildVolumeNoise(void);
    Texture* volume_noise = NULL;
    Shader* volume_noise_shader = NULL;

    //std::vector<Asteroid*> asteroids;
    std::vector<HingedDoor*> doors;

    Scene* CreateEmptyScene();
    //Places one door with its hinge at hinge_position, the leaf extending along +X turned by yaw.
    HingedDoor* AddDoor(const vec3& hinge_position, float yaw);

    //MCP: enough of a handle on the ship and the doors to drive a repeatable shot at one and read
    //back what it did - the ship app's own tools, registered on top of the core ones.
    void RegisterMCPTools();
    //Handlers for this app's own SimCommand types (SHIP_CMD_*). Called from Init(), next to
    //RegisterMCPTools.
    void RegisterCommandHandlers();
    //One SHIP_CMD_SPAWN_ASTEROID, built by whichever caller rolled these values.
    SimCommand MakeSpawnAsteroidCommand(const vec3& position, float scale,
                                        const vec3& velocity, const vec3& angular_velocity) const;
    //One SHIP_CMD_SPAWN_DOOR. yaw is in radians, as HingedDoor takes it.
    SimCommand MakeSpawnDoorCommand(const vec3& hinge_position, float yaw) const;
    //One SHIP_CMD_SPAWN_PICKUP.
    SimCommand MakeSpawnPickupCommand(const vec3& position, float scale, const vec3& angular_velocity,
                                      PickupKind kind, float amount) const;
    json GetDoorTelemetry(HingedDoor* door);
    //Every live asteroid, found by walking the scene rather than the `asteroids` vector - see the
    //note on that member.
    json GetAsteroidTelemetry();
    json GetCollectedTotals();
    //The one InputController, reached under its old name so the gamepad call sites read the same.
    //It IS main_window->inputcontroller - the separate GamePadController class is gone.
    InputController* gamepad_controller = NULL;

    //Append-only and never read, same caveat as the commented-out `asteroids` vector: an
    //explosion Destroy()s itself once it has shrunk away, without being removed from here.
    std::vector<AsteroidExplosion*>active_asteroid_explosions;
    //Asteroids that took their killing shot, staged by onContact for RunLogic to turn into
    //explosions. Only the FACT is staged, not a constructed explosion - see RunLogic for why.
    std::vector<Asteroid*>pending_asteroid_explosions;
    //Pickups the ship flew into, staged by onTrigger for RunLogic to bank and remove. Same rule as
    //above: a physics callback records what happened, RunLogic is what acts on it.
    std::vector<Pickup*>pending_collected_pickups;
    //Running total of what has been collected, per PickupKind - the placeholder for the energy /
    //ammo / health the ship does not have yet, so the trigger path can be seen to work and there
    //is somewhere obvious for those stats to replace. Index with a PickupKind.
    float collected_totals[4] = {0,0,0,0};

private:
    vec3 camera_target = {};
    float zoom_target = 20.0f;
    bool f_filemodal = false;
    std::string filemodal_filename;
    bool f_import_file = false;
    bool f_mode_grab = false;
    bool f_mode_camera_track = true;
    bool f_lock_ship_axis = true;

    //Volume tweakables, driven from the "Volume" ImGui panel and pushed by SetVolumeUniforms.
    float volume_density = 1.2f;    //Density where the noise is solid. Most of the box is now
                                    // empty air, so this is much higher than the constant-
                                    // density value it replaces (0.15) for a similar look.
    float volume_noise_scale = 1.0f;
    float volume_density_threshold = 0.45f;
    float volume_edge_falloff = 0.15f;
    int volume_noise_cells = 4;             //Worley cells per axis in the lowest-frequency channel
    int volume_noise_resolution = 128;      //Texture is this cubed - 128^3 RGBA8 is 8 MB
    //Wind, in noise-space units per second, integrated into volume_noise_offset every frame.
    //Noise space, not world space: at noise_scale 1 the box is one tile across, so 0.01 here
    //drifts the cloud through 1% of the box per second whatever size the box is.
    vec3 volume_wind = {0.015f,0.0f,0.006f};
    vec3 volume_noise_offset = {};
    float volume_light_absorption = 1.2f;
    //Scale on the sun's OWN brightness, not a replacement for it. 0.15 x the sun's 7.0
    //reproduces the look from when the volume ignored sun brightness and used 1.0 flat.
    float volume_sun_intensity = 0.15f;
    int volume_view_steps = 32;
    int volume_light_steps = 6;
    //Per point/cone light, per view step - so this one multiplies by the number of active
    //lights. Kept lower than the sun's because a local light's reach is short.
    int volume_point_light_steps = 3;
    //2 is the inverse square the reference shader uses. default.frag lights SURFACES with an
    //exponent of 1, so at 2 a light reads dimmer at range in fog than on a hull beside it.
    float volume_light_falloff = 2.0f;
    float volume_max_radiance = 10.0f;
    //Volume debug view: 0 off, 1 the marched interval, 2 the G-buffer the shader reads.
    //Matches the f_show_box uniform in shaders/raymarch_volume.frag.
    int volume_debug_view = 0;
    bool f_volume_visible = true;

    void onContact(const rp3d::CollisionCallback::CallbackData& callbackData) override;
    void onTrigger(const rp3d::OverlapCallback::CallbackData& callbackData) override;
};

#endif
