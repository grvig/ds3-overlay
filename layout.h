// How the overlay arranges its lines on screen. Deliberately free of any
// Windows drawing calls so tests.cpp can check the arithmetic directly,
// against the same code the overlay actually runs.
#pragma once

// When the list is too tall for the screen it's split into side-by-side
// columns rather than running off the bottom. The overlay is click-through,
// so anything off-screen would be unreachable - there is nothing to scroll.
struct ColumnLayout {
    int columns;
    int linesPerColumn;
};

inline ColumnLayout PlanColumns(int lineCount, int lineHeight, int verticalPadding, int screenHeight) {
    ColumnLayout layout = { 1, lineCount };
    if (lineCount <= 0 || lineHeight <= 0) {
        layout.columns = 1;
        layout.linesPerColumn = (lineCount > 0) ? lineCount : 1;
        return layout;
    }

    int usableHeight = screenHeight - verticalPadding;
    int fitPerColumn = usableHeight / lineHeight;
    if (fitPerColumn < 1) {
        fitPerColumn = 1; // pathological screen size; still make progress
    }

    if (lineCount <= fitPerColumn) {
        return layout; // one column is enough
    }

    layout.columns = (lineCount + fitPerColumn - 1) / fitPerColumn;
    // Spread the lines evenly instead of filling the early columns to the
    // brim and leaving a stub at the end.
    layout.linesPerColumn = (lineCount + layout.columns - 1) / layout.columns;
    return layout;
}
