#include "Window.h"
#include "Debug.h"

static Debugger* debug = new Debugger("Window",DEBUG_ALL);

std::vector<Window*>Window::windows; //A list of windows to match handles to

//Used to match window to handle
Window* Window::GetWindowByHandle(HWND hWnd){
    for (Window* wnd:windows){
        if (wnd->hWnd == hWnd){
            return wnd;
        }
    }
    return NULL;
}

Window::Window(){
    windows.push_back(this);
}

Window::~Window(){

}

void Window::Close(){
    f_should_quit = true;
    SendMessage(hWnd, WM_CLOSE, 0, 0);
}

//This registers the window classes for this application
void Window::RegisterWindowClass(HINSTANCE hInst){
    wc = {0};

    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windproc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInst;
    wc.hIcon = LoadIcon(0, IDI_APPLICATION);
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = 0;
    wc.lpszMenuName = 0;
    wc.lpszClassName = "GLLayeredWindowClass";
    wc.hIconSm = 0;

    if (!RegisterClassEx(&wc)){
        debug->Fatal("Failed to register GLLayeredWindowClass\n");
    }
}


LRESULT CALLBACK windproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
    Window* wnd = Window::GetWindowByHandle(hWnd);
    if (!wnd){
        //Cannot find window before WM_CREATE is issued
        //Normally, you get: WM_NCCREATE, WM_NCCALCSIZE, WM_CREATE, WM_SIZE, WM_MOVE
        debug->Err("Unable to find window by handle. msg = %lu (0x%0X) hWnd = %lu (0x%0X) \n",msg,msg,hWnd,hWnd);
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    switch(msg){
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE){
                SendMessage(hWnd, WM_CLOSE, 0, 0);
                return 0;
            }else if (wParam == VK_CONTROL){
                wnd->f_control_down = true;
                return 0;
            }else if (wParam == 'C'){
                if (wnd->f_control_down){
                    wnd->f_should_quit = true;
                    debug->Info("CTRL+C on window\n");
                }
                return 0;
            }else{
                //debug->Info("WM_KEYDOWN: %lu (0x%0X)\n",wParam,wParam);
                return 0;
            }
            break;

        case WM_NCHITTEST:
            //This returns the mouse is over the Titlebar of the window, which allows it to be dragged.
            return HTCAPTION;
        //case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_MOUSEMOVE:
        case WM_RBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_MOUSEWHEEL:

        case WM_LBUTTONUP:
        case WM_LBUTTONDOWN:

        case WM_MBUTTONUP:
        case WM_MBUTTONDOWN:

            break;
        case WM_DESTROY:
            debug->Warn("We should definitely quit now.\n");
            PostQuitMessage(0);
            break;
        case WM_CREATE:
            {
            DWORD thread_id = GetCurrentThreadId();
            debug->Ok("WM_CREATE on main window thread id %lu\n",thread_id);
            }
            break;
        case WM_ACTIVATE:
            if(LOWORD(wParam) == WA_INACTIVE ){
                wnd->f_has_focus = true;
            }else{
                wnd->f_has_focus = false;
            }
            break;
        case WM_MOVE:
        {
            int x = (int)(short) LOWORD(lParam);
            int y = (int)(short) HIWORD(lParam);
            //debug->Info("WM_MOVE to %i,%i\n",x,y);
            //return 0;
        }
        break;
        case WM_MOVING:
        {
            //debug->Info("WM_MOVING\n");
           //return 0;
        }
        break;
        case WM_SETFOCUS:
            wnd->f_has_focus = true;
        break ;
        case WM_KILLFOCUS:
            wnd->f_has_focus = false;
        break;
        case WM_PAINT:
            debug->Info("WM_PAINT received\n");
        break;


        default:
            //ebug->Info("Window Message %lu\n",msg);
            break;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}


/*
LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static TCHAR szBuffer[32] = {0};

    switch (msg)
    {
    case WM_CREATE:


        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_KEYUP:
        if (wParam == VK_CONTROL){
            f_control_down = false;
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE){
            SendMessage(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }else if (wParam == VK_CONTROL){
            f_control_down = true;
            return 0;
        }else if (wParam == 'C'){
            if (f_control_down){
                quit = true;
            }
            return 0;
        }else{
            //debug->Info("WM_KEYDOWN: %lu (0x%0X)\n",wParam,wParam);
            return 0;
        }
        break;

    case WM_NCHITTEST:
        //This returns the mouse is over the Titlebar of the window, which allows it to be dragged.
        return HTCAPTION;

    case WM_TIMER:
        _stprintf(szBuffer, _TEXT("%d FPS"), g_frames);
        SetWindowText(hWnd, szBuffer);
        g_frames = 0;
        return 0;

    default:
        break;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}
*/