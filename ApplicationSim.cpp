#include "ApplicationSim.h"
#include "Debug.h"
#include <stdlib.h>


static Debugger *debug = new Debugger("ApplicationSim", DEBUG_ALL);

ApplicationSim::ApplicationSim():Application(){
    debug->Info("Created new application.\n");
};

void ApplicationSim::Run(void){
    int2 dimensions = GetDisplaySettings();

    //Create a main window
    main_window = Window::CreateNewWindow(1280,720,&Window::wcs.at(0));
    if (!main_window){
        debug->Fatal("Unable to create window\n");
    }
    if (!main_window->Init()){
        debug->Fatal("Failed to init window\n");
    }

    main_window->Show(SW_SHOWDEFAULT);

    //Setup renderer
    Renderer::SetVSync(true);

    if (!main_window->InitImGui()){
        debug->Fatal("Failed to setup ImGui on Window\n");
    }

    //Create a renderer for this window
    renderer = new Renderer(main_window->width,main_window->height);
    renderer->Init();



    //Catch all input and window related messages in this thread:
    MSG msg = {0};
    while (main_window->f_should_quit == false){
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)){
            if (msg.message == WM_QUIT)
                break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        main_window->inputcontroller->UpdateKeyState();

        RunLogic();

        renderer->DrawFrame(NULL,NULL,NULL);

        //Tell ImGui to start a new frame
        main_window->ImGuiNewFrame();

        UpdateUI();

        main_window->ImGuiDrawFrame();

        //Copy to screen and finish
        main_window->DrawFrame();
    }
}




//Called before update physics
void ApplicationSim::RunLogic(){

}


void ApplicationSim::RenderRandTestWindow(){
    static float arr[256];
    static int count = 0;


    uint8_t r = rrand.Get_uint8();
    float f = 0;
    int s = 1;
    for (int i = 0;i<s;i++){
        f += rrand.Get_uint8();
    }
    f/= s;

    arr[count++] = f;//rand() % 256;

    count = count % 256;
    //UI
    ImGui::Begin("Random Test Suite");
    ImGui::Text("This is for testing our own random functions. Neat?");
    ImGui::PlotHistogram("Histogram", arr, IM_ARRAYSIZE(arr), 0, NULL, 0.0f, 255.0f, ImVec2(0, 80.0f));

    static int rand_int = 0;
    if (ImGui::Button("Get Random Int")){
        rand_int = rrand.GetInt();
    }
    ImGui::SameLine();
    ImGui::Text("Random Int: %i",rand_int);

    static int rand_limit = 0;
    static int minmax[2] = {0,1};
    ImGui::SliderInt2("Int Min / Max",minmax,-100,100);
    if (ImGui::Button("Get Random Int Between")){
        rand_limit = rrand.GetInt(minmax[0],minmax[1]);
    }
    ImGui::SameLine();
    ImGui::Text("Random Int: %i",rand_limit);

    static float rand_float = 0;
    static float fminmax[2] = {0,1};
    ImGui::SliderFloat2("Float Min / Max",fminmax,-100,100);
    if (ImGui::Button("Get Random Float Between")){
        rand_float = rrand.GetFloat(fminmax[0],fminmax[1]);
    }
    ImGui::SameLine();
    ImGui::Text("Random Float: %.3f",rand_float);

    //Normal distribution
    int num_bins = 50;
    static float bins[50] = {};
    int bin_start = -25;
    int bin_size = 1;

    //We sample from our normal distribution and see if they fall in a bin
    int num_samples = 10000;
    float smax = 0;
    static float bmax = 50;
    if (ImGui::Button("Sample Distribution")){
        memset(bins,0,sizeof(float)*num_bins);
        bmax = 50;
        for (int s =0;s<num_samples;s++){
            //float sample = rrand.GetFloat(-10,10);
            float sample = rrand.GetNormalFloat(0,4);
            if (sample > smax){
                smax = sample;
            }
            for (int i=0;i<num_bins;i++){
                //Check if sample is in this bin
                if ((sample > (bin_start + i)) && (sample < (bin_start + i + bin_size))){
                    bins[i]+=1;
                    if (bins[i] > bmax){
                        bmax = bins[i];
                    }
                    break;
                }
            }
        }
        bmax *= 1.1f;
    }

    ImGui::PlotHistogram("Sampled Floats", bins, IM_ARRAYSIZE(bins), 0, NULL, 0.0f, bmax, ImVec2(0, 80.0f));
    ImGui::End();
}

void ApplicationSim::RenderNoiseTestWindow(){
    ImGui::Begin("Perlin (and Others) Noise Test Suite");
    static bool regenerate = true;

    int texw = 512;
    int texh = 512;

    if (ImGui::CollapsingHeader("Perlin Noise")){
        if (ImGui::DragInt("Noise Seed",&pnoise.seed,1,0,3200))regenerate = true;
        if (ImGui::DragFloat("Noise Frequency",&pnoise.frequency,0.001f,0.0,10.0))regenerate = true;
        if (ImGui::DragFloat2("Noise Center Coord",(float*)&pnoise.coord,0.1f,-100.0,100.0))regenerate = true;
        if (ImGui::DragFloat("Noise Persistence",&pnoise.persistence,0.01f,0.0,10.0))regenerate = true;
        if (ImGui::DragFloat("Noise Lacunarity",&pnoise.lacunarity,0.01f,0.0,10.0))regenerate = true;
        if (ImGui::DragInt("Noise Num Octaves",&pnoise.num_octaves,1,1,10))regenerate = true;
        if (ImGui::DragFloat("Noise Offset",&pnoise.offset,1,0,256))regenerate = true;
        if (ImGui::DragFloat("Noise Scale",&pnoise.scale,0.01f,0,10.0))regenerate = true;

        if (ImGui::Button("Generate Noise"))regenerate = true;

        if (regenerate && noise_texture){
            //debug->Info("Generated a noise texture\n");
            int index = 0;
            for (int y=0;y<texh;y++){
                for (int x=0;x<texw;x++){
                    float f = pnoise.GetValue2D(x,y);
                    noise_texture->img_data[index + 0] = f;
                    noise_texture->img_data[index + 1] = f;
                    noise_texture->img_data[index + 2] = f;
                    index+=3;
                }
            }
        }
    }

    if (ImGui::CollapsingHeader("Worley Noise")){
        if (ImGui::DragInt("Grid Sizes",&wnoise.grid_size,1,1,64))regenerate = true;

        if (ImGui::DragFloat("Noise Scale",&wnoise.scale,0.1f,0,255.0))regenerate = true;

        if (ImGui::Button("Generate Noise"))regenerate = true;

        if (regenerate && noise_texture){
            //debug->Info("Generated a noise texture\n");
            int index = 0;
            for (int y=0;y<texh;y++){
                for (int x=0;x<texw;x++){
                    float f = wnoise.GetValue2D(x,y);
                    noise_texture->img_data[index + 0] = f;
                    noise_texture->img_data[index + 1] = f;
                    noise_texture->img_data[index + 2] = f;
                    index+=3;
                }
            }
        }
    }

    if (!noise_texture){
        ImGui::Text("Image not loaded yet");
    }else{
        ImGui::Image((ImTextureID)(intptr_t)noise_texture->texture_id, ImVec2(noise_texture->width,noise_texture->height));
    }

    ImGui::End();

    if (regenerate){
        regenerate = false;
        //Build the noise texture.
        if(!noise_texture){
            noise_texture = new Texture();
            noise_texture->Create2D(texw,texh,GL_RGB8,GL_TEXTURE_2D,1);
            //Allocate data for it
            noise_texture->img_data_sz = texw*texh*3;
            noise_texture->img_data = (uint8_t*)malloc(noise_texture->img_data_sz);
            noise_texture->UploadTexture();
        }else{
            noise_texture->UploadTexture();
        }
    }
}

void ApplicationSim::UpdateUI(){
    RenderRandTestWindow();

    RenderNoiseTestWindow();

    ImGui::ShowDemoWindow();
}