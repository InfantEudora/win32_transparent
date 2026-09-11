#include "ApplicationShip.h"
#include "MCPServer.h"
#include "Debug.h"

static Debugger *debug = new Debugger("ApplicationShip", DEBUG_ALL);

#define INPUT_SHOOT             INPUT_LAST+1
#define INPUT_F                 INPUT_LAST+2
#define INPUT_G                 INPUT_LAST+3
#define INPUT_Q                 INPUT_LAST+4
#define INPUT_E                 INPUT_LAST+5
#define GAMEPAD_LEFT_STICK_X    INPUT_LAST+6
#define GAMEPAD_LEFT_STICK_Y    INPUT_LAST+7
#define GAMEPAD_RIGHT_STICK_X   INPUT_LAST+8
#define GAMEPAD_RIGHT_STICK_Y   INPUT_LAST+9
#define GAMEPAD_R2L2            INPUT_LAST+10

ApplicationShip::ApplicationShip():Application(){
    debug->Info("Created new ApplicationShip.\n");
};

Scene* ApplicationShip::CreateEmptyScene(){
    Scene* scene = CreateNewScene("Empty Test Scene");
    //Make a sun
    sun = new DirectionalLight();
    sun->name = "Directional Light (Sun)";
    //Position no longer affects the shading angle (that comes from the light's forward), it only
    //places the shadow frustum. Keep the sun well clear of the grid so the whole 40x40 of it sits
    //between the shadow camera's near and far planes.
    sun->SetPosition(vec3(-50,50,50));
    sun->color = vec3(1,0.8,0.6);
    sun->brightness = 7.0;
    //Half-extent 30 covers the 40x40 grid; anything outside is simply unshadowed.
    sun->SetupOrthographic(4096,4096,30.0f,1.0f,200.0f);
    sun->SetLookAt(vec3());
    scene->AddObject(sun);

    scene->inputcontroller->AddKeyMap(VK_SPACE,INPUT_SHOOT);
    scene->inputcontroller->AddKeyMap('F',INPUT_F);
    scene->inputcontroller->AddKeyMap('G',INPUT_G);
    scene->inputcontroller->AddKeyMap('Q',INPUT_Q);
    scene->inputcontroller->AddKeyMap('E',INPUT_E);
    scene->camera->SetPosition(vec3(0,25,0));
    scene->camera->SetLookAt(vec3(0,0.0,0));
    //The straight-down pose tracking mode keeps the camera in. Remembered rather than rebuilt
    //later because a lookat straight down the world up axis is degenerate (the cross product that
    //makes the camera's left axis collapses), so the orientation that comes out of THIS call is
    //the only definition of "overhead" this app has. See RunLogic, which eases back to it.
    overhead_rotation = scene->camera->GetRotation();

    //Add phyics
    scene->physics_world = new PhysicsWorld();
    scene->physics_world->SetGravity(vec3(0,-9.81,0));
    scene->physics_world->SetDebugRendering(false);
    scene->physics_world->rp_world->setEventListener(this);
    return scene;
}

//One door panel on a fixed vertical hinge - see ship/HingedDoor.h. The door adds its own hinge
//post to the scene; only the panel goes in `doors`, since that is the body anything interacts with.
HingedDoor* ApplicationShip::AddDoor(const vec3& hinge_position, float yaw){
    HingedDoor* door = new HingedDoor(assetmanager,main_scene->physics_world,main_scene,hinge_position,yaw);
    main_scene->AddObject(door);
    doors.push_back(door);
    return door;
}

//A unit cube spanning -0.5..0.5 on every axis, which is the box raymarch_volume.frag marches.
//Sized that way so an Object scale of (10,4,10) reads as a 10x4x10 volume in world units.
//
//Built here rather than taken from the GLTF "cube" asset for two reasons: asset meshes are
//shared by pointer, so tagging one MESH_MODE_SHADER would affect everything else using it, and
//the shader's BOX_MIN/BOX_MAX have to agree with the mesh's actual extents - which is a
//guarantee worth having in code next to the shader rather than in a .glb someone might edit.
Mesh* ApplicationShip::BuildVolumeCube(){
    //Per face: the outward normal, and two edge vectors with cross(u,v) == n, so walking
    //corner -> +u -> +u+v -> +v comes out counter-clockwise seen from outside the cube. That
    //winding is what makes the renderer's default GL_BACK culling cull the inside faces (and,
    //once the volume pass flips to GL_FRONT, keep exactly the inside ones).
    const vec3 face_normal[6] = {
        vec3( 1, 0, 0), vec3(-1, 0, 0),
        vec3( 0, 1, 0), vec3( 0,-1, 0),
        vec3( 0, 0, 1), vec3( 0, 0,-1),
    };
    const vec3 face_u[6] = {
        vec3( 0, 0,-1), vec3( 0, 0, 1),
        vec3( 1, 0, 0), vec3( 1, 0, 0),
        vec3( 1, 0, 0), vec3(-1, 0, 0),
    };
    const vec3 face_v[6] = {
        vec3( 0, 1, 0), vec3( 0, 1, 0),
        vec3( 0, 0,-1), vec3( 0, 0, 1),
        vec3( 0, 1, 0), vec3( 0, 1, 0),
    };

    std::vector<vertex>verts;
    for (int f = 0;f < 6;f++){
        vec3 n = face_normal[f];
        vec3 u = face_u[f];
        vec3 v = face_v[f];
        vec3 corner = (n * 0.5f) - (u * 0.5f) - (v * 0.5f);

        //The quad's four corners in winding order, with matching UVs.
        vec3 p[4] = {corner, corner + u, corner + u + v, corner + v};
        vec2 uv[4] = {vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,1)};
        const int order[6] = {0,1,2, 0,2,3};

        for (int i = 0;i < 6;i++){
            vertex vert;
            vert.pos = p[order[i]];
            vert.normal = n;
            vert.tangent = u;
            vert.uv = uv[order[i]];
            //The volume shader reads no material, and the object's slot 0 is -1 anyway.
            vert.matid = 0;
            verts.push_back(vert);
        }
    }

    Mesh* mesh = new Mesh();
    mesh->SetMeshData(&verts.at(0),verts.size());
    //SetMeshData leaves the mesh in MESH_MODE_NORMAL; this is what moves it out of the main
    //geometry pass and into the renderer's custom shader pass.
    mesh->mesh_mode = MESH_MODE_SHADER;
    return mesh;
}

/*
    One raymarched volume Object.

    They all share volume_mesh on purpose. The box a volume marches comes from its own
    transform, read per instance in the shader, not from a uniform - so N volumes are N
    instances of one mesh and the renderer emits a single draw call for all of them. Giving each
    its own mesh would split that into N draws and buy nothing.

    The catch of sharing one draw call: the instances are not depth sorted, and a volume writes
    no depth, so two that OVERLAP on screen blend in instance order rather than back to front.
    Keep them apart, or sort them if that ever stops being true.
*/
Object* ApplicationShip::AddVolume(const char* name, const vec3& position, const vec3& size){
    if (!volume_mesh){
        volume_mesh = BuildVolumeCube();
        //Says WHICH custom shader draws this mesh. Only matters once more than one is
        //registered, but setting it from the index the renderer handed back keeps that true.
        volume_mesh->custom_shader_index = volume_shader_index;
    }

    Object* v = new Object();
    v->name = name;
    v->SetMesh(volume_mesh);
    v->SetPosition(position);
    v->SetScale(size);
    //No material: the shader computes its own colour and never touches the material buffer.
    v->material_slot[0] = -1;
    //Belt and braces. MESH_MODE_SHADER meshes no longer go through DeferredPass at all, so the
    //box does not reach the object-id buffer and could not be picked anyway - but nothing about
    //a fog box is meant to be clickable.
    v->SetPickability(false);
    main_scene->AddObject(v);
    volumes.push_back(v);
    return v;
}

/*
    Fills volume_noise by running shaders/noise3d.comp over it once. Generated rather than
    loaded from disk because it is procedural, tiny in code, and the parameters (cell count,
    resolution) are things worth changing while looking at the result - a 128^3 RGBA8 file would
    be 8 MB of asset to re-export every time.

    Cheap enough to be unnoticeable at startup: one dispatch over 128^3 texels.
*/
void ApplicationShip::BuildVolumeNoise(void){
    if (!volume_noise_shader){
        volume_noise_shader = new Shader();
        volume_noise_shader->CreateComputeShader("shaders/noise3d.comp");
    }
    if (!volume_noise){
        volume_noise = new Texture();
        volume_noise->name = "volume_noise";
        volume_noise->Create3D(volume_noise_resolution,GL_RGBA8);
    }

    volume_noise_shader->Use();
    volume_noise_shader->Setint("cells_base",volume_noise_cells);
    //layered=GL_TRUE for a 3D image: the shader writes the whole volume, not one slice.
    glBindImageTexture(0,volume_noise->texture_id,0,GL_TRUE,0,GL_WRITE_ONLY,GL_RGBA8);

    //local_size is 8x8x8 in the shader, so one work group per 8 texels per axis.
    int groups = volume_noise_resolution / 8;
    glDispatchCompute(groups,groups,groups);
    //The volume shader samples this as a texture, not as an image, so wait for the writes to be
    //visible to texture fetches specifically.
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

    debug->Info("Built %i^3 volume noise, %i base worley cells\n",volume_noise_resolution,volume_noise_cells);
}

//Called by the renderer from inside the MESH_MODE_SHADER pass, with volume_shader bound.
void ApplicationShip::SetVolumeUniforms(void){
    if (!volume_shader){
        return;
    }
    volume_shader->Setfloat("light_absorption",volume_light_absorption);
    volume_shader->Setfloat("sun_intensity",volume_sun_intensity);
    volume_shader->Setint("num_view_steps",volume_view_steps);
    volume_shader->Setint("num_light_steps",volume_light_steps);
    volume_shader->Setint("num_point_light_steps",volume_point_light_steps);
    volume_shader->Setfloat("light_falloff",volume_light_falloff);
    volume_shader->Setfloat("max_radiance",volume_max_radiance);
    //One value shared with the surface shaders - see Renderer::cone_softness.
    volume_shader->Setfloat("cone_softness",renderer->cone_softness);
    volume_shader->Setint("f_show_box",volume_debug_view);

    //Shape, density and the wind offset - the uniforms density.glsl declares. Shared with the
    //cloud shadow compute shader, which is the point: it has to march the same cloud. The wind
    //itself is advanced in PreRender, before the shadow map is built.
    SetSharedDensityUniforms(volume_shader);

    if (volume_noise){
        glBindTextureUnit(TEXUNIT_APP_RESERVED,volume_noise->texture_id);
    }

    //Render the box's INSIDE faces only, write no depth, and do not depth TEST either.
    //
    //Back faces rather than front is what makes the volume survive the camera being inside the
    //box - with front faces there would be nothing left to rasterise once you fly into it - and
    //it also guarantees exactly one fragment per pixel, so the volume is blended once instead
    //of twice. The shader intersects the box analytically, so it does not care which of the two
    //it was handed. No depth write because a volume must not occlude anything drawn after it.
    //
    //The depth TEST has to go for a different reason, and it is the whole reason a ship inside
    //a cloud was not fogged at all. Rasterising back faces puts this fragment's depth at the
    //FAR side of the box, so anything solid standing inside the volume is nearer and the
    //fixed-function test threw the fragment away - on exactly the pixels that needed fog in
    //front of the ship. The clamp in raymarch_volume.frag was written for that case and could
    //never be reached: it only ran where the far face had already passed the test, i.e. where
    //nothing solid was nearer than the box exit, and there the clamp is a no-op.
    //
    //With the test off the shader owns depth entirely - it clamps the march to the G-buffer's
    //world position and discards when the scene is in front of the box. The cost is that we
    //lose early-Z rejection for a volume hidden behind a wall; that path now runs the shader
    //prologue and discards before the march loop.
    //
    //Renderer::CustomShaderPass restores GL_BACK / depth writes / the depth test right after
    //this pass, so this does not leak into the next frame's depth passes.
    glCullFace(GL_FRONT);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
}

//The uniforms shaders/density.glsl declares. Pushed to every program that includes it, because
//the cloud those programs describe has to be the same cloud - raymarch_volume.frag draws it and
//cloud_shadow.comp decides where its shadow falls.
void ApplicationShip::SetSharedDensityUniforms(Shader* s){
    if (!s){
        return;
    }
    s->Setfloat("noise_scale",volume_noise_scale);
    s->Setfloat("density_threshold",volume_density_threshold);
    s->Setfloat("edge_falloff",volume_edge_falloff);
    s->Setfloat("volume_density",volume_density);
    s->Setvec3("noise_offset",volume_noise_offset);
}

//Called once from Init, on the frame thread - the only thread allowed to touch GL.
void ApplicationShip::BuildCloudShadowResources(void){
    if (!cloud_shadow_shader){
        cloud_shadow_shader = new Shader();
        cloud_shadow_shader->CreateComputeShader("shaders/cloud_shadow.comp");
    }
    if (!cloud_shadow_camera){
        //Not added to the scene: nothing should be able to select it, and it is refitted from
        //the sun every frame anyway.
        cloud_shadow_camera = new Camera();
        cloud_shadow_camera->name = "Cloud Shadow Camera";
    }
    if (cloud_shadow_ssbo == (uint32_t)-1){
        GLuint ssbo = 0;
        glCreateBuffers(1,&ssbo);
        cloud_shadow_ssbo = ssbo;
    }
    if (!cloud_shadow_map){
        cloud_shadow_map = new Texture();
        cloud_shadow_map->name = "cloud_shadow";
        //CLAMP_TO_EDGE matters most on R, the depth axis: it is what makes a receiver above the
        //layer read the first slice (nothing in front of it yet) and one below the layer read
        //the last (the whole column of cloud in front of it), with no bounds test in the
        //shader. See CalcCloudShadow in default.frag.
        cloud_shadow_map->Create3D(cloud_shadow_resolution,cloud_shadow_resolution,
                                   cloud_shadow_slices,GL_R8,GL_CLAMP_TO_EDGE,GL_LINEAR);
    }
    debug->Info("Cloud shadow map: %ix%ix%i R8, %i KB\n",
                cloud_shadow_resolution,cloud_shadow_resolution,cloud_shadow_slices,
                (cloud_shadow_resolution * cloud_shadow_resolution * cloud_shadow_slices) / 1024);
}

/*
    Point the shadow camera where the sun points, then shrink its orthographic box onto the
    volumes.

    The refit is the whole reason this is not just the sun's own shadow camera. That one is
    fitted to the scene - half-extent 30, near 1, far 200 - and spreading 32 slices over 200
    world units would put six units of depth in every slice while a cloud bank is six units
    thick, so every receiver would land in the same slice and the third axis would buy nothing.
    Fitted to the volumes, the slices land where there is actually cloud.
*/
bool ApplicationShip::FitCloudShadowCamera(void){
    if (!cloud_shadow_camera || !sun){
        return false;
    }
    //Same viewpoint and direction as the sun, so the map's rays ARE the rays that light the
    //scene. Only the frustum differs.
    cloud_shadow_camera->SetPosition(sun->GetPosition());
    cloud_shadow_camera->SetRotation(sun->GetRotation());

    vec3 origin  = cloud_shadow_camera->GetPosition();
    vec3 forward = cloud_shadow_camera->GetForward();
    vec3 up      = cloud_shadow_camera->GetUp();
    vec3 left    = cloud_shadow_camera->GetLeft();

    float half_extent = 0.0f;
    float near_dst = 0.0f;
    float far_dst = 0.0f;
    bool found = false;

    for (Object* v : volumes){
        if (!v || !v->IsVisible()){
            continue;
        }
        fmat4 m = v->GetWorldTransformScaleMatrix();
        for (int c = 0;c < 8;c++){
            //Corners of the unit cube, which is exactly what the shader marches - so this
            //bounds the real volume however it is scaled or rotated, not an axis-aligned guess.
            vec3 corner((c & 1) ? 0.5f : -0.5f,
                        (c & 2) ? 0.5f : -0.5f,
                        (c & 4) ? 0.5f : -0.5f);
            vec3 d = (m * corner) - origin;
            //Measured against the camera's own axes rather than by inverting its matrix, so
            //this does not depend on which handedness lookatmatrix happens to use. The box is
            //symmetric (SetupOrthographic takes one half-size for both sides), so the sign of
            //`left` does not matter either.
            float along = d.dot(forward);
            half_extent = fmaxf(half_extent,fmaxf(fabsf(d.dot(left)),fabsf(d.dot(up))));
            if (!found){
                near_dst = along;
                far_dst  = along;
                found = true;
            }else{
                near_dst = fminf(near_dst,along);
                far_dst  = fmaxf(far_dst,along);
            }
        }
    }
    if (!found || (half_extent <= 0.0f)){
        return false;
    }

    //A margin, so a cloud sitting exactly on the boundary is not clipped by half a texel and
    //the edge falloff has somewhere to land.
    half_extent *= 1.05f;
    near_dst -= 1.0f;
    far_dst  += 1.0f;
    //znear has to stay positive: the depth parameterisation IS distance along the view axis,
    //and an orthographic matrix with znear <= 0 folds. Clamping rather than failing covers the
    //sun ending up inside a volume.
    if (near_dst < 0.1f){
        near_dst = 0.1f;
    }
    if (far_dst <= near_dst + 0.1f){
        far_dst = near_dst + 0.1f;
    }
    cloud_shadow_fit_near = near_dst;
    cloud_shadow_fit_far  = far_dst;

    //Square, so aspect is 1 and `zoom` is the half-extent on both axes. SetupOrthographic
    //recalculates mat_cam itself.
    cloud_shadow_camera->SetupOrthographic((float)cloud_shadow_resolution,
                                           (float)cloud_shadow_resolution,
                                           half_extent,near_dst,far_dst);
    return true;
}

/*
    Build this frame's cloud shadow map and hand it to the renderer.

    Runs from PreRender, so the map is complete before the colour pass samples it. Anything that
    means "no usable map" clears the renderer's texture id instead of leaving the last good one
    bound - Renderer::UploadCloudShadow then tells the shaders to skip the lookup, rather than
    shadowing the scene with a stale frame.
*/
void ApplicationShip::DispatchCloudShadow(void){
    if (!renderer){
        return;
    }
    if (!f_cloud_shadows || !cloud_shadow_shader || !cloud_shadow_map || !volume_noise
        || volumes.empty() || !FitCloudShadowCamera()){
        renderer->cloud_shadow_tex_id = (GLuint)-1;
        return;
    }

    //World-to-local for each volume, which is what the shader marches in. Inverted HERE - once
    //per volume per frame - rather than in the shader, which had to do it once per volume for
    //every column of the shadow map. That was a workaround for fmat4::inverse_transform assuming
    //a rigid transform, which quietly gave a wrong box for anything scaled (these are 20x6x20);
    //it accounts for scale now, see core/type_fmat4.h.
    //
    //The copy into `m` is load-bearing. GetWorldTransformScaleMatrix returns a REFERENCE to the
    //object's cached world matrix and inverse_transform mutates in place, so inverting the
    //returned reference directly would leave the volume itself holding an inverted transform
    //until something moved it again.
    std::vector<fmat4> inverse_transforms;
    for (Object* v : volumes){
        if (v && v->IsVisible()){
            fmat4 m = v->GetWorldTransformScaleMatrix();
            inverse_transforms.push_back(m.inverse_transform());
        }
    }
    if (inverse_transforms.empty()){
        renderer->cloud_shadow_tex_id = (GLuint)-1;
        return;
    }
    glNamedBufferData(cloud_shadow_ssbo,sizeof(fmat4) * inverse_transforms.size(),
                      inverse_transforms.data(),GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,4,cloud_shadow_ssbo);

    cloud_shadow_shader->Use();
    SetSharedDensityUniforms(cloud_shadow_shader);
    cloud_shadow_shader->Setmat4("mat_cloud_shadow",cloud_shadow_camera->mat_cam);
    cloud_shadow_shader->Setint("num_volumes",(int)inverse_transforms.size());
    cloud_shadow_shader->Setint("num_march_steps",cloud_shadow_march_steps);
    //The same extinction the volume shades itself with. It is the same medium: a mismatch shows
    //up as a shadow darker or lighter than the cloud casting it.
    cloud_shadow_shader->Setfloat("light_absorption",volume_light_absorption);
    cloud_shadow_shader->Setfloat("shadow_strength",cloud_shadow_strength);

    glBindTextureUnit(TEXUNIT_APP_RESERVED,volume_noise->texture_id);
    //layered=GL_TRUE for a 3D image: one invocation writes down the whole depth of it.
    glBindImageTexture(0,cloud_shadow_map->texture_id,0,GL_TRUE,0,GL_WRITE_ONLY,GL_R8);

    int groups = (cloud_shadow_resolution + 7) / 8;
    glDispatchCompute(groups,groups,1);
    //default.frag samples this as a texture later in the frame, not as an image.
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

    renderer->cloud_shadow_tex_id = cloud_shadow_map->texture_id;
    renderer->mat_cloud_shadow = cloud_shadow_camera->mat_cam;
}

void ApplicationShip::PreRender(void){
    /*
        Drift the noise through the box.

        Integrated here rather than in SetVolumeUniforms, which is where it used to live: that
        callback fires during the colour pass, which is AFTER the cloud shadow map has been
        built, so the shadow would march last frame's cloud and sit permanently one frame behind
        the thing casting it. PreRender is the one place that runs exactly once per frame before
        anything reads the offset.

        Still not RunLogic: the drift is purely visual and deliberately outside the tick.
    */
    float dt = 1.0f / 60.0f;
    if (tmr_render_loop && tmr_render_loop->delta > 0){
        //delta is microseconds. Clamped so a hitch or a breakpoint does not teleport the cloud.
        dt = fminf((float)(tmr_render_loop->delta / 1000000.0),0.1f);
    }
    volume_noise_offset += volume_wind * dt;

    DispatchCloudShadow();
}

void ApplicationShip::ReloadVolumeShader(void){
    Shader* reloaded = new Shader("shaders/default.vert","shaders/raymarch_volume.frag");
    reloaded->uniform_callback = std::bind(&ApplicationShip::SetVolumeUniforms,this);
    //Only swap once the new one is known good. In practice a bad shader never gets this far -
    //Shader's compile path calls debug->Fatal - but this keeps the old program bound if that
    //ever becomes a soft failure.
    if (reloaded->progid != -1){
        Shader* previous = volume_shader;
        volume_shader = reloaded;
        //Replace it at the index the volume's mesh already points at, rather than registering
        //another one - AddCustomShader would append, and the mesh would keep drawing with the
        //stale shader while the new one drew nothing.
        renderer->custom_shaders.at(volume_shader_index) = volume_shader;
        delete previous;
        debug->Info("Reloaded shaders/raymarch_volume.frag (program %i)\n",volume_shader->progid);
    }else{
        debug->Err("Failed to reload shaders/raymarch_volume.frag, keeping the old program\n");
        delete reloaded;
    }
}

void ApplicationShip::Init(void){
    //Create a renderer with initial size
    int2 dimensions = GetDisplaySettings();
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init(PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }
    renderer->skinned_shader = new Shader("shaders/default_skinned.vert","shaders/default.frag");
    renderer->alpha_clip = 0.5f;
    renderer->f_render_skybox = false;

    SetPhysicsTPS(50.0f);

    //Randomise the randomiser
    rrand = new RRandom();
    debug->Info("Polulating RRandom\n");
    rrand->Generate(512,512);

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    main_scene = CreateEmptyScene();
    main_scene->UpdatePhysics(GetPhysicsTimestep());

    assetmanager = new AssetManager();
    gltfloader.LoadGLTFFile("data/ships.glb");
    GetAllAssetsFromGLTF();

    //Let's load in the ship
    ship_character = new ShipCharacter(assetmanager,main_scene->physics_world,main_scene,rrand);
    if (ship_character){
        main_scene->AddObject(ship_character);
        //Give the exhaust some particles
        Particle* exhaust_particle = new Particle(main_scene->physics_world);
        assetmanager->GetObjectFromAsset("smoke_particle",exhaust_particle);
        ship_character->exhaust_emitter->emission_properties = {
            .emission_rate         = 10.0f,
            .emission_direction    = vec3(0,0,-1), //Relative to ship
            .emission_spread       = 20.0f,
            .particle_size_min     = 0.05f,
            .particle_size_max     = 0.25f,
            .particle_lifetime_min = 0.2f,
            .particle_lifetime_max = 0.5f,
        };
        ship_character->exhaust_emitter->AddParticleType(exhaust_particle);

        //Setup laser emitter
        Particle* laser_particle = new Particle(main_scene->physics_world);

        assetmanager->GetObjectFromAsset("laser_particle",laser_particle);
        vec3 sz = laser_particle->GetMesh()->GetExtents();
        laser_particle->GetPhysics()->AddBoxCollider(sz/2,vec3(0,0,0),quat().identity(),0.1f);
        laser_particle->SetCollideWithMaskBits(COLLISION_CATEGORY_ASTEROID|COLLISION_CATEGORY_DOOR);
        laser_particle->SetCollisionCategoryBits(COLLISION_CATEGORY_LASER);
        laser_particle->GetPhysics()->SetActive(false); //We don't want it to interfere until fired.
        laser_particle->name = "laser_particle";
        ship_character->laser_emitter->emission_properties = {
            .emission_rate         = 10.0f,
            .emission_direction    = vec3(0,0,1), //Relative to ship
            .emission_spread       = 20.0f,
            .particle_size_min     = 1.0f,
            .particle_size_max     = 1.0f,
            .particle_lifetime_min = 0.5f,
            .particle_lifetime_max = 0.5f,
            .emission_speed_min    = 10.0f,
            .emission_speed_max    = 12.0f,
        };
        ship_character->laser_emitter->AddParticleType(laser_particle);

        //The ships laser light should be added to the scene for rendering
        main_scene->AddObject(ship_character->laser_light);
    }

    //We add a grid of cells to have a sense of speed and movement
    Object* grid = new Object();
    grid->name = "Grid of Cells";
    for (int x = -10; x <= 10; x++){
        for (int z = -10; z <= 10; z++){
            Object* cell = assetmanager->GetObjectFromAsset("cube");
            cell->name = "Grid Cell";
            grid->AttachChild(cell);
            if (cell){
                cell->SetPosition(vec3(x * 2,-2,z * 2));

            }
        }
    }
    main_scene->AddObject(grid);

    //A door panel to bump into, shoot at, and watch the asteroids swing around.
    AddDoor(vec3(4,0,-5),0.0f);

    volume_shader = new Shader("shaders/default.vert","shaders/raymarch_volume.frag");
    volume_shader->uniform_callback = std::bind(&ApplicationShip::SetVolumeUniforms,this);
    volume_shader_index = renderer->AddCustomShader(volume_shader);
    BuildVolumeNoise();

    //Two cloud banks on opposite sides of the grid, off to the sides rather than centred: at the
    //default zoom the camera looks down through anything above the ship, so a volume at the
    //origin turns the whole app into permanent haze. These are banks to fly into instead.
    //
    //Still ONE draw call between them - see AddVolume. They share the shader's shape and march
    //settings too, since those are uniforms; only the transform is per volume, which is what
    //makes the second one a different size without any extra plumbing.
    AddVolume("Raymarch Volume",vec3(0,3,-18),vec3(20,6,20));
    AddVolume("Raymarch Volume Small",vec3(0,4,17),vec3(10,4,10));

    //Cloud shadows. Built after the volumes exist because the shadow camera is fitted to them,
    //and after BuildVolumeNoise because the compute shader samples that same noise.
    BuildCloudShadowResources();

    //A handler for dropping files onto the window
    main_window->SetOnFileDropped([this](std::string filename){
        debug->Info("File callback received with file %s\n",filename.c_str());
        //Create a modal window in the UI:
        f_filemodal = true;
        filemodal_filename = filename;
    });

    main_window->Resize(1680,900);

    gamepad_controller = main_window->inputcontroller;
    gamepad_controller->ListDevices();
    gamepad_controller->AddGamePadMap(0,GAMEPAD_LEFT_STICK_X);
    gamepad_controller->AddGamePadMap(1,GAMEPAD_LEFT_STICK_Y);
    gamepad_controller->AddGamePadMap(2,GAMEPAD_RIGHT_STICK_X);
    gamepad_controller->AddGamePadMap(3,GAMEPAD_RIGHT_STICK_Y);
    gamepad_controller->AddGamePadMap(4,GAMEPAD_R2L2);

    RegisterCommandHandlers();
    RegisterMCPTools();

    BinaryAsset::DumpBinaryAssets();
    assetmanager->ListAssets();
}

//Everything a door did since it was last asked, plus what the last knock on it was - which is
//the whole point of the tool: a knock's direction is the thing that is easy to get wrong.
json ApplicationShip::GetDoorTelemetry(HingedDoor* door){
    if (!door){
        return json{ {"error","no such door"} };
    }
    vec3 angular_velocity = door->GetPhysics() ? door->GetPhysics()->GetAngularVelocity() : vec3();
    vec3 p = door->GetPosition();
    vec3 kd = door->last_knock_direction;
    vec3 kp = door->last_knock_point;
    return json{
        {"angle_degrees",door->GetAngle() * 180.0f / TYPE_PI},
        {"swing_rate",angular_velocity.y},
        {"position",json::array({p.x,p.y,p.z})},
        {"knock_count",door->knock_count},
        {"last_knock_direction",json::array({kd.x,kd.y,kd.z})},
        {"last_knock_point",json::array({kp.x,kp.y,kp.z})},
        {"spring_stiffness",door->spring_stiffness},
        {"knock_force",door->knock_force},
    };
}

//Walks the scene rather than the `asteroids` vector, which holds stale pointers by design - see
//the note on that member.
//The running collected totals, by kind name. See ApplicationShip::collected_totals.
json ApplicationShip::GetCollectedTotals(){
    return json{
        {"energy",collected_totals[PICKUP_KIND_ENERGY]},
        {"ammo",collected_totals[PICKUP_KIND_AMMO]},
        {"health",collected_totals[PICKUP_KIND_HEALTH]},
    };
}

json ApplicationShip::GetAsteroidTelemetry(){
    json out = json::array();
    for (Object* object:renderer->objects){
        Asteroid* asteroid = dynamic_cast<Asteroid*>(object);
        if (!asteroid){
            continue;
        }
        vec3 p = asteroid->GetPosition();
        out.push_back(json{
            {"id",asteroid->GetID()},
            {"health",asteroid->health},
            {"position",json::array({p.x,p.y,p.z})},
        });
    }
    return out;
}

SimCommand ApplicationShip::MakeSpawnAsteroidCommand(const vec3& position, float scale,
                                                     const vec3& velocity, const vec3& angular_velocity) const{
    SimCommand cmd;
    cmd.type = SHIP_CMD_SPAWN_ASTEROID;
    cmd.flags = SIM_CMD_FLAG_POSITION | SIM_CMD_FLAG_SCALE | SIM_CMD_FLAG_VELOCITY | SIM_CMD_FLAG_ANGULAR_VELOCITY;
    cmd.position = position;
    cmd.scale = vec3(scale);
    cmd.velocity = velocity;
    cmd.angular_velocity = angular_velocity;
    return cmd;
}

SimCommand ApplicationShip::MakeSpawnDoorCommand(const vec3& hinge_position, float yaw) const{
    SimCommand cmd;
    cmd.type = SHIP_CMD_SPAWN_DOOR;
    cmd.flags = SIM_CMD_FLAG_POSITION | SIM_CMD_FLAG_ROTATION;
    cmd.position = hinge_position;
    cmd.rotation = quat(vec3(0,1,0),yaw);
    return cmd;
}

SimCommand ApplicationShip::MakeSpawnPickupCommand(const vec3& position, float scale, const vec3& angular_velocity,
                                                   PickupKind kind, float amount) const{
    SimCommand cmd;
    cmd.type = SHIP_CMD_SPAWN_PICKUP;
    cmd.flags = SIM_CMD_FLAG_POSITION | SIM_CMD_FLAG_SCALE | SIM_CMD_FLAG_ANGULAR_VELOCITY;
    cmd.position = position;
    cmd.scale = vec3(scale);
    cmd.angular_velocity = angular_velocity;
    cmd.subtype = (uint32_t)kind;
    cmd.value[0] = amount;
    return cmd;
}

void ApplicationShip::RegisterCommandHandlers(){
    if (!main_scene){
        debug->Err("No scene to register ship command handlers on\n");
        return;
    }
    //Runs on the physics thread, at the top of the tick, before the step, with physics_mutex
    //already held - so creating a rigid body and adding it to the scene is safe here, and this is
    //the only place either is done for an asteroid that something OUTSIDE the simulation asked
    //for. Note Scene::DrainCommands runs well before the per-object UpdatePhysicsState loop, so
    //the AddObject here is not mutating a vector that is being iterated.
    main_scene->RegisterCommandHandler(SHIP_CMD_SPAWN_ASTEROID,
        [this](const SimCommand& cmd) -> objectid_t {
            Asteroid* asteroid = new Asteroid(assetmanager,main_scene->physics_world,main_scene,rrand);
            if (!asteroid || !asteroid->GetPhysics()){
                debug->Err("spawn_asteroid: could not build an asteroid\n");
                return OBJECTID_INVALID;
            }
            if (cmd.flags & SIM_CMD_FLAG_POSITION){
                asteroid->SetPosition(cmd.position);
            }
            //Before the velocities: SetScale rescales the collider and recomputes the inertia.
            if (cmd.flags & SIM_CMD_FLAG_SCALE){
                asteroid->SetScale(cmd.scale);
            }
            if (cmd.flags & SIM_CMD_FLAG_ANGULAR_VELOCITY){
                asteroid->GetPhysics()->SetAngularVelocity(cmd.angular_velocity);
            }
            if (cmd.flags & SIM_CMD_FLAG_VELOCITY){
                asteroid->GetPhysics()->SetVelocity(cmd.velocity);
            }
            asteroid->UpdatePhysicsState();
            main_scene->AddObject(asteroid);
            return asteroid->GetID();
        });

    //Same thread and timing guarantees as the asteroid handler above, which is what makes it safe
    //to create the panel, the post and the joint that ties them together from here.
    main_scene->RegisterCommandHandler(SHIP_CMD_SPAWN_DOOR,
        [this](const SimCommand& cmd) -> objectid_t {
            //The command's rotation is a turn about world up, so the yaw comes straight back out
            //of the quaternion's y/w pair - the same extraction HingedDoor::GetAngle uses, rather
            //than quat::get_yaw, so this cannot disagree with the angle the door reports.
            float yaw = 2.0f * atan2f(cmd.rotation.y,cmd.rotation.w);
            HingedDoor* door = AddDoor(cmd.position,yaw);
            return door ? door->GetID() : OBJECTID_INVALID;
        });

    main_scene->RegisterCommandHandler(SHIP_CMD_SPAWN_PICKUP,
        [this](const SimCommand& cmd) -> objectid_t {
            PickupKind kind = (cmd.subtype <= PICKUP_KIND_HEALTH) ? (PickupKind)cmd.subtype : PICKUP_KIND_NONE;
            Pickup* pickup = new Pickup(assetmanager,main_scene->physics_world,main_scene,kind,cmd.value[0]);
            if (!pickup || !pickup->GetPhysics()){
                debug->Err("spawn_pickup: could not build a pickup\n");
                return OBJECTID_INVALID;
            }
            if (cmd.flags & SIM_CMD_FLAG_POSITION){
                pickup->SetPosition(cmd.position);
            }
            if (cmd.flags & SIM_CMD_FLAG_SCALE){
                pickup->SetScale(cmd.scale);
            }
            if (cmd.flags & SIM_CMD_FLAG_ANGULAR_VELOCITY){
                pickup->GetPhysics()->SetAngularVelocity(cmd.angular_velocity);
            }
            pickup->UpdatePhysicsState();
            main_scene->AddObject(pickup);
            return pickup->GetID();
        });
}

//Called from within the physics step, exactly like onContact - so this only RECORDS which pickups
//were flown into and RunLogic is what banks and removes them. Object::Destroy would in fact be
//safe here (it only sets a flag), but the moment a pickup actually adds to something on the ship
//that will not be, and one rule for both callbacks is worth more than the shortcut.
void ApplicationShip::onTrigger(const rp3d::OverlapCallback::CallbackData& callbackData){
    for (uint32_t i = 0; i < callbackData.getNbOverlappingPairs(); i++){
        rp3d::OverlapCallback::OverlapPair pair = callbackData.getOverlappingPair(i);
        //OverlapStart only. A pickup is collected the instant it is touched, and the ship stays
        //inside the trigger volume for several ticks afterwards - every one of which would be
        //reported again as OverlapStay. Same lesson as the laser's ContactStart, see onContact.
        if (pair.getEventType() != rp3d::OverlapCallback::OverlapPair::EventType::OverlapStart){
            continue;
        }
        Object* d1 = (Object*)pair.getBody1()->getUserData();
        Object* d2 = (Object*)pair.getBody2()->getUserData();
        if (!d1 || !d2){
            continue;
        }
        //Either order - rp3d does not promise which body of the pair is which.
        Pickup* pickup = dynamic_cast<Pickup*>(d1);
        Object* other = d2;
        if (!pickup){
            pickup = dynamic_cast<Pickup*>(d2);
            other = d1;
        }
        if (!pickup || pickup->f_collected){
            continue;
        }
        //The mask pair already restricts this to the ship, so this cast is a belt-and-braces
        //check rather than the thing doing the filtering.
        if (!dynamic_cast<ShipCharacter*>(other)){
            continue;
        }
        //Claimed here rather than in RunLogic, so a second overlap in the same tick (or the
        //OverlapStart of the ship's other collider - it has two) cannot bank it twice.
        pickup->f_collected = true;
        pending_collected_pickups.push_back(pickup);
    }
}

void ApplicationShip::RegisterMCPTools(){
    MCPServer::Get()->RegisterTool("asteroid_spawn",
        "Add asteroids to the scene, the same way the 'Add Asteroid' button in the Ship Settings "
        "window does - both go through the simulation's command queue, so the spawn lands on the "
        "physics thread at the top of a tick. Default position is a little way in front of the "
        "ship, and default velocity is zero: a drifting asteroid wanders out of a scripted line of "
        "fire. Blocks until each spawn has been applied, then returns the telemetry of every "
        "asteroid in the scene, including the new ones and their health.",
        json{
            {"type","object"},
            {"properties", {
                {"position", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] world position, default 6 units ahead of the ship"}}},
                {"count", {{"type","number"},{"description","how many to add, default 1, capped at 20. More than one are spaced 2 units apart along X, deliberately not scattered - see the handler"}}},
                {"scale", {{"type","number"},{"description","uniform scale, default 1. The collider is a 0.4-radius sphere scaled with it"}}},
                {"velocity", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] initial velocity, default none"}}},
                {"angular_velocity", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] initial spin in rad/s, default none"}}}
            }}
        },
        [this](const json &args) -> json {
            if (!ship_character || !main_scene){
                return json{ {"error","scene not ready"} };
            }
            uint32_t count = (uint32_t)clamp(args.value("count",1.0f),1.0f,20.0f);
            float scale = clamp(args.value("scale",1.0f),0.1f,10.0f);
            vec3 position = ship_character->GetPosition() + vec3(0,0,-6);
            vec3 velocity = {};
            vec3 angular_velocity = {};
            auto read_vec3 = [&args](const char* key, vec3& out){
                if (args.contains(key) && args[key].is_array() && args[key].size() == 3){
                    out = vec3(args[key][0],args[key][1],args[key][2]);
                }
            };
            read_vec3("position",position);
            read_vec3("velocity",velocity);
            read_vec3("angular_velocity",angular_velocity);
            json spawned = json::array();
            for (uint32_t i = 0;i < count;i++){
                //Spaced, not scattered. The obvious thing is a random offset per asteroid, but
                //rrand is the SIMULATION's generator: drawing from it here would be an
                //unsynchronised read from the MCP thread AND would shift every draw the physics
                //thread makes afterwards (the Asteroid constructor picks its model from it), so a
                //scripted run would stop being reproducible. A caller who wants scatter passes
                //explicit positions, one call each.
                vec3 offset = vec3(2.0f * (float)i,0,0);
                //Not SubmitUICommand: an MCP handler is on the MCP thread, holds no lock, and has
                //to report what it made - so it can wait for the physics thread to apply this and
                //hand back the id. The debug UI cannot (it holds physics_mutex) - see
                //Application::SubmitCommandAndWait.
                objectid_t id = SubmitCommandAndWait(MakeSpawnAsteroidCommand(position + offset,scale,velocity,angular_velocity));
                if (id == OBJECTID_INVALID){
                    return json{ {"error","spawn command was not applied"}, {"spawned",spawned}, {"asteroids",GetAsteroidTelemetry()} };
                }
                spawned.push_back(id);
            }
            return json{ {"spawned",spawned}, {"asteroids",GetAsteroidTelemetry()} };
        });

    MCPServer::Get()->RegisterTool("ship_shoot",
        "Fire the ship's laser. RunLogic emits one laser particle per physics tick that the shoot "
        "input is held, so `shots` is simply how many ticks to hold it - 1 is a single laser. "
        "Blocks until the shots have been fired and the given settle_ms has passed, then returns "
        "the telemetry of every door in the scene, so a single call is enough to see what the shot "
        "did to one. Aim the ship with object_set_transform first.",
        json{
            {"type","object"},
            {"properties", {
                {"shots", {{"type","number"},{"description","physics ticks to hold the trigger, i.e. laser particles fired. Default 1, capped at 60"}}},
                {"settle_ms", {{"type","number"},{"description","how long to keep waiting after the last shot so the hit has landed, default 500, capped at 10000"}}}
            }}
        },
        [this](const json &args) -> json {
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input){
                return json{ {"error","no input controller"} };
            }
            uint32_t shots = (uint32_t)clamp(args.value("shots",1.0f),1.0f,60.0f);
            float settle_ms = clamp(args.value("settle_ms",500.0f),0.0f,10000.0f);
            //Same "MCP is a player" route the tank tools take: hold the mapped input for a number
            //of ticks and let RunLogic do exactly what it does for a person holding the key. Note
            //RunLogic gates shooting on the window having focus, so this needs the app focused.
            input->HoldKey(INPUT_SHOOT,shots);
            if (!main_scene->IsPhysicsPaused()){
                Sleep((DWORD)(shots * GetPhysicsTimestep() * 1000.0f) + (DWORD)settle_ms);
            }
            json out = json::array();
            for (HingedDoor* door:doors){
                out.push_back(GetDoorTelemetry(door));
            }
            return json{ {"doors",out}, {"asteroids",GetAsteroidTelemetry()}, {"window_focused",main_window->f_has_focus} };
        });

    MCPServer::Get()->RegisterTool("pickup_spawn",
        "Float a pickup capsule in the scene for the ship to fly through, the same way the "
        "'Add Pickup' button does - through the simulation's command queue. A pickup's collider is "
        "a trigger, so the ship passes through it and collects it instead of bouncing off. Blocks "
        "until the spawn has been applied and returns the new object's id plus what has been "
        "collected so far.",
        json{
            {"type","object"},
            {"properties", {
                {"position", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] world position, default 6 units ahead of the ship"}}},
                {"kind", {{"type","string"},{"enum", json::array({"energy","ammo","health"})},{"description","what it is worth, default energy. Nothing consumes this yet - see Pickup.h"}}},
                {"amount", {{"type","number"},{"description","how much of that it is worth, default 25"}}},
                {"scale", {{"type","number"},{"description","uniform scale, default 2"}}},
                {"spin", {{"type","number"},{"description","spin about world up in rad/s, default 1"}}}
            }}
        },
        [this](const json &args) -> json {
            if (!ship_character || !main_scene){
                return json{ {"error","scene not ready"} };
            }
            vec3 position = ship_character->GetPosition() + vec3(0,0,-6);
            if (args.contains("position") && args["position"].is_array() && args["position"].size() == 3){
                position = vec3(args["position"][0],args["position"][1],args["position"][2]);
            }
            std::string kind_name = args.value("kind",std::string("energy"));
            PickupKind kind = PICKUP_KIND_ENERGY;
            if (kind_name == "ammo"){
                kind = PICKUP_KIND_AMMO;
            }else if (kind_name == "health"){
                kind = PICKUP_KIND_HEALTH;
            }
            float amount = args.value("amount",25.0f);
            float scale = clamp(args.value("scale",2.0f),0.1f,10.0f);
            vec3 spin = vec3(0,args.value("spin",1.0f),0);
            objectid_t id = SubmitCommandAndWait(MakeSpawnPickupCommand(position,scale,spin,kind,amount));
            if (id == OBJECTID_INVALID){
                return json{ {"error","spawn command was not applied"} };
            }
            return json{ {"spawned",id}, {"collected",GetCollectedTotals()} };
        });

    MCPServer::Get()->RegisterTool("pickup_status",
        "What pickups are floating in the scene, and the running totals of what the ship has "
        "collected. Those totals are the placeholder for the energy / ammo / health the ship does "
        "not have yet - nothing else reads them.",
        json{ {"type","object"},{"properties",json::object()} },
        [this](const json &args) -> json {
            json out = json::array();
            for (Object* object:renderer->objects){
                Pickup* pickup = dynamic_cast<Pickup*>(object);
                if (!pickup){
                    continue;
                }
                vec3 p = pickup->GetPosition();
                out.push_back(json{
                    {"id",pickup->GetID()},
                    {"kind",pickup->KindName()},
                    {"amount",pickup->amount},
                    {"collected",pickup->f_collected},
                    {"is_trigger",pickup->GetPhysics() ? pickup->GetPhysics()->IsTrigger() : false},
                    {"position",json::array({p.x,p.y,p.z})},
                });
            }
            return json{ {"pickups",out}, {"collected",GetCollectedTotals()} };
        });

    MCPServer::Get()->RegisterTool("door_telemetry",
        "Swing angle, swing rate and last-knock details for every hinged door panel in the scene.",
        json{ {"type","object"},{"properties",json::object()} },
        [this](const json &args) -> json {
            json out = json::array();
            for (HingedDoor* door:doors){
                out.push_back(GetDoorTelemetry(door));
            }
            return json{ {"doors",out} };
        });
}

//Called before update physics after update animations
void ApplicationShip::RunLogic(){
    //Does anything need to be created / destroyed before we run any logic on it?
    //An asteroid that took its killing shot becomes an explosion HERE, not in the contact callback
    //that spotted the hit. onContact runs from inside rp_world->update(), and constructing an
    //AsteroidExplosion creates three rigid bodies - its own (AddPhysics), its fragment emitter's,
    //and its particle template's - so building one there adds bodies to the world while the world
    //is part-way through iterating its own component arrays. RunLogic runs on the same physics
    //thread but sits between ticks, which makes it the earliest safe point. This used to stage an
    //already-constructed explosion, which deferred the AddObject but not the part that mattered.
    //
    //Deliberately NOT a SimCommand, even though the UI and the MCP tools spawn things that way. A
    //command is for a mutation arriving from OUTSIDE the simulation: another thread's, and - for
    //the record/replay plan - a tick-stamped note of external intent. An explosion is a
    //consequence of the simulation's own state, so a replay reproduces it by reproducing the
    //collision that caused it; recorded as a command it would land a second time on top of that.
    for (Asteroid* asteroid:pending_asteroid_explosions){
        //Nothing else destroys a claimed asteroid (health -1 marks it as spoken for) and
        //DeleteDestroyedObjects has not run yet this tick, so this pointer is still live.
        if (!asteroid || asteroid->IsDestroyed()){
            continue;
        }
        AsteroidExplosion* explosion = new AsteroidExplosion(assetmanager,main_scene->physics_world,main_scene,rrand);
        explosion->target_asteroid = asteroid;
        main_scene->AddObject(explosion);
        explosion->StartExplosion();
        active_asteroid_explosions.push_back(explosion);
    }
    pending_asteroid_explosions.clear();

    //Pickups the ship flew into last tick. "Instantly vanish" in the sense that matters - the
    //trigger already stopped it colliding, and Destroy() here takes it out of the scene on this
    //same tick's DeleteDestroyedObjects below, one tick after the touch.
    for (Pickup* pickup:pending_collected_pickups){
        if (!pickup){
            continue;
        }
        //Where the pickup's actual effect will go once the ship HAS an energy/ammo/health to add
        //it to. Until then this total is the only evidence it was collected - see Pickup.h.
        if (pickup->kind < 4){
            collected_totals[pickup->kind] += pickup->amount;
        }
        debug->Ok("Collected %s pickup worth %.0f (total %.0f)\n",pickup->KindName(),pickup->amount,collected_totals[pickup->kind]);
        pickup->Destroy();
    }
    pending_collected_pickups.clear();

    if (selected_object && selected_object->IsDestroyed()){
        selected_object = NULL;
    }

    renderer->DeleteDestroyedObjects();


    //Shortcuts
    Camera* camera = main_scene->camera;
    InputController* input = main_scene->inputcontroller;

    if (selected_object){
        if (f_mode_grab){
            //We use the camera left/right up down to move the character.
            int dx = input->GetDelta(INPUT_MOUSE_X);
            int dy = input->GetDelta(INPUT_MOUSE_Y);

            vec3 vdx = camera->GetLeft() * dx * 0.01;
            vec3 vdy = camera->GetUp() * -dy * 0.01;
            selected_object->MoveBy(vdx + vdy);

            if (input->WasKeyReleased(INPUT_CLICK_LEFT)){
                f_mode_grab = false;
            }
        }
    }

    //Camera. Two independent switches - which way it looks, and whether it follows the ship - so
    //all four combinations mean something:
    //
    //  Overhead + Track     the default: hangs straight over the ship and goes where it goes.
    //  Overhead + no Track  the same top-down view over a fixed point, panned around the map
    //                       with shift+middle. The ship flies out of frame and stays out.
    //  Free + Track         chase cam: the pivot rides the ship and the camera is carried along
    //                       with it, at whatever angle and distance the mouse was left at.
    //  Free + no Track      a camera that stays exactly where it is put.
    //
    //Both branches work on camera_target, the pivot the middle-mouse orbit/pan and the wheel
    //further down all act on - so tracking is only ever a question of what moves that point.
    if (f_camera_overhead){
        //Overhead owns both the camera's rotation and its height; the mouse only gets to move
        //the pivot (the shift+middle pan below). Tracking decides whether that pivot is the
        //ship or wherever it was last panned to.
        float local_zoom_target = zoom_target;
        if (f_mode_camera_track && ship_character){
            camera_target = ship_character->GetPosition();
            //We use a slight zoom depending on speed. Only while tracking: with the pivot
            //standing still there is nothing for the extra height to keep ahead of.
            vec3 vel = ship_character->GetVelocity();
            float fact = vel.length();
            fact = clamp(fact,0.0f,5.0f);
            local_zoom_target += fact;
        }

        vec3 p = camera->GetPosition();
        vec3 camera_goal = camera_target + vec3(0,local_zoom_target,0);
        vec3 diff = p.lerp(camera_goal,0.04f);
        camera->SetPosition(diff);

        //Ease back to looking straight down. A no-op for as long as the view has been overhead,
        //since nothing else touches the rotation then - but free rotation leaves the camera at
        //whatever angle it was orbited to, and this is what swings the overhead view back when
        //the mode is switched, rather than snapping it.
        quat r = camera->GetRotation();
        camera->SetRotation(quat::slerp(r,overhead_rotation,0.05f));
    }else if (f_mode_camera_track && ship_character){
        //Free rotation with tracking on. Carry the camera by the same delta the pivot moves
        //rather than re-aiming it: the angle and distance the mouse set are left exactly as they
        //were, and the ship stays put in the middle of the view. This is what
        //ApplicationTank::SnapCameraToControlledVehicle does, minus its chase-behind-the-heading
        //blend - a ship that rolls and yaws as freely as this one would have the view constantly
        //swinging around after it.
        vec3 ship_pos = ship_character->GetPosition();
        vec3 delta = ship_pos - camera_target;
        camera->SetPosition(camera->GetPosition() + delta);
        camera_target = ship_pos;
    }

    if (f_lock_ship_axis && ship_character){
        vec3 angular_vel = ship_character->GetPhysics()->GetAngularVelocity();
        if (angular_vel.length() > 0.01f){
            //Dampen the angular velocity
            ship_character->GetPhysics()->SetAngularVelocity(angular_vel * 0.95f);
        }


        //We attempt to keep the ship upright
        quat ship_rot = ship_character->GetRotation();

        vec3 fwd = ship_character->GetForward();
        //We want the forward vector to have no y component
        vec3 corrected_fwd = vec3(fwd.x,0,fwd.z).normalize();

        quat q1 = quat::getquat(corrected_fwd,vec3(),vec3(0,1,0));

        quat q2 = quat::slerp(ship_rot,q1.normalize(),0.05f);

        ship_character->SetRotation(q2);

        //We also attempt to keep the ship at y=0
        vec3 ship_pos = ship_character->GetPosition();
        if (ship_pos.y < -0.1f || ship_pos.y > 0.1f){
            vec3 v = ship_character->GetVelocity();
            ship_character->SetVelocity(v - vec3(0,ship_pos.y * 0.1f,0));
        }
    }



    CheckObjectSelection();

    //We keep the asteroids in a certain range around the ship
    //Get all asteroids in the scene
    for (Object* object:renderer->objects){
        Asteroid* asteroid = dynamic_cast<Asteroid*>(object);
        if (asteroid){
            vec3 center_pos = vec3();
            vec3 asteroid_pos = asteroid->GetPosition();
            vec3 diff = asteroid_pos - center_pos;
            float dist = diff.length();
            if (dist > 20.0f){
                asteroid_pos.y = 0;
                //asteroid->SetPosition(asteroid_pos);
                //We modify its velocity to head towards the ship
                vec3 dir_to_ship = (center_pos - asteroid_pos).normalize();
                float speed = asteroid->GetPhysics()->GetVelocity().length();
                speed = clamp(speed,1.0f,5.0f);
                asteroid->GetPhysics()->SetVelocity(dir_to_ship * speed);
            }

            bool update_pos = false;
            if (asteroid_pos.y > 0.1f){
                asteroid_pos.y = 0.1f;
                update_pos = true;
            }
            if (asteroid_pos.y < -0.1f){
                asteroid_pos.y = -0.1f;
                update_pos = true;
            }
            if (update_pos){
                vec3 vel = asteroid->GetVelocity();
                asteroid->SetPosition(asteroid_pos);
                asteroid->SetVelocity(vel);
            }

        }
    }


    //Mouse camera: middle mouse orbits around camera_target, shift+middle pans both camera and
    //pivot. Same scheme, same sensitivities as ApplicationTank - see the block at the end of its
    //RunLogic.
    //
    //Read with GetDelta, and read EVERY tick whether or not the drag is active. Both halves
    //matter and getting either wrong makes the camera spin out, exactly as it did in the tank:
    //GetValue on a relative axis returns a running total that is never reset (so the camera would
    //rotate by every mouse count since process start, once per frame), and InputController only
    //clears the delta of a map that was actually read this tick - so a read behind the button
    //gate lets movement pile up for the whole time the button is NOT held, and the first frame of
    //a drag applies all of it at once. Draining it here keeps a drag starting from rest.
    int cam_dx = input->GetDelta(INPUT_MOUSE_DELTA_X);
    int cam_dy = input->GetDelta(INPUT_MOUSE_DELTA_Y);
    if (main_window->f_has_focus && input->IsKeyDown(INPUT_CLICK_MIDDLE)){
        if (input->IsKeyDown(INPUT_SHIFT)){
            //Move the camera, carrying the pivot with it so the viewing angle is left alone.
            //Overhead included - there this is what slides the top-down view across the map,
            //since the camera's up axis lies flat when it is looking straight down. With
            //tracking on the pan is simply undone next tick, when the pivot goes back on the
            //ship.
            vec3 d = camera->MoveSidewaysBy(-cam_dx/100.0f);
            d += camera->MoveUpBy(cam_dy/100.0f);
            camera_target += d;
        }else if (!f_camera_overhead){
            //Orbiting is free rotation only: overhead owns the camera's rotation, and a drag
            //there would just be eased back out by the slerp above.
            //
            //Up/down rotates the camera position around the camera's own left axis.
            vec3 p = camera->GetPosition() - camera_target;
            vec3 axis = camera->GetLeft();
            quat q(axis,-cam_dy/50.0f);
            p = q * p;
            camera->SetPosition(p+camera_target);

            //Re-aim at the pivot keeping the current up, which allows a full 360 over the top.
            vec3 up = camera->GetUp();
            camera->SetLookAt(camera_target,&up);

            //Left/right rotates around the world Y axis, lookat included.
            p = camera->GetPosition()-camera_target;
            axis = vec3(0,1,0);
            q.set_rotation(axis,-cam_dx/50.0f);
            p = q * p;
            camera->SetPosition(p+camera_target);
            camera->RotateBy(q);
        }
    }

    //Mouse wheel for zoom, focused only - otherwise it tracks a wheel being used in another
    //application. InputController drops the delta while unfocused as well.
    static float mouse_delta_sum = 0;
    if (main_window->f_has_focus){
        if (mouse_delta_sum != 0){
            if (f_camera_overhead){
                //Overhead: the wheel sets how high above the pivot the camera rides.
                zoom_target -= mouse_delta_sum * 0.5f;
                if (zoom_target < 5.0f) zoom_target = 5.0f;
                if (zoom_target > 80.0f) zoom_target = 80.0f;
                mouse_delta_sum = 0;
            }else{
                //Free rotation: dolly along the view direction by a fraction of the distance to the
                //pivot, so the step shrinks as it closes in. The tank measures that distance from
                //GetForward() rather than GetPosition(), which makes its zoom speed depend on how
                //far the pivot is from the world origin; this uses the camera's actual distance.
                vec3 diff = camera->GetPosition() - camera_target;
                float dist = diff.length() * mouse_delta_sum;
                camera->MoveForwardBy(dist / 50.0f);
                mouse_delta_sum /= 1.1f;
            }
        }
        mouse_delta_sum += input->GetDelta(INPUT_MOUSE_WHEEL);
    }


    //Character input with gamepad. The focus check keeps a background window from flying the ship
    //on input meant for another application - but a scripted hold (an MCP tool, later a replay)
    //does not come from the OS, and an unfocused window is exactly when those run, so it has to
    //be let through. See InputController's note on SyntheticHold.
    if (ship_character && (main_window->f_has_focus || input->HasSyntheticHolds())){
        float gp_lx = gamepad_controller->GetNormalizedAnalogValue(GAMEPAD_LEFT_STICK_X);
        float gp_ly = gamepad_controller->GetNormalizedAnalogValue(GAMEPAD_LEFT_STICK_Y);
        float gp_rx = gamepad_controller->GetNormalizedAnalogValue(GAMEPAD_RIGHT_STICK_X);
        float gp_ry = gamepad_controller->GetNormalizedAnalogValue(GAMEPAD_RIGHT_STICK_Y);
        float gp_l2r2 = gamepad_controller->GetNormalizedAnalogValue(GAMEPAD_R2L2);

        float y = gp_ly + gp_ry;
        y = clamp(y,-1.0f,1.0f);

        if (input->IsKeyDown(INPUT_TURN_UP)){
            y = 1;
        }
        if (input->IsKeyDown(INPUT_TURN_DOWN)){
            y = -1;
        }
        if (input->IsKeyDown(INPUT_Q)){
            gp_rx = -1;
        }
        if (input->IsKeyDown(INPUT_E)){
            gp_rx = 1;
        }

        if (input->IsKeyDown(INPUT_TURN_RIGHT)){
           gp_lx = 1;
        }
        if (input->IsKeyDown(INPUT_TURN_LEFT)){
            gp_lx = -1;
        }
        if (input->IsKeyDown(INPUT_SHOOT)){
            gp_l2r2 = 1;
        }

        if (y > 0.01f){
            ship_character->MoveForwardBy(y * 100);
        }
        if (y < -0.01f){
            ship_character->MoveBackwardBy(y * 100);
        }
        if (gp_rx > 0.01f){
            ship_character->RollBy(-gp_rx * 0.05f);
            ship_character->StrafeBy(-gp_rx * 50.0f);
        }
        if (gp_rx < -0.01f){
            ship_character->RollBy(-gp_rx * 0.05f);
             ship_character->StrafeBy(-gp_rx * 50.0f);
        }
        if (gp_lx > 0.01f){
            ship_character->TurnRightBy(gp_lx*gp_lx * 0.04f);
            ship_character->RollBy(-gp_lx * 0.05f);
        }
        if (gp_lx < -0.01f){
            ship_character->TurnLeftBy((gp_lx*gp_lx) * 0.04f);
            ship_character->RollBy(-gp_lx * 0.05f);
        }
        if (gp_l2r2 > 0.01f){
            ship_character->ShootLaser();
            gamepad_controller->rmotor = 5000;
        }
    }

    //All further code requires the cursor not to be above an UI element
    if (ImGui::GetIO().WantCaptureMouse){
        //Clear mouse delta
        input->GetDelta(INPUT_MOUSE_WHEEL);
        return;
    }
}

void ApplicationShip::DrawImGuiUI(){
    //We're asked to import the f_filemodal file.
    if (f_import_file){
        debug->Info("Starting import of file %s\n",filemodal_filename.c_str());
        if (!assetmanager){
            debug->Info("No AssetManager. Creating...\n");
            assetmanager = new AssetManager();
        }
        Debugger* handle = debug->FindHandle("GLTFLoader");
        handle->SetLevel(DEBUG_INFO);
        gltfloader.LoadGLTFFile(filemodal_filename.c_str());
        f_import_file = false;
        GetAllAssetsFromGLTF();
    }

    //ImGui::ShowDemoWindow();
    RenderDebugMenuBar();
    RenderApplicationUI();
    //RenderShaderUI(default_shader);

    ImGui::Begin("Ship Settings");
    if (ship_character){
        ImGui::Checkbox("Overhead View",&f_camera_overhead);
        if (f_camera_overhead){
            ImGui::TextDisabled("(top down - wheel sets height, +shift pans)");
        }else{
            ImGui::TextDisabled("(free rotation - middle mouse orbits, +shift pans, wheel zooms)");
        }
        ImGui::Checkbox("Camera Track",&f_mode_camera_track);
        if (f_mode_camera_track){
            ImGui::TextDisabled("(follows the ship)");
        }else{
            ImGui::TextDisabled("(camera stays where it is put)");
        }
        ImGui::Checkbox("Lock Ship Axis",&f_lock_ship_axis);
        ImGui::Text("Ship Up              : (%.2f, %.2f, %.2f)",ship_character->GetUp().x,ship_character->GetUp().y,ship_character->GetUp().z);
        ImGui::Text("Ship Y-Pos           : %.2f",ship_character->GetPosition().y);

        if (ImGui::Button("Reset Ship Position")){
            ship_character->SetPosition(vec3(0,0,0));
            ship_character->SetRotation(quat().identity());
        }
        if (ImGui::Button("Add Asteroid")){
            //The dice are rolled HERE and the results travel in the command, rather than the
            //handler rolling them - so the command fully describes the asteroid it makes, and a
            //replay reproduces this one instead of a fresh roll. See SHIP_CMD_SPAWN_ASTEROID.
            //(Drawing from rrand on this thread is safe - DrawImGuiUI holds physics_mutex - but it
            //does consume from the simulation's own generator at a moment that is not tied to a
            //tick, which is a reproducibility hole this app has always had and this does not fix.)
            vec3 ship_pos = ship_character->GetPosition();
            vec3 offset = vec3(rrand->GetFloat(-10,10),rrand->GetFloat(-0.2,0.2),rrand->GetFloat(-10,10));
            float scale = rrand->GetFloat(0.8f,2.5f);
            vec3 angular_velocity = vec3(rrand->GetFloat(-0.5,0.5),rrand->GetFloat(-0.2,0.2),rrand->GetFloat(-0.5,0.5));
            vec3 velocity = vec3(rrand->GetFloat(-1,1),0,rrand->GetFloat(-1,1));
            //Submit and return, never wait: this runs with physics_mutex held, and the physics
            //thread needs that same lock to reach DrainCommands.
            SubmitUICommand(MakeSpawnAsteroidCommand(ship_pos + offset,scale,velocity,angular_velocity));
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Door Panel")){
            //Off to the ship's right, far enough out that the leaf cannot spawn inside the ship.
            //Queued, not built here - see the Add Asteroid button above and SHIP_CMD_SPAWN_DOOR.
            vec3 hinge = ship_character->GetPosition() + ship_character->GetLeft() * -4.0f;
            hinge.y = 0;
            SubmitUICommand(MakeSpawnDoorCommand(hinge,rrand->GetFloat(-TYPE_PI,TYPE_PI)));
        }
        if (ImGui::Button("Add Pickup")){
            //Was "Add Capsule", building a plain Object inline. It is a Pickup now (a trigger the
            //ship flies through), and like the other two spawn buttons the dice are rolled here
            //and the results travel in the command - see SHIP_CMD_SPAWN_PICKUP.
            vec3 ship_pos = ship_character->GetPosition();
            vec3 offset = vec3(rrand->GetFloat(-10,10),rrand->GetFloat(-0.2,0.2),rrand->GetFloat(-10,10));
            vec3 spin = vec3(rrand->GetFloat(-1,1),rrand->GetFloat(-1,1),rrand->GetFloat(-1,1));
            PickupKind kind = (PickupKind)(PICKUP_KIND_ENERGY + rrand->GetInt(0,2));
            SubmitUICommand(MakeSpawnPickupCommand(ship_pos + offset,2.0f,spin,kind,25.0f));
        }

        if (!doors.empty()){
            ImGui::SeparatorText("Door Panels");
            //Safe to read and write the doors from here: DrawImGuiUI runs with
            //renderer->physics_mutex held, so it cannot overlap a physics tick.
            HingedDoor* first = doors.front();
            float stiffness = first->spring_stiffness;
            float damping = first->spring_damping;
            float knock = first->knock_force;
            bool changed = false;
            changed |= ImGui::SliderFloat("Spring Nm/rad",&stiffness,0.0f,300.0f);
            changed |= ImGui::SliderFloat("Spring Damping",&damping,0.0f,100.0f);
            changed |= ImGui::SliderFloat("Laser Knock N",&knock,0.0f,5000.0f);
            if (changed){
                for (HingedDoor* door:doors){
                    door->spring_stiffness = stiffness;
                    door->spring_damping = damping;
                    door->knock_force = knock;
                }
            }
            if (ImGui::Button("Reset Doors")){
                for (HingedDoor* door:doors){
                    door->Reset();
                }
            }
            for (size_t i = 0;i < doors.size();i++){
                ImGui::Text("Door %zu swing         : %.1f deg",i,doors[i]->GetAngle() * 180.0f / TYPE_PI);
            }
        }
        ImGui::End();
    }

    //Its own window rather than a section of "Ship Settings", whose End() sits inside the
    //if (ship_character) above.
    if (!volumes.empty()){
        ImGui::Begin("Volume");
        //Only the transform below is per volume. Everything from "Shape" down is a uniform on
        //the one shared shader, so it applies to all of them at once.
        if (volumes.size() > 1){
            ImGui::SliderInt("Volume",&volume_selected,0,(int)volumes.size() - 1);
        }
        if (volume_selected >= (int)volumes.size()){
            volume_selected = 0;
        }
        Object* volume = volumes.at(volume_selected);
        ImGui::TextDisabled("%s",volume->name.c_str());

        //Read straight off the object rather than mirroring it in a member, so the checkbox
        //cannot drift out of step when the selection changes.
        bool visible = volume->IsVisible();
        if (ImGui::Checkbox("Visible",&visible)){
            volume->SetVisibility(visible);
        }
        const char* debug_views[] = {"Off","Marched interval","G-buffer input"};
        ImGui::Combo("Debug View",&volume_debug_view,debug_views,3);

        //Safe to move the volume from here: DrawImGuiUI runs with renderer->physics_mutex held.
        vec3 p = volume->GetPosition();
        vec3 s = volume->GetScale();
        if (ImGui::DragFloat3("Position",(float*)&p,0.1f)){
            volume->SetPosition(p);
        }
        if (ImGui::DragFloat3("Size",(float*)&s,0.1f,0.1f,200.0f)){
            volume->SetScale(s);
        }

        ImGui::SeparatorText("Shape");
        ImGui::SliderFloat("Noise Scale",&volume_noise_scale,0.25f,8.0f);
        ImGui::SliderFloat("Threshold",&volume_density_threshold,0.0f,0.95f);
        ImGui::SliderFloat("Edge Falloff",&volume_edge_falloff,0.0f,0.5f);
        ImGui::DragFloat3("Wind",(float*)&volume_wind,0.005f,-0.5f,0.5f);
        //Regenerating is a single dispatch, so it is fine to do on a slider release.
        bool rebuild = false;
        rebuild |= ImGui::SliderInt("Worley Cells",&volume_noise_cells,2,16);
        if (rebuild){
            BuildVolumeNoise();
        }

        ImGui::SeparatorText("March");
        ImGui::SliderFloat("Density",&volume_density,0.0f,3.0f);
        ImGui::SliderFloat("Absorption",&volume_light_absorption,0.0f,5.0f);
        ImGui::SliderFloat("Sun Intensity",&volume_sun_intensity,0.0f,4.0f);
        ImGui::SliderInt("View Steps",&volume_view_steps,1,128);
        ImGui::SliderInt("Sun Light Steps",&volume_light_steps,0,32);

        ImGui::SeparatorText("Cloud shadows");
        ImGui::Checkbox("Cast Shadows",&f_cloud_shadows);
        if (f_cloud_shadows){
            ImGui::SliderFloat("Shadow Strength",&cloud_shadow_strength,0.0f,1.0f);
            ImGui::SliderInt("Shadow Steps",&cloud_shadow_march_steps,4,128);
            //The fitted depth range, which is the number that says whether the slices are
            //landing anywhere useful: (far - near) / slices is how many world units of cloud
            //end up in one slice.
            float span = cloud_shadow_fit_far - cloud_shadow_fit_near;
            ImGui::TextDisabled("%ix%ix%i, depth %.1f..%.1f (%.2f/slice)",
                                cloud_shadow_resolution,cloud_shadow_resolution,
                                cloud_shadow_slices,cloud_shadow_fit_near,cloud_shadow_fit_far,
                                span / (float)cloud_shadow_slices);
        }

        ImGui::SeparatorText("Point / cone lights");
        //Per light, so the whole march costs
        //view_steps * (sun_steps + active_lights * point_steps).
        ImGui::SliderInt("Light Steps",&volume_point_light_steps,0,16);
        ImGui::SliderFloat("Falloff Exp",&volume_light_falloff,0.5f,3.0f);
        ImGui::SliderFloat("Cone Softness",&renderer->cone_softness,0.01f,0.6f);
        ImGui::SliderFloat("Max Radiance",&volume_max_radiance,1.0f,50.0f);
        if (ship_character && ship_character->headlight){
            ConeLight* hl = ship_character->headlight;
            bool on = hl->IsVisible();
            if (ImGui::Checkbox("Ship Headlight",&on)){
                hl->SetVisibility(on);
            }
            ImGui::SliderFloat("Headlight Power",&hl->brightness,0.0f,200.0f);
            ImGui::SliderFloat("Headlight Angle",&hl->cone_angle,5.0f,170.0f);
        }

        if (ImGui::Button("Reload Shader")){
            //Recompiles from disk. A shader error exits the app - see ReloadVolumeShader.
            ReloadVolumeShader();
        }
        ImGui::End();
    }

    if (f_filemodal){
        ImGui::OpenPopup("Attempt to import assets?");
    }

    // Always center this window when appearing
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Attempt to import assets?", NULL, ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::Text("Attempt to import [%s] with assetmanager?",filemodal_filename.c_str());
        ImGui::Separator();
        if (ImGui::Button("Yes, go ahead.", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            f_filemodal = false;
            f_import_file = true;
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("No, never mind", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            f_filemodal = false;
            f_import_file = false;
        }
        ImGui::EndPopup();
    }
}

//Called from within physics update.
void ApplicationShip::onContact(const rp3d::CollisionCallback::CallbackData& callbackData){
    //debug->Info("Contact: num pairs %hhu\n",callbackData.getNbContactPairs());
    for (uint32_t i = 0; i < callbackData.getNbContactPairs(); i++) {
        //We want to be notified of asteroid - ship collisions
        CollisionCallback::ContactPair contactPair = callbackData.getContactPair(i);


        Object* d1 = (Object*)contactPair.getBody1()->getUserData();
        Object* d2 = (Object*)contactPair.getBody2()->getUserData();

        if ((d1 == NULL) || (d2 == NULL)){
            debug->Err("Collision has no userdata\n");
            continue;
        }

        ShipCharacter* ship = dynamic_cast<ShipCharacter*>(d1);
        Object* other = d2;
        if (!ship){
            ship = dynamic_cast<ShipCharacter*>(d2);
            other = d1;
        }
        if (ship){
            //debug->Info("Bump\n");
            if (contactPair.getEventType() == CollisionCallback::ContactPair::EventType::ContactStart){
                //Velocity before impact?
                vec3 vel = ship->GetVelocity();
                //debug->Info("Ship velocity: %.2f\n",vel.length());
                vec3 othervel = other->GetVelocity();
                //debug->Info("Other velocity: %.2f\n",othervel.length());
                //The sum determines the impact
                float impact_vel = othervel.length() + vel.length();
                impact_vel = clamp(impact_vel,0,10);

                gamepad_controller->lmotor = 3200*impact_vel;
                gamepad_controller->rmotor = max((float)gamepad_controller->rmotor, 3200*impact_vel);

            }
        }

        Object* laser = NULL;
        if (d1->name.compare("laser_particle") == 0){
            laser = d1;
            other = d2;
        }
        if (d2->name.compare("laser_particle") == 0){
            laser = d2;
            other = d1;
        }
        if (laser){
            //Everything a laser hit does - knock a door, take a point off an asteroid - is per
            //HIT, so it happens on ContactStart and on no other event. That is load-bearing, not
            //tidiness. rp3d reports contacts from PhysicsWorld::update() BEFORE it solves them,
            //then keeps re-reporting the same contact as ContactStay for every tick the particle
            //is still inside whatever it hit - three or four of them for something this fast and
            //light. So an unfiltered handler fires several times per shot, and on all but the
            //first the solver has already bounced the laser back the way it came: reading its
            //velocity there knocked the door TOWARD the shooter (the stay knocks outnumbered and
            //outlasted the one good one), and an asteroid lost several health to a single shot.
            if (contactPair.getEventType() != CollisionCallback::ContactPair::EventType::ContactStart){
                continue;
            }

            //A laser particle weighs almost nothing, so its own impact impulse would barely
            //register on an 8kg door - the door gets told about the hit instead and gives itself
            //a knock worth seeing. Queued, not applied: we are inside the physics step here.
            HingedDoor* door = dynamic_cast<HingedDoor*>(other);
            if (door){
                vec3 direction = laser->GetVelocity();
                if (direction.length() < 0.0001f){
                    //Shouldn't happen to a laser in flight, but a knock still needs a direction.
                    direction = door->GetPosition() - laser->GetPosition();
                }
                door->QueueKnock(laser->GetPosition(),direction);
            }

            Asteroid* asteroid = dynamic_cast<Asteroid*>(other);
            if (asteroid){
                if (asteroid->health > 0){
                    asteroid->health = clamp(asteroid->health-1.0,0,100);
                    //debug->Info("Laser hit on asteroid. Health = %.1f\n",asteroid->health);
                }else if (asteroid->health == 0){
                    debug->Ok("Laser hit on asteroid. Staging explosion.\n");
                    //Only the fact that this asteroid died is recorded; RunLogic builds the
                    //explosion, because building one here would create rigid bodies inside
                    //rp_world->update(). health = -1 claims it, so it cannot be staged twice.
                    pending_asteroid_explosions.push_back(asteroid);
                    asteroid->health = -1;
                }
            }
        }

    }
}