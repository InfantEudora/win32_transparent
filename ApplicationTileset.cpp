#include "ApplicationTileset.h"
#include "Debug.h"

static Debugger *debug = new Debugger("ApplicationTileset", DEBUG_ALL);

ApplicationTileset::ApplicationTileset():Application(){
    debug->Info("Created new application.\n");
};

ApplicationTileset::~ApplicationTileset(){
    if (http_server){
        http_server->Stop();
        delete http_server;
        http_server = nullptr;
    }
};

void ApplicationTileset::Init(void){
    //Create a renderer for this window
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init(PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }
    renderer->SetVSync(true);

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    //We make an assetmanager which we use to load/build all assets from:
    assetmanager = new AssetManager();

    main_scene = CreateNewScene("Main Scene");
    main_scene->UpdatePhysics();

     // Create a simple cube mesh and add it to the scene so there is something visible by default.
    {
        Mesh* cubeMesh = new Mesh();
        std::vector<vertex> verts;
        verts.reserve(36);
        float s = 0.5f; // half-size

        auto pushQuad = [&](vec3 a, vec3 b, vec3 c, vec3 d, vec3 n){
            vertex v;
            v.tangent = vec3(0,0,0);
            v.matid = 0;

            v.pos = a; v.normal = n; v.uv = vec2(0,0); verts.push_back(v);
            v.pos = c; v.normal = n; v.uv = vec2(1,1); verts.push_back(v);
            v.pos = b; v.normal = n; v.uv = vec2(1,0); verts.push_back(v);


            v.pos = a; v.normal = n; v.uv = vec2(0,0); verts.push_back(v);

            v.pos = d; v.normal = n; v.uv = vec2(0,1); verts.push_back(v);
            v.pos = c; v.normal = n; v.uv = vec2(1,1); verts.push_back(v);
        };

        // +X face
        pushQuad(vec3(s,-s,-s), vec3(s,-s,s), vec3(s,s,s), vec3(s,s,-s), vec3(1,0,0));
        // -X face
        pushQuad(vec3(-s,-s,s), vec3(-s,-s,-s), vec3(-s,s,-s), vec3(-s,s,s), vec3(-1,0,0));
        // +Y face (top)
        pushQuad(vec3(-s,s,-s), vec3(s,s,-s), vec3(s,s,s), vec3(-s,s,s), vec3(0,1,0));
        // -Y face (bottom)
        pushQuad(vec3(-s,-s,s), vec3(s,-s,s), vec3(s,-s,-s), vec3(-s,-s,-s), vec3(0,-1,0));
        // +Z face (front)
        pushQuad(vec3(-s,-s,s), vec3(-s,s,s), vec3(s,s,s), vec3(s,-s,s), vec3(0,0,1));
        // -Z face (back)
        pushQuad(vec3(s,-s,-s), vec3(s,s,-s), vec3(-s,s,-s), vec3(-s,-s,-s), vec3(0,0,-1));

        cubeMesh->SetMeshData(verts.data(), (int)verts.size());
        cubeMesh->num_materials = 1;
        cubeMesh->InitVBOVAO();

        Object* cubeObj = new Object();
        cubeObj->name = "Cube";
        cubeObj->SetMesh(cubeMesh);
        cubeObj->SetPosition(vec3(0,0,0));
        // Add a light green material and assign it to the cube so it's visible.
        Material lightGreenMat;
        lightGreenMat.name = "LightGreen";
        lightGreenMat.glsl_material.color = vec4(0.6f, 1.0f, 0.6f, 1.0f);
        int matIndex = renderer->AddMaterial(lightGreenMat);
        cubeObj->material_slot[0] = matIndex;
        main_scene->AddObject(cubeObj);

        //Setup sun light
        DirectionalLight* sun = new DirectionalLight();
        sun->name = "Directional Light (Sun)";
        sun->SetPosition(vec3(-10,10,10));
        sun->color = vec3(1,0.85,0.7);
        sun->brightness = 8.5;
        sun->viewport.zoom = 15;
        sun->SetLookAt(vec3());
        main_scene->AddObject(sun);
    }

    //Create an HTTP server to listen for connections
    http_server = new HTTPServer(9090);
    if (!http_server->Start()){
        debug->Fatal("Failed to start HTTP server\n");
    }
    // Set initial values that the page can display
    http_server->SetVariable("playerHealth", "100");
    http_server->SetVariable("score", "0");
    http_server->SetVariable("fps", "0");
    debug->Info("HTTP Server started on port 9090\n");
}

static unsigned long long g_lastTick = GetTickCount64();
static int g_frames = 0;
static int g_lastFPS = 0;

//Called before update physics
void ApplicationTileset::RunLogic(){
    // Count frames and update FPS once per second
    g_frames++;
    unsigned long long now = GetTickCount64();
    if (now - g_lastTick >= 1000) {
        g_lastFPS = g_frames;
        g_frames = 0;
        g_lastTick = now;
        if (http_server) {
            http_server->SetVariable("fps", std::to_string(g_lastFPS));
        }
    }

    CheckObjectSelection();
}

void ApplicationTileset::DrawImGuiUI(){
    //UI
    ImGui::Begin("Hi there!");
    ImGui::Text("This application only renders a window.");

    // Player health slider
    if (ImGui::SliderInt("Player Health", &ui_playerHealth, 0, 100)){
        if (http_server) http_server->SetVariable("playerHealth", std::to_string(ui_playerHealth));
    }

    // Score slider
    if (ImGui::SliderInt("Score", &ui_score, 0, 100000)){
        if (http_server) http_server->SetVariable("score", std::to_string(ui_score));
    }

    // Show last measured FPS
    ImGui::Text("FPS: %d", g_lastFPS);

    ImGui::End();

    RenderDebugMenuBar();
    RenderApplicationUI();
    RenderRandTestWindow();
}