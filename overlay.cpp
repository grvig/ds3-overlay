#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <string>
#include <cstring>
#include "ds3reader.h"

Ds3Connection g_conn;
bool g_connected = false;
bool g_bossDefeated[BOSS_COUNT] = {};
uint32_t g_souls = 0;

// How long to wait after first spotting the game process before we actually
// attach to it and inject code. Attaching the instant the process appears
// risks catching the game mid-launch (before its own startup has settled),
// which seems to have caused it to fail to start on one occasion - this
// grace period gives it room to finish starting up first.
DWORD g_firstSeenTick = 0;
const DWORD STARTUP_GRACE_MS = 8000;

const UINT_PTR TIMER_ID = 1;
const int LINE_HEIGHT = 20;
const int WINDOW_WIDTH = 400;
const int SUMMARY_HEIGHT = LINE_HEIGHT + 10;

// The boss list is grouped into sections (Base Game, Ashes of Ariandel, The
// Ringed City) with a header line above each group. Count how many section
// headers there are so the window is made tall enough to fit them.
int CountSections() {
    int count = 0;
    const wchar_t* lastSection = nullptr;
    for (int i = 0; i < BOSS_COUNT; i++) {
        if (lastSection == nullptr || wcscmp(lastSection, BOSS_LIST[i].section) != 0) {
            count++;
            lastSection = BOSS_LIST[i].section;
        }
    }
    return count;
}
const int SECTION_COUNT = CountSections();
const int WINDOW_HEIGHT = 40 + LINE_HEIGHT + SUMMARY_HEIGHT + BOSS_COUNT * LINE_HEIGHT + SECTION_COUNT * LINE_HEIGHT;

// Renders the current frame into a true per-pixel-transparent bitmap and
// hands it to Windows as the window's whole appearance. Unlike the old
// "treat this one color as invisible" trick, every pixel gets its own real
// transparency level, so text edges blend cleanly with whatever is behind
// the overlay instead of picking up a tint from a fake background color.
void RenderOverlay(HWND hwnd) {
    HDC screenDC = GetDC(nullptr);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = WINDOW_WIDTH;
    bmi.bmiHeader.biHeight = -WINDOW_HEIGHT; // negative = top-down, easier to reason about
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP bitmap = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, bitmap);

    // Start fully transparent (all zero bytes = black, 0 alpha) everywhere.
    memset(pixels, 0, WINDOW_WIDTH * WINDOW_HEIGHT * 4);

    SetBkMode(memDC, TRANSPARENT);
    HFONT font = CreateFont(
        15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI"
    );
    HFONT oldFont = (HFONT)SelectObject(memDC, font);

    if (!g_connected) {
        SetTextColor(memDC, RGB(255, 255, 0));
        RECT textRect = { 20, 20, 380, 60 };
        DrawText(memDC, L"Waiting for Dark Souls III...", -1, &textRect, DT_LEFT | DT_TOP);
    } else {
        int defeatedCount = 0;
        for (int i = 0; i < BOSS_COUNT; i++) {
            if (g_bossDefeated[i]) {
                defeatedCount++;
            }
        }

        std::wstring soulsLine = L"Souls: " + std::to_wstring(g_souls);
        SetTextColor(memDC, RGB(255, 255, 255));
        RECT soulsRect = { 20, 20, 380, 20 + LINE_HEIGHT };
        DrawText(memDC, soulsLine.c_str(), -1, &soulsRect, DT_LEFT | DT_TOP);

        std::wstring summary = L"Bosses Defeated: " + std::to_wstring(defeatedCount) + L" / " + std::to_wstring(BOSS_COUNT);
        SetTextColor(memDC, RGB(255, 255, 0));
        RECT summaryRect = { 20, 20 + LINE_HEIGHT, 380, 20 + LINE_HEIGHT + SUMMARY_HEIGHT };
        DrawText(memDC, summary.c_str(), -1, &summaryRect, DT_LEFT | DT_TOP);

        int y = 20 + LINE_HEIGHT + SUMMARY_HEIGHT;
        const wchar_t* lastSection = nullptr;
        for (int i = 0; i < BOSS_COUNT; i++) {
            if (lastSection == nullptr || wcscmp(lastSection, BOSS_LIST[i].section) != 0) {
                lastSection = BOSS_LIST[i].section;
                SetTextColor(memDC, RGB(150, 150, 255));
                RECT headerRect = { 20, y, 380, y + LINE_HEIGHT };
                DrawText(memDC, lastSection, -1, &headerRect, DT_LEFT | DT_TOP);
                y += LINE_HEIGHT;
            }

            SetTextColor(memDC, g_bossDefeated[i] ? RGB(0, 255, 0) : RGB(255, 255, 255));
            RECT lineRect = { 30, y, 380, y + LINE_HEIGHT };
            DrawText(memDC, BOSS_LIST[i].name, -1, &lineRect, DT_LEFT | DT_TOP);
            y += LINE_HEIGHT;
        }
    }

    // GDI only paints the color channels, not transparency. Since we started
    // from solid black, each pixel's brightness now tells us how much "ink"
    // is there - use that as its transparency (this also happens to be
    // exactly the format Windows wants for blending: color pre-multiplied
    // by transparency).
    BYTE* bytes = (BYTE*)pixels;
    for (int i = 0; i < WINDOW_WIDTH * WINDOW_HEIGHT; i++) {
        BYTE b = bytes[i * 4 + 0];
        BYTE g = bytes[i * 4 + 1];
        BYTE r = bytes[i * 4 + 2];
        BYTE alpha = (b > g ? (b > r ? b : r) : (g > r ? g : r));
        bytes[i * 4 + 3] = alpha;
    }

    POINT srcPos = { 0, 0 };
    SIZE size = { WINDOW_WIDTH, WINDOW_HEIGHT };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(hwnd, screenDC, nullptr, &size, memDC, &srcPos, 0, &blend, ULW_ALPHA);

    SelectObject(memDC, oldFont);
    DeleteObject(font);
    SelectObject(memDC, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            KillTimer(hwnd, TIMER_ID);
            PostQuitMessage(0);
            return 0;

        case WM_TIMER: {
            // If the game has been closed since our last check, forget the
            // old connection and go back to "waiting" instead of showing
            // stale or wrong boss info.
            if (g_connected) {
                DWORD exitCode = 0;
                bool stillRunning = GetExitCodeProcess(g_conn.process, &exitCode) && exitCode == STILL_ACTIVE;
                if (!stillRunning) {
                    CloseHandle(g_conn.process);
                    g_conn = Ds3Connection();
                    g_connected = false;
                }
            }

            // Re-check every boss's flag periodically so the overlay
            // reflects what's actually happening in the game right now.
            if (!g_connected) {
                DWORD pid = FindProcessId(DS3_PROCESS_NAME);
                if (pid == 0) {
                    g_firstSeenTick = 0;
                } else {
                    if (g_firstSeenTick == 0) {
                        g_firstSeenTick = GetTickCount();
                    }
                    if (GetTickCount() - g_firstSeenTick >= STARTUP_GRACE_MS) {
                        g_connected = ConnectToDs3(g_conn);
                    }
                }
            }
            if (g_connected) {
                std::vector<uint8_t> flags = ReadAllBossFlags(g_conn);
                for (int i = 0; i < BOSS_COUNT; i++) {
                    g_bossDefeated[i] = flags[i];
                }
                g_souls = ReadSouls(g_conn);
            }
            RenderOverlay(hwnd);
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
    RegisterClass(&wc);

    // WS_EX_LAYERED lets us give the window true per-pixel transparency.
    // WS_EX_TRANSPARENT makes mouse clicks pass straight through the window
    // to whatever is underneath it (the game), instead of being caught by us.
    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        CLASS_NAME,
        L"DS3 Overlay",
        WS_POPUP,
        10, 10, WINDOW_WIDTH, WINDOW_HEIGHT,
        nullptr, nullptr, hInstance, nullptr
    );

    if (hwnd == nullptr) {
        return 1;
    }

    g_connected = ConnectToDs3(g_conn);
    RenderOverlay(hwnd);
    SetTimer(hwnd, TIMER_ID, 1000, nullptr);

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
