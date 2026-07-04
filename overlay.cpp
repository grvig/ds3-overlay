#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

// The background is this color, and we tell Windows to treat this exact
// color as invisible - so the window itself has no visible background,
// just whatever we explicitly draw on top of it.
const COLORREF TRANSPARENT_KEY = RGB(0, 200, 0);

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // A small red box, just so we can still see the window is there
            // once the rest of it turns invisible.
            HBRUSH redBrush = CreateSolidBrush(RGB(200, 0, 0));
            RECT box = { 20, 20, 120, 70 };
            FillRect(hdc, &box, redBrush);
            DeleteObject(redBrush);

            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"DS3OverlayWindowClass";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = CreateSolidBrush(TRANSPARENT_KEY);
    RegisterClass(&wc);

    // WS_EX_LAYERED lets us mark a color as see-through.
    // WS_EX_TRANSPARENT makes mouse clicks pass straight through the window
    // to whatever is underneath it (the game), instead of being caught by us.
    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        CLASS_NAME,
        L"DS3 Overlay",
        WS_POPUP,
        100, 100, 400, 200,
        nullptr, nullptr, hInstance, nullptr
    );

    if (hwnd == nullptr) {
        return 1;
    }

    SetLayeredWindowAttributes(hwnd, TRANSPARENT_KEY, 0, LWA_COLORKEY);

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
