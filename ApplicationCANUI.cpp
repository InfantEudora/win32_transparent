#include "ApplicationCANUI.h"
#include "Debug.h"
#include "caninterfaces/controllers/InfyPower.h"

static Debugger *debug = new Debugger("ApplicationCANUI", DEBUG_ALL);

ApplicationCANUI::ApplicationCANUI():Application(){
    debug->Info("Created new application.\n");
};

void ApplicationCANUI::Run(void){
    int2 dimensions = GetDisplaySettings();

    //Create a main window
    main_window = Window::CreateNewLayeredWindow(1600,800,&Window::wcs.at(0));
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

    //Init can interface
    pcan_reader = new PCANReader();
    pcan_reader->name = "USB1";

    //Create hardware
    infy = new InfyPower();
    delta = new DeltaSM15K();
    delta->ipv4[0] = 192;
    delta->ipv4[1] = 168;
    delta->ipv4[2] = 0;
    delta->ipv4[3] = 108;


    //And do all render calls from a seperate thread:
    HANDLE hThread = NULL;

    // Create a new thread which will get this one's render context
    hThread = CreateThread(
        NULL,    // Thread attributes
        0,       // Stack size (0 = use default)
        CANReadThreadFunction, // Thread start address
        this,    // Parameter to pass to the thread
        0,       // Creation flags
        &thread_id_physics);   // Thread id

    if (hThread == NULL){
        debug->Fatal("Unable to create FrameFunction thread\n");
    }

    // Create a new thread which will get this one's render context
    hThread = CreateThread(
        NULL,    // Thread attributes
        0,       // Stack size (0 = use default)
        DeltaReadThread, // Thread start address
        this,    // Parameter to pass to the thread
        0,       // Creation flags
        NULL);   // Thread id

    if (hThread == NULL){
        debug->Fatal("Unable to create DeltaReadThread thread\n");
    }

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

//Thread function for reading frames into buffer
DWORD WINAPI ApplicationCANUI::DeltaReadThread(LPVOID lpParameter){
    ApplicationCANUI* app = static_cast<ApplicationCANUI*>(lpParameter);
    if (!app){
        debug->Err("No application was supplied to DeltaReadThread\n");
        return 0;
    }

    DWORD thread_id_delta = GetCurrentThreadId();
    debug->Info("DeltaReadThread ThreadID: %lu\n",thread_id_delta);

    while(1){
        if (!app->delta->IsConnected()){
            Sleep(100);
        }else{
            app->delta->UpdateDevice();
            Sleep(100);
        }
    }

}

//Thread function for reading frames into buffer
DWORD WINAPI ApplicationCANUI::CANReadThreadFunction(LPVOID lpParameter){
    ApplicationCANUI* app = static_cast<ApplicationCANUI*>(lpParameter);
    if (!app){
        debug->Err("No application was supplied to CANReadThreadFunction\n");
        return 0;
    }

    app->thread_id_render = GetCurrentThreadId();
    debug->Info("CANReadThreadFunction ThreadID: %lu\n",app->thread_id_render);

    while(1){
        if (!app->pcan_reader->connected){
            Sleep(1);
        }else{

            //Send a single can Frame
            CANMessage outmsg;
            outmsg.frame.can_dlc = 8;
            outmsg.frame.can_id = 0x028cf001;


            //app->pcan_reader->out_queue.push(outmsg);
            //app->pcan_reader->out_queue.push(outmsg);

            app->pcan_reader->ReadMessages();
        }
    }
}

//Called before update physics
void ApplicationCANUI::RunLogic(){

}


int32_t byteswap_i32(int32_t in){
    uint32_t i = in;
    uint8_t* iptr = (uint8_t*)&i;
    int32_t o = 0;
    uint8_t* optr = (uint8_t*)&o;
    optr[0] = iptr[3];
    optr[1] = iptr[2];
    optr[2] = iptr[1];
    optr[3] = iptr[0];
    return o;
}

void ApplicationCANUI::UpdateUI(){
    //UI
    ImGui::Begin("Hi there!");
    ImGui::Text("A CAN UI Application");
    if (ImGui::Button("Connect CAN-USB interface\n")){
        pcan_reader->Connect();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset CAN-USB interface\n")){
        pcan_reader->Reset();
    }

    ImVec4 red = ImVec4(1,0,0,1);
    ImVec4 green = ImVec4(0,1,0,1);
    ImVec4 yellow = ImVec4(1,1,0,1);
    ImVec4 grey = ImVec4(0.6,0.6,0.6,1.0);

    ImGui::SameLine();
    if (!pcan_reader->connected){
        ImGui::TextColored(red,"Disconnected");
    }else{
        ImGui::TextColored(green,"Connected");

        CANMessage msg;
        while (pcan_reader->msg_queue.pop(msg)){
            debug->Info("RX MSG: %X\n",msg.frame.can_id);
            if (msg.frame.can_id > 0x7FF){
                infy_queue.push(msg);
            }else{
                //vector_queue.push(msg);
                cab500_queue.push(msg);
            }
        }



    }
    ImGui::End();


    //Delta Interface
    ImGui::Begin("Delta SM15K Control Interface");
    ImGui::InputInt4("IP Address",delta->ipv4);
    if (ImGui::Button("Connect")){
        delta->Connect();
    };
    ImGui::Separator();

    if (ImGui::Button("Query Device")){
        delta->SendDeltaMessage();
    };

    if (ImGui::Button("Read Device")){
        delta->ReadDeltaMessage();
    };
    if (ImGui::Button("Set Remote State Ethernet")){
        delta->SetRemoteState();
    };

    if (ImGui::Button("Enable Output")){
        delta->EnableOutput(true);
    };
    ImGui::SameLine();
    if (ImGui::Button("Disable Output")){
        delta->EnableOutput(false);
    };
    if (ImGui::Button("Update Device")){
        delta->UpdateDevice();
    };


    ImGui::DragFloat("Voltage Set", &delta->output_voltage_set,1,0,500);
    ImGui::DragFloat("Current Set", &delta->output_current_set,0.1,-15,15);

    ImGui::NewLine();
    ImGui::Text("Output Voltage: %.2f Volt\n",delta->output_voltage);
    ImGui::Text("Output Current: %.2f Amps\n",delta->output_current);

    ImGui::End();


    //InfyPower interface
    ImGui::Begin("InfyPower Control Interface");
    ImGui::Text("Num Queued Msgs: %i",infy_queue.size());

    if (ImGui::Button("Generate Test Messages")){
        infy->GenerateTestMessages();
    };
    ImGui::NewLine();


    if (infy->output_enabled){
        if (ImGui::Button("Disable Device")){
            infy->output_enabled = false;
        }
    }else{
        if (ImGui::Button("Enable Device")){
            infy->output_enabled = true;
        }
    }

    static bool update_device = false;

    ImGui::Checkbox("Update Device",&update_device);

    if (update_device){
        can_frame_t frame;
        if (infy->UpdateDevice(frame)){
            pcan_reader->WriteCanFrame(&frame);
        }
    }

    if (ImGui::Button("Query Voltage")){
        can_frame_t frame;
        infy->QueryDevice(frame,0x04);
        pcan_reader->WriteCanFrame(&frame);
        infy->QueryDevice(frame,0x06);

    }

    if (ImGui::Button("Get Num Devices")){
        can_frame_t frame;
        infy->ReadNrSystemModules(frame);
        pcan_reader->WriteCanFrame(&frame);
    }

    if (ImGui::Button("Broadcast Set Dial Mode")){
        can_frame_t frame;
        infy->BroadcastSetDialMode(frame);
        pcan_reader->WriteCanFrame(&frame);

    }

    if (infy->output_enabled){
        ImGui::TextColored(green,"Output              : Enabled");
    }else{
        ImGui::Text("Output              : Disabled");
    }
    if (infy->output_enabled_read){
        ImGui::TextColored(green,"Output Read         : Enabled");
    }else{
        ImGui::Text("Output Read         : Disabled");
    }

    //Get all the bits from device
    ImGui::Text("State 2 Bits: 0x%02X",infy->module_state_2);
    for (int i = 0; i < 8; i++){
        if (i > 0)
            ImGui::SameLine();
        ImGui::PushID(i);
        if (infy->module_state_2 & (1<<i)){
            ImGui::PushStyleColor(ImGuiCol_Button, red);
        }else{
            ImGui::PushStyleColor(ImGuiCol_Button, grey);
        }
        //ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(i / 7.0f, 0.7f, 0.7f));
        //ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(i / 7.0f, 0.8f, 0.8f));
        ImGui::Button("BIT2");
        ImGui::PopStyleColor(1);
        ImGui::PopID();
    }

    ImGui::Text("State 1 Bits: 0x%02X",infy->module_state_1);
    for (int i = 0; i < 8; i++){
        if (i > 0)
            ImGui::SameLine();
        ImGui::PushID(i);
        if (infy->module_state_1 & (1<<i)){
            ImGui::PushStyleColor(ImGuiCol_Button, red);
        }else{
            ImGui::PushStyleColor(ImGuiCol_Button, grey);
        }
        //ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(i / 7.0f, 0.7f, 0.7f));
        //ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(i / 7.0f, 0.8f, 0.8f));
        ImGui::Button("BIT1");
        ImGui::PopStyleColor(1);
        ImGui::PopID();
    }

    ImGui::Text("State 0 Bits: 0x%02X",infy->module_state_0);
    for (int i = 0; i < 8; i++){
        if (i > 0)
            ImGui::SameLine();
        ImGui::PushID(i);
        if (infy->module_state_0 & (1<<i)){
            ImGui::PushStyleColor(ImGuiCol_Button, red);
        }else{
            ImGui::PushStyleColor(ImGuiCol_Button, grey);
        }
        //ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(i / 7.0f, 0.7f, 0.7f));
        //ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(i / 7.0f, 0.8f, 0.8f));
        ImGui::Button("BIT0");
        ImGui::PopStyleColor(1);
        ImGui::PopID();
    }

    float vout = infy->vout_setmv / 1000.0f;
    if (ImGui::DragFloat("Output Voltage", &vout,0.5,0,500)){
        infy->vout_setmv = vout * 1000.0f;
    }
    float iout = infy->iout_setma / 1000.0f;
    if (ImGui::DragFloat("Output Current", &iout,0.1,0,50)){
        infy->iout_setma = iout * 1000.0f;
    }

    ImGui::Text("Input Voltage       : %.2f Volts",infy->input_voltage);
    ImGui::Text("Ouput Voltage       : %.2f Volts",infy->output_voltage);
    ImGui::Text("External Voltage    : %.2f Volts",infy->external_voltage);
    ImGui::Text("Ouput Current       : %.2f Amps",infy->output_current);
    ImGui::Text("Available Current   : %.2f Amps",infy->available_current);
    ImGui::Text("Ouput Power         : %.1f Watts",infy->external_voltage * infy->output_current);

    ImGui::Text("Ouput Voltage Set   : %.2f Volts",infy->vout_setmv/1000.0f);
    ImGui::Text("Ouput Voltage Read  : %.2f Volts",infy->vout_setmv_read/1000.0f);
    ImGui::Text("Ouput Current Set   : %.2f Amps",infy->iout_setma/1000.0f);
    ImGui::Text("Ouput Current Read  : %.2f Amps",infy->iout_setma_read/1000.0f);

    ImGui::Text("Ambient Temperature : %.1f Deg C",infy->ambient_temperature);

    CANMessage msg;
    while (infy_queue.pop(msg)){
        debug->Info("Infy Message: %X\n",msg.frame.can_id);
        infy->HandleCANMessage(&msg.frame);
    }
    ImGui::End();




}