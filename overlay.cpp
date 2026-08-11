#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <string>
#include <cstring>
#include <algorithm>
#include <vector>
#include "ds3reader.h"
#include "settings.h"
#include "layout.h"

Ds3Connection g_conn;
bool g_connected = false;
bool g_bossDefeated[BOSS_COUNT] = {};
bool g_bonfireLit[BONFIRE_COUNT] = {};
uint32_t g_souls = 0;
bool g_soulsAvailable = false;

// Demo mode (overlay.exe --demo) fills in made-up progress so the layout can
// be checked without launching the game. It never touches the game process.
bool g_demoMode = false;

// How long to wait after first spotting the game process before we actually
// attach to it and inject code. Attaching the instant the process appears
// risks catching the game mid-launch (before its own startup has settled),
// which seems to have caused it to fail to start on one occasion - this
// grace period gives it room to finish starting up first.
DWORD g_firstSeenTick = 0;
const DWORD STARTUP_GRACE_MS = 8000;

const UINT_PTR TIMER_ID = 1;
const int HOTKEY_TOGGLE_ID = 1;
const int HOTKEY_QUIT_ID = 2;
const int HOTKEY_MOVE_ID = 3;
bool g_visible = true;
OverlaySettings g_settings;
const int LINE_HEIGHT = 20;
const int SUMMARY_HEIGHT = LINE_HEIGHT + 10;
const int FONT_HEIGHT = 15;

// Layout margins. Boss names sit slightly further in than section headers
// so the grouping reads clearly.
const int PAD_LEFT = 20;
const int PAD_RIGHT = 20;
const int PAD_TOP = 20;
const int PAD_BOTTOM = 20;
const int BOSS_INDENT = 30;

// Gap kept between the overlay and the edges of the screen. The layout has
// to allow for this at the top AND the bottom: the window doesn't start at
// y=0, so budgeting the full screen height would push the last line off the
// bottom by however far down the window starts.
const int SCREEN_MARGIN = 10;

HFONT CreateOverlayFont() {
    return CreateFont(
        FONT_HEIGHT, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI"
    );
}

// Width and height are worked out at runtime from what's actually being
// shown. A fixed guess either clipped content or left dead space, and broke
// whenever the font or the tracked lists changed.
int g_windowWidth = 0;
int g_windowHeight = 0;

// When the list is too tall for the screen it's split into side-by-side
// columns rather than running off the bottom. The overlay is click-through,
// so anything off-screen would be unreachable - there's nothing to scroll.
int g_columnCount = 1;
int g_linesPerColumn = 0;
int g_columnWidth = 0;

int MeasureLineWidth(HDC dc, const wchar_t* text, int indent) {
    SIZE size = {};
    GetTextExtentPoint32(dc, text, (int)wcslen(text), &size);
    return indent + size.cx;
}

int MeasureRequiredWidth() {
    HDC screenDC = GetDC(nullptr);
    HDC measureDC = CreateCompatibleDC(screenDC);
    HFONT font = CreateOverlayFont();
    HFONT oldFont = (HFONT)SelectObject(measureDC, font);

    int widest = 0;
    const wchar_t* lastSection = nullptr;
    for (int i = 0; i < BOSS_COUNT; i++) {
        if (lastSection == nullptr || wcscmp(lastSection, BOSS_LIST[i].section) != 0) {
            lastSection = BOSS_LIST[i].section;
            widest = std::max(widest, MeasureLineWidth(measureDC, lastSection, PAD_LEFT));
        }
        widest = std::max(widest, MeasureLineWidth(measureDC, BOSS_LIST[i].name, BOSS_INDENT));
    }

    const wchar_t* lastArea = nullptr;
    for (int i = 0; i < BONFIRE_COUNT; i++) {
        if (lastArea == nullptr || wcscmp(lastArea, BONFIRE_LIST[i].area) != 0) {
            lastArea = BONFIRE_LIST[i].area;
            widest = std::max(widest, MeasureLineWidth(measureDC, lastArea, PAD_LEFT));
        }
        widest = std::max(widest, MeasureLineWidth(measureDC, BONFIRE_LIST[i].name, BOSS_INDENT));
    }

    // The status lines can be wider than any name, so measure them too. Use
    // worst-case stand-ins rather than the live values, so the column width
    // doesn't change as the numbers do.
    widest = std::max(widest, MeasureLineWidth(measureDC, L"Waiting for Dark Souls III...", PAD_LEFT));
    widest = std::max(widest, MeasureLineWidth(measureDC, L"Found game, connecting in 00s...", PAD_LEFT));
    widest = std::max(widest, MeasureLineWidth(measureDC, L"Bosses Defeated: 00 / 00", PAD_LEFT));
    widest = std::max(widest, MeasureLineWidth(measureDC, L"Bonfires Lit: 00 / 00", PAD_LEFT));
    widest = std::max(widest, MeasureLineWidth(measureDC, L"Souls: 9999999999", PAD_LEFT));

    SelectObject(measureDC, oldFont);
    DeleteObject(font);
    DeleteDC(measureDC);
    ReleaseDC(nullptr, screenDC);

    // A few pixels of slack: measuring and drawing can disagree slightly,
    // and landing exactly flush risks shaving the last pixel off the
    // longest line.
    const int SLACK = 4;
    return widest + SLACK + PAD_RIGHT;
}

// One line of text to put on screen. The frame is assembled as a list of
// these first and drawn afterwards, which keeps "what to show" separate from
// "where it goes" - needed once the list is long enough to need laying out
// in more than one column.
struct OverlayLine {
    std::wstring text;
    COLORREF color;
    int indent;
};

const COLORREF COLOR_HEADER = RGB(150, 150, 255);
const COLORREF COLOR_DONE = RGB(0, 255, 0);
const COLORREF COLOR_NOT_DONE = RGB(255, 255, 255);
const COLORREF COLOR_SUMMARY = RGB(255, 255, 0);
const COLORREF COLOR_WAITING = RGB(255, 255, 0);

// Builds the lines for one grouped list (bosses, bonfires), adding a header
// each time the group changes.
template <typename T, typename NameFn, typename GroupFn>
void AppendGroupedLines(std::vector<OverlayLine>& lines, const T* list, int count,
                        const bool* done, NameFn nameOf, GroupFn groupOf) {
    const wchar_t* lastGroup = nullptr;
    for (int i = 0; i < count; i++) {
        if (lastGroup == nullptr || wcscmp(lastGroup, groupOf(list[i])) != 0) {
            lastGroup = groupOf(list[i]);
            lines.push_back({ lastGroup, COLOR_HEADER, PAD_LEFT });
        }
        lines.push_back({ nameOf(list[i]), done[i] ? COLOR_DONE : COLOR_NOT_DONE, BOSS_INDENT });
    }
}

std::vector<OverlayLine> BuildOverlayLines() {
    std::vector<OverlayLine> lines;

    if (!g_connected) {
        std::wstring waitingText = L"Waiting for Dark Souls III...";
        if (g_firstSeenTick != 0) {
            DWORD elapsed = GetTickCount() - g_firstSeenTick;
            DWORD remainingMs = (elapsed < STARTUP_GRACE_MS) ? (STARTUP_GRACE_MS - elapsed) : 0;
            int remainingSec = (int)((remainingMs + 999) / 1000);
            waitingText = L"Found game, connecting in " + std::to_wstring(remainingSec) + L"s...";
        }
        lines.push_back({ waitingText, COLOR_WAITING, PAD_LEFT });
        return lines;
    }

    int defeatedCount = 0;
    for (int i = 0; i < BOSS_COUNT; i++) {
        if (g_bossDefeated[i]) {
            defeatedCount++;
        }
    }
    int litCount = 0;
    for (int i = 0; i < BONFIRE_COUNT; i++) {
        if (g_bonfireLit[i]) {
            litCount++;
        }
    }

    lines.push_back({ g_soulsAvailable ? L"Souls: " + std::to_wstring(g_souls) : L"Souls: --",
                      COLOR_NOT_DONE, PAD_LEFT });

    lines.push_back({ L"Bosses Defeated: " + std::to_wstring(defeatedCount) + L" / " + std::to_wstring(BOSS_COUNT),
                      COLOR_SUMMARY, PAD_LEFT });
    AppendGroupedLines(lines, BOSS_LIST, BOSS_COUNT, g_bossDefeated,
                       [](const BossInfo& b) { return b.name; },
                       [](const BossInfo& b) { return b.section; });

    lines.push_back({ L"Bonfires Lit: " + std::to_wstring(litCount) + L" / " + std::to_wstring(BONFIRE_COUNT),
                      COLOR_SUMMARY, PAD_LEFT });
    AppendGroupedLines(lines, BONFIRE_LIST, BONFIRE_COUNT, g_bonfireLit,
                       [](const BonfireInfo& b) { return b.name; },
                       [](const BonfireInfo& b) { return b.area; });

    return lines;
}

// Stands in for the game when running with --demo: marks roughly the first
// half of each list as done so the layout can be seen at a realistic size.
void FillDemoData() {
    for (int i = 0; i < BOSS_COUNT; i++) {
        g_bossDefeated[i] = (i < BOSS_COUNT / 2);
    }
    for (int i = 0; i < BONFIRE_COUNT; i++) {
        g_bonfireLit[i] = (i < BONFIRE_COUNT / 2);
    }
    g_souls = 123456;
    g_soulsAvailable = true;
    g_connected = true;
}

// Renders the current frame into a true per-pixel-transparent bitmap and
// hands it to Windows as the window's whole appearance. Unlike the old
// "treat this one color as invisible" trick, every pixel gets its own real
// transparency level, so text edges blend cleanly with whatever is behind
// the overlay instead of picking up a tint from a fake background color.
void RenderOverlay(HWND hwnd) {
    std::vector<OverlayLine> lines = BuildOverlayLines();

    // Decide the shape of this frame before drawing it. The window is
    // resized to match, so it always ends up exactly big enough.
    int heightBudget = GetSystemMetrics(SM_CYSCREEN) - 2 * SCREEN_MARGIN;
    ColumnLayout layout = PlanColumns((int)lines.size(), LINE_HEIGHT,
                                      PAD_TOP + PAD_BOTTOM, heightBudget);
    g_columnCount = layout.columns;
    g_linesPerColumn = layout.linesPerColumn;
    g_windowWidth = g_columnWidth * g_columnCount;
    g_windowHeight = PAD_TOP + PAD_BOTTOM + g_linesPerColumn * LINE_HEIGHT;

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, g_windowWidth, g_windowHeight,
                 SWP_NOMOVE | SWP_NOACTIVATE);

    HDC screenDC = GetDC(nullptr);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = g_windowWidth;
    bmi.bmiHeader.biHeight = -g_windowHeight; // negative = top-down, easier to reason about
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP bitmap = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, bitmap);

    // Start fully transparent (all zero bytes = black, 0 alpha) everywhere.
    memset(pixels, 0, g_windowWidth * g_windowHeight * 4);

    SetBkMode(memDC, TRANSPARENT);
    HFONT font = CreateOverlayFont();
    HFONT oldFont = (HFONT)SelectObject(memDC, font);

    for (size_t i = 0; i < lines.size(); i++) {
        int column = (int)i / g_linesPerColumn;
        int row = (int)i % g_linesPerColumn;
        int columnLeft = column * g_columnWidth;

        int y = PAD_TOP + row * LINE_HEIGHT;
        RECT lineRect = { columnLeft + lines[i].indent, y,
                          columnLeft + g_columnWidth - PAD_RIGHT, y + LINE_HEIGHT };

        SetTextColor(memDC, lines[i].color);
        DrawText(memDC, lines[i].text.c_str(), -1, &lineRect, DT_LEFT | DT_TOP);
    }

    // GDI only paints the color channels, not transparency. Since we started
    // from solid black, each pixel's brightness now tells us how much "ink"
    // is there - use that as its transparency (this also happens to be
    // exactly the format Windows wants for blending: color pre-multiplied
    // by transparency).
    BYTE* bytes = (BYTE*)pixels;
    for (int i = 0; i < g_windowWidth * g_windowHeight; i++) {
        BYTE b = bytes[i * 4 + 0];
        BYTE g = bytes[i * 4 + 1];
        BYTE r = bytes[i * 4 + 2];
        BYTE alpha = (b > g ? (b > r ? b : r) : (g > r ? g : r));
        bytes[i * 4 + 3] = alpha;
    }

    POINT srcPos = { 0, 0 };
    SIZE size = { g_windowWidth, g_windowHeight };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(hwnd, screenDC, nullptr, &size, memDC, &srcPos, 0, &blend, ULW_ALPHA);

    SelectObject(memDC, oldFont);
    DeleteObject(font);
    SelectObject(memDC, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

// Moves the overlay to the next screen corner, wrapping back around to the
// first. Corners are inset by the same margin the default position uses, so
// the overlay never sits flush against a screen edge.
void MoveToNextCorner(HWND hwnd) {
    const int MARGIN = SCREEN_MARGIN;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    int left = MARGIN;
    int right = screenW - g_windowWidth - MARGIN;
    int top = MARGIN;
    int bottom = screenH - g_windowHeight - MARGIN;

    // If the overlay is taller than the screen, keep it pinned to the top
    // rather than pushing its start position off-screen.
    if (bottom < top) {
        bottom = top;
    }
    if (right < left) {
        right = left;
    }

    const POINT corners[] = {
        { left,  top },
        { right, top },
        { right, bottom },
        { left,  bottom },
    };
    const int cornerCount = sizeof(corners) / sizeof(corners[0]);

    // Find which corner we're currently closest to, then step to the next.
    int nearest = 0;
    long long bestDistance = -1;
    for (int i = 0; i < cornerCount; i++) {
        long long dx = corners[i].x - g_settings.x;
        long long dy = corners[i].y - g_settings.y;
        long long distance = dx * dx + dy * dy;
        if (bestDistance < 0 || distance < bestDistance) {
            bestDistance = distance;
            nearest = i;
        }
    }

    POINT next = corners[(nearest + 1) % cornerCount];
    g_settings.x = next.x;
    g_settings.y = next.y;

    SetWindowPos(hwnd, HWND_TOPMOST, g_settings.x, g_settings.y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
    SaveSettings(g_settings);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            KillTimer(hwnd, TIMER_ID);
            UnregisterHotKey(hwnd, HOTKEY_TOGGLE_ID);
            UnregisterHotKey(hwnd, HOTKEY_QUIT_ID);
            UnregisterHotKey(hwnd, HOTKEY_MOVE_ID);
            PostQuitMessage(0);
            return 0;

        // F9 moves the overlay to the next corner, F10 shows/hides it, F11
        // closes it. These are registered as global hotkeys, so they work
        // even while the game has focus - which matters because the overlay
        // itself can't be clicked.
        case WM_HOTKEY: {
            if (wParam == HOTKEY_TOGGLE_ID) {
                g_visible = !g_visible;
                ShowWindow(hwnd, g_visible ? SW_SHOWNOACTIVATE : SW_HIDE);
            } else if (wParam == HOTKEY_QUIT_ID) {
                DestroyWindow(hwnd);
            } else if (wParam == HOTKEY_MOVE_ID) {
                MoveToNextCorner(hwnd);
            }
            return 0;
        }

        case WM_TIMER: {
            // If the game has been closed since our last check, forget the
            // old connection and go back to "waiting" instead of showing
            // stale or wrong boss info.
            if (g_demoMode) {
                // Demo mode never talks to the game; its data is already set.
                RenderOverlay(hwnd);
                return 0;
            }

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
                std::vector<uint8_t> bossFlags = ReadAllBossFlags(g_conn);
                for (int i = 0; i < BOSS_COUNT; i++) {
                    g_bossDefeated[i] = bossFlags[i];
                }
                std::vector<uint8_t> bonfireFlags = ReadAllBonfireFlags(g_conn);
                for (int i = 0; i < BONFIRE_COUNT; i++) {
                    g_bonfireLit[i] = bonfireFlags[i];
                }
                g_soulsAvailable = ReadSouls(g_conn, g_souls);
            }
            if (g_visible) {
                RenderOverlay(hwnd);
            }
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"DS3OverlayWindowClass";

    if (lpCmdLine != nullptr && strstr(lpCmdLine, "--demo") != nullptr) {
        g_demoMode = true;
        FillDemoData();
    }

    // A column is as wide as the longest line that can appear in it. The
    // window is this wide times however many columns the layout ends up
    // needing; RenderOverlay resizes it to fit on the first frame.
    g_columnWidth = MeasureRequiredWidth();
    g_windowWidth = g_columnWidth;
    g_windowHeight = LINE_HEIGHT + PAD_TOP + PAD_BOTTOM;
    g_settings = LoadSettings();

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
        g_settings.x, g_settings.y, g_windowWidth, g_windowHeight,
        nullptr, nullptr, hInstance, nullptr
    );

    if (hwnd == nullptr) {
        return 1;
    }

    // Global hotkeys, so they work while the game has focus. If another
    // program has already claimed F10/F11, registration fails - not fatal,
    // the overlay just runs without them.
    RegisterHotKey(hwnd, HOTKEY_MOVE_ID, 0, VK_F9);
    RegisterHotKey(hwnd, HOTKEY_TOGGLE_ID, 0, VK_F10);
    RegisterHotKey(hwnd, HOTKEY_QUIT_ID, 0, VK_F11);

    // Deliberately no ConnectToDs3 here - the timer handles connecting, and
    // it waits out the startup grace period first. Attaching straight away
    // could catch the game mid-launch, which is what used to make it close
    // itself. Paint once now so the window shows its waiting message right
    // away rather than sitting blank until the first tick.
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
