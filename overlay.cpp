#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <string>
#include "ds3reader.h"

// The background is this color, and we tell Windows to treat this exact
// color as invisible - so the window itself has no visible background,
// just whatever we explicitly draw on top of it.
const COLORREF TRANSPARENT_KEY = RGB(0, 200, 0);

const uint32_t IUDEX_GUNDYR_DEFEATED_FLAG = 14000800;

Ds3Connection g_conn;
bool g_connected = false;
bool g_gundyrDefeated = false;

const UINT_PTR TIMER_ID = 1;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            KillTimer(hwnd, TIMER_ID);
            PostQuitMessage(0);
            return 0;

        case WM_TIMER: {
            // Re-check the boss flag periodically so the overlay reflects
            // what's actually happening in the game right now.
            if (!g_connected) {
                g_connected = ConnectToDs3(g_conn);
            }
            if (g_connected) {
                g_gundyrDefeated = ReadEventFlag(g_conn, IUDEX_GUNDYR_DEFEATED_FLAG);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // Wipe out whatever was drawn last time (e.g. the previous
            // status text) before drawing the current one, so old and new
            // text don't overlap into an unreadable mess.
            HBRUSH clearBrush = CreateSolidBrush(TRANSPARENT_KEY);
            FillRect(hdc, &ps.rcPaint, clearBrush);
            DeleteObject(clearBrush);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 0));

            HFONT font = CreateFont(
                24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
            );
            HFONT oldFont = (HFONT)SelectObject(hdc, font);

            std::wstring statusText;
            if (!g_connected) {
                statusText = L"Waiting for Dark Souls III...";
            } else {
                statusText = L"Iudex Gundyr: ";
                statusText += g_gundyrDefeated ? L"Defeated" : L"Not defeated";
            }

            RECT textRect = { 20, 20, 380, 180 };
            DrawText(hdc, statusText.c_str(), -1, &textRect, DT_LEFT | DT_TOP);

            SelectObject(hdc, oldFont);
            DeleteObject(font);

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

    g_connected = ConnectToDs3(g_conn);
    SetTimer(hwnd, TIMER_ID, 1000, nullptr);

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
