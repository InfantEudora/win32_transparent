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
        debug->Fatal("Unable to FrameFunction thread\n");
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
DWORD WINAPI ApplicationCANUI::CANReadThreadFunction(LPVOID lpParameter){
    ApplicationCANUI* app = static_cast<ApplicationCANUI*>(lpParameter);
    if (!app){
        debug->Err("No application was supplied to FrameThread\n");
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

    ImVec4 red = ImVec4(1,0,0,1);
    ImVec4 green = ImVec4(0,1,0,1);
    ImVec4 yellow = ImVec4(1,1,0,1);

    ImGui::SameLine();
    if (!pcan_reader->connected){
        ImGui::TextColored(red,"Disconnected");
    }else{
        ImGui::TextColored(green,"Connected");

        CANMessage msg;
        while (pcan_reader->msg_queue.pop(msg)){
            //debug->Info("RX MSG: %X\n",msg.frame.can_id);
            if (msg.frame.can_id > 0x7FF){
                infy_queue.push(msg);
            }else{
                //vector_queue.push(msg);
                cab500_queue.push(msg);
            }
        }



    }
    ImGui::End();

    //InfyPower interface
    ImGui::Begin("InfyPower Control Interface");
    ImGui::Text("Num Queued Msgs: %i",infy_queue.size());

    if (ImGui::Button("Generate Test Messages")){
        infy->GenerateTestMessages();
    };
    ImGui::NewLine();


    ImGui::Button("Enable Device");

    ImGui::Text("Ouput Voltage: %.2f Volts",infy->output_voltage);
    ImGui::Text("Ouput Current: %.2f Amps",infy->output_current);

    CANMessage msg;
    while (infy_queue.pop(msg)){
        debug->Info("Infy Message: %X\n",msg.frame.can_id);
        infy->HandleCANMessage(&msg.frame);
    }

    ImGui::End();



    //CAB Sensor Interface
    ImGui::Begin("CAB 500 Current Sensor");
    ImGui::Text("Num Queued Msgs: %i",cab500_queue.size());
    int32_t current_ma = 0;
    while (cab500_queue.pop(msg)){
        debug->Info("CAB Message: %X\n",msg.frame.can_id);

        if (msg.frame.can_id = 0x3C2){

            memcpy(&current_ma,&msg.frame.data[0],4);

            current_ma = byteswap_i32(current_ma);

        }
        //infy->HandleCANMessage(&msg.frame);
    }
    //current_ma = 0x7ffffffe;
    if (current_ma & 0x80000000){
        current_ma &= 0x7FFFFFFF;
    }else{
        current_ma -= 0x7FFFFFFF;
    }

    ImGui::Text("CAB Current 0x%08X",current_ma);
    ImGui::Text("CAB Current %12li mA (%.2f Amps)", current_ma,(float)current_ma / 1000.0f);

    ImGui::End();

}