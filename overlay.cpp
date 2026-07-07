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

Ds3Connection g_conn;
bool g_connected = false;
bool g_bossDefeated[BOSS_COUNT] = {};

const UINT_PTR TIMER_ID = 1;
const int LINE_HEIGHT = 26;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            KillTimer(hwnd, TIMER_ID);
            PostQuitMessage(0);
            return 0;

        case WM_TIMER: {
            // Re-check every boss's flag periodically so the overlay
            // reflects what's actually happening in the game right now.
            if (!g_connected) {
                g_connected = ConnectToDs3(g_conn);
            }
            if (g_connected) {
                for (int i = 0; i < BOSS_COUNT; i++) {
                    g_bossDefeated[i] = ReadEventFlag(g_conn, BOSS_LIST[i].defeatedFlag);
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // Draw the entire frame off-screen first, then copy it to the
            // window in one go. Drawing directly to the window (like we did
            // before) causes a visible blank flash each time it redraws -
            // this "double buffering" avoids that.
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

            HBRUSH clearBrush = CreateSolidBrush(TRANSPARENT_KEY);
            FillRect(memDC, &clientRect, clearBrush);
            DeleteObject(clearBrush);

            SetBkMode(memDC, TRANSPARENT);

            HFONT font = CreateFont(
                18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
            );
            HFONT oldFont = (HFONT)SelectObject(memDC, font);

            if (!g_connected) {
                SetTextColor(memDC, RGB(255, 255, 0));
                RECT textRect = { 20, 20, 380, 60 };
                DrawText(memDC, L"Waiting for Dark Souls III...", -1, &textRect, DT_LEFT | DT_TOP);
            } else {
                for (int i = 0; i < BOSS_COUNT; i++) {
                    SetTextColor(memDC, g_bossDefeated[i] ? RGB(0, 255, 0) : RGB(255, 255, 255));
                    RECT lineRect = { 20, 20 + i * LINE_HEIGHT, 380, 20 + (i + 1) * LINE_HEIGHT };
                    DrawText(memDC, BOSS_LIST[i].name, -1, &lineRect, DT_LEFT | DT_TOP);
                }
            }

            BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldFont);
            DeleteObject(font);
            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);

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
    // Made tall enough to fit the full boss list for now - a later step
    // will size this automatically instead of a fixed guess.
    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        CLASS_NAME,
        L"DS3 Overlay",
        WS_POPUP,
        100, 100, 400, 40 + BOSS_COUNT * LINE_HEIGHT,
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
