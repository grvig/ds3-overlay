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
#include "tracked.h"

Ds3Connection g_conn;
bool g_connected = false;
std::vector<char> g_bossDefeated;
std::vector<char> g_bonfireLit;
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
// Text size comes from the settings file; line spacing and indents are
// derived from it so the whole overlay scales together.
int g_fontHeight = 15;
int g_lineHeight = 20;

void ApplyFontSize(int fontSize) {
    g_fontHeight = fontSize;
    // A third again on top of the text height keeps lines from touching.
    g_lineHeight = fontSize + fontSize / 3;
}

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

// The size of the monitor the overlay is currently on. GetSystemMetrics only
// ever describes the primary monitor, so on a second screen it would report
// the wrong size and the overlay would be laid out for the wrong display.
RECT GetCurrentMonitorRect(HWND hwnd) {
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (monitor != nullptr && GetMonitorInfo(monitor, &info)) {
        return info.rcWork; // work area, so the taskbar isn't covered
    }

    RECT fallback = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    return fallback;
}

HFONT CreateOverlayFont() {
    return CreateFont(
        g_fontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
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

    // Headers carry a progress suffix, so measure them with the widest one
    // that could appear rather than the bare group name.
    const std::wstring widestSuffix = L"  00/00";

    int widest = 0;
    const std::vector<TrackedEntry>* allLists[] = { &g_tracked.bosses, &g_tracked.bonfires };
    for (auto* list : allLists) {
        std::wstring lastGroup;
        bool haveGroup = false;
        for (size_t i = 0; i < list->size(); i++) {
            const TrackedEntry& entry = (*list)[i];
            if (!haveGroup || entry.group != lastGroup) {
                lastGroup = entry.group;
                haveGroup = true;
                std::wstring header = lastGroup + widestSuffix;
                widest = std::max(widest, MeasureLineWidth(measureDC, header.c_str(), PAD_LEFT));
            }
            widest = std::max(widest, MeasureLineWidth(measureDC, entry.name.c_str(), BOSS_INDENT));
        }
    }

    // The status lines can be wider than any name, so measure them too. Use
    // worst-case stand-ins rather than the live values, so the column width
    // doesn't change as the numbers do.
    widest = std::max(widest, MeasureLineWidth(measureDC, L"Waiting for Dark Souls III...", PAD_LEFT));
    widest = std::max(widest, MeasureLineWidth(measureDC, L"Found game, connecting in 00s...", PAD_LEFT));
    widest = std::max(widest, MeasureLineWidth(measureDC, L"Bosses Defeated: 00 / 00", PAD_LEFT));
    widest = std::max(widest, MeasureLineWidth(measureDC, L"Bonfires Lit: 00 / 00", PAD_LEFT));
    widest = std::max(widest, MeasureLineWidth(measureDC, L"Completion: 100%  (000/000)", PAD_LEFT));
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
// each time the group changes. Each header carries that group's progress, so
// "which area am I still missing something in" is answerable at a glance
// instead of by counting down the list.
void AppendGroupedLines(std::vector<OverlayLine>& lines,
                        const std::vector<TrackedEntry>& list,
                        const std::vector<char>& done) {
    size_t i = 0;
    while (i < list.size()) {
        const std::wstring& group = list[i].group;

        // Look ahead over this group to total it up before writing the
        // header, since the header shows the group's own progress.
        size_t groupSize = 0;
        int groupDone = 0;
        for (size_t j = i; j < list.size() && list[j].group == group; j++) {
            groupSize++;
            if (j < done.size() && done[j]) {
                groupDone++;
            }
        }

        std::wstring header = group + L"  " + std::to_wstring(groupDone)
                            + L"/" + std::to_wstring(groupSize);
        // A finished group is worth spotting immediately.
        lines.push_back({ header, (groupDone == (int)groupSize) ? COLOR_DONE : COLOR_HEADER, PAD_LEFT });

        for (size_t j = 0; j < groupSize; j++) {
            size_t index = i + j;
            bool isDone = index < done.size() && done[index];
            lines.push_back({ list[index].name, isDone ? COLOR_DONE : COLOR_NOT_DONE, BOSS_INDENT });
        }
        i += groupSize;
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

    int bossCount = (int)g_tracked.bosses.size();
    int bonfireCount = (int)g_tracked.bonfires.size();

    int defeatedCount = 0;
    for (size_t i = 0; i < g_bossDefeated.size(); i++) {
        if (g_bossDefeated[i]) {
            defeatedCount++;
        }
    }
    int litCount = 0;
    for (size_t i = 0; i < g_bonfireLit.size(); i++) {
        if (g_bonfireLit[i]) {
            litCount++;
        }
    }

    // Overall completion across everything being tracked - the headline
    // number for a completionist run. Only counts sections that are actually
    // shown, so hiding one doesn't leave a percentage that can never reach
    // 100%.
    int totalTracked = 0;
    int totalDone = 0;
    if (g_settings.showBosses) {
        totalTracked += bossCount;
        totalDone += defeatedCount;
    }
    if (g_settings.showBonfires) {
        totalTracked += bonfireCount;
        totalDone += litCount;
    }
    if (totalTracked > 0) {
        int percent = (totalDone * 100) / totalTracked;
        lines.push_back({ L"Completion: " + std::to_wstring(percent) + L"%  ("
                          + std::to_wstring(totalDone) + L"/" + std::to_wstring(totalTracked) + L")",
                          (totalDone == totalTracked) ? COLOR_DONE : COLOR_SUMMARY, PAD_LEFT });
    }

    if (g_settings.showSouls) {
        lines.push_back({ g_soulsAvailable ? L"Souls: " + std::to_wstring(g_souls) : L"Souls: --",
                          COLOR_NOT_DONE, PAD_LEFT });
    }

    if (g_settings.showBosses) {
        lines.push_back({ L"Bosses Defeated: " + std::to_wstring(defeatedCount) + L" / " + std::to_wstring(bossCount),
                          COLOR_SUMMARY, PAD_LEFT });
        AppendGroupedLines(lines, g_tracked.bosses, g_bossDefeated);
    }

    if (g_settings.showBonfires) {
        lines.push_back({ L"Bonfires Lit: " + std::to_wstring(litCount) + L" / " + std::to_wstring(bonfireCount),
                          COLOR_SUMMARY, PAD_LEFT });
        AppendGroupedLines(lines, g_tracked.bonfires, g_bonfireLit);
    }

    // With everything switched off there'd be nothing to draw and the window
    // would collapse to nothing, which looks like a crash. Say so instead.
    if (lines.empty()) {
        lines.push_back({ L"(all sections hidden)", COLOR_SUMMARY, PAD_LEFT });
    }

    return lines;
}

// Stands in for the game when running with --demo: marks roughly the first
// half of each list as done so the layout can be seen at a realistic size.
void FillDemoData() {
    g_bossDefeated.assign(g_tracked.bosses.size(), 0);
    for (size_t i = 0; i < g_bossDefeated.size(); i++) {
        g_bossDefeated[i] = (i < g_bossDefeated.size() / 2) ? 1 : 0;
    }
    g_bonfireLit.assign(g_tracked.bonfires.size(), 0);
    for (size_t i = 0; i < g_bonfireLit.size(); i++) {
        g_bonfireLit[i] = (i < g_bonfireLit.size() / 2) ? 1 : 0;
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
    RECT monitor = GetCurrentMonitorRect(hwnd);
    int heightBudget = (monitor.bottom - monitor.top) - 2 * SCREEN_MARGIN;
    int widthBudget = (monitor.right - monitor.left) - 2 * SCREEN_MARGIN;
    int maxColumns = (g_columnWidth > 0) ? (widthBudget / g_columnWidth) : 1;

    ColumnLayout layout = PlanColumns((int)lines.size(), g_lineHeight,
                                      PAD_TOP + PAD_BOTTOM, heightBudget, maxColumns);

    // If some lines had nowhere to go, replace the last visible line with a
    // note saying so - better than letting them fall off the screen unseen.
    if (layout.droppedLines > 0) {
        size_t visible = (size_t)layout.columns * layout.linesPerColumn;
        if (visible > 0 && visible <= lines.size()) {
            lines.resize(visible);
            lines.back() = { L"(+" + std::to_wstring(layout.droppedLines + 1)
                             + L" more - lower fontSize)", COLOR_SUMMARY, PAD_LEFT };
        }
    }
    g_columnCount = layout.columns;
    g_linesPerColumn = layout.linesPerColumn;
    g_windowWidth = g_columnWidth * g_columnCount;
    g_windowHeight = PAD_TOP + PAD_BOTTOM + g_linesPerColumn * g_lineHeight;

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

        int y = PAD_TOP + row * g_lineHeight;
        RECT lineRect = { columnLeft + lines[i].indent, y,
                          columnLeft + g_columnWidth - PAD_RIGHT, y + g_lineHeight };

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
    // Corners of whichever monitor the overlay is on, not always the primary
    // one, so this still works on a second screen.
    RECT monitor = GetCurrentMonitorRect(hwnd);

    int left = monitor.left + MARGIN;
    int right = monitor.right - g_windowWidth - MARGIN;
    int top = monitor.top + MARGIN;
    int bottom = monitor.bottom - g_windowHeight - MARGIN;

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
                // Both lists go out in one batch so the game is interrupted
                // once per tick rather than once per list.
                std::vector<uint32_t> flagIds = FlagsOf(g_tracked.bosses);
                std::vector<uint32_t> bonfireIds = FlagsOf(g_tracked.bonfires);
                flagIds.insert(flagIds.end(), bonfireIds.begin(), bonfireIds.end());

                std::vector<uint8_t> flags = ReadFlags(g_conn, flagIds);

                size_t bossCount = g_tracked.bosses.size();
                g_bossDefeated.assign(bossCount, 0);
                for (size_t i = 0; i < bossCount && i < flags.size(); i++) {
                    g_bossDefeated[i] = flags[i] ? 1 : 0;
                }
                g_bonfireLit.assign(g_tracked.bonfires.size(), 0);
                for (size_t i = 0; i < g_bonfireLit.size() && bossCount + i < flags.size(); i++) {
                    g_bonfireLit[i] = flags[bossCount + i] ? 1 : 0;
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

    // Data first - everything else is sized from what's in the lists.
    LoadTrackedLists();
    if (g_tracked.AnyProblems()) {
        // Bad data would otherwise show up as silently missing entries, so
        // say so plainly and let the user fix the file.
        std::string message = "Problems in the data files:\n\n";
        for (size_t i = 0; i < g_tracked.problems.size() && i < 20; i++) {
            message += "  " + g_tracked.problems[i] + "\n";
        }
        message += "\nThe overlay will run with whatever loaded correctly.";
        MessageBoxA(nullptr, message.c_str(), "DS3 Overlay", MB_OK | MB_ICONWARNING);
    }

    if (lpCmdLine != nullptr && strstr(lpCmdLine, "--demo") != nullptr) {
        g_demoMode = true;
        FillDemoData();
    }

    // Settings first: the font size decides every other measurement.
    g_settings = LoadSettings();
    ApplyFontSize(g_settings.fontSize);

    // A column is as wide as the longest line that can appear in it. The
    // window is this wide times however many columns the layout ends up
    // needing; RenderOverlay resizes it to fit on the first frame.
    g_columnWidth = MeasureRequiredWidth();
    g_windowWidth = g_columnWidth;
    g_windowHeight = g_lineHeight + PAD_TOP + PAD_BOTTOM;

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
