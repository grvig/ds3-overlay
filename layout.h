// How the overlay arranges its lines on screen. Deliberately free of any
// Windows drawing calls so tests.cpp can check the arithmetic directly,
// against the same code the overlay actually runs.
#pragma once

// When the list is too tall for the screen it's split into side-by-side
// columns rather than running off the bottom. The overlay is click-through,
// so anything off-screen would be unreachable - there is nothing to scroll.
//
// Columns are also capped so the window can't run off the right-hand edge.
// If even the maximum number of columns can't hold everything, some lines
// genuinely don't fit; `droppedLines` says how many, so the overlay can say
// so rather than silently hiding them.
struct ColumnLayout {
    int columns;
    int linesPerColumn;
    int droppedLines;
};

inline ColumnLayout PlanColumns(int lineCount, int lineHeight, int verticalPadding,
                                int heightBudget, int maxColumns) {
    ColumnLayout layout = { 1, lineCount, 0 };
    if (maxColumns < 1) {
        maxColumns = 1;
    }
    if (lineCount <= 0 || lineHeight <= 0) {
        layout.columns = 1;
        layout.linesPerColumn = (lineCount > 0) ? lineCount : 1;
        return layout;
    }

    int usableHeight = heightBudget - verticalPadding;
    int fitPerColumn = usableHeight / lineHeight;
    if (fitPerColumn < 1) {
        fitPerColumn = 1; // pathological screen size; still make progress
    }

    if (lineCount <= fitPerColumn) {
        return layout; // one column is enough
    }

    int neededColumns = (lineCount + fitPerColumn - 1) / fitPerColumn;
    if (neededColumns <= maxColumns) {
        layout.columns = neededColumns;
        // Spread the lines evenly instead of filling the early columns to
        // the brim and leaving a stub at the end.
        layout.linesPerColumn = (lineCount + layout.columns - 1) / layout.columns;
        return layout;
    }

    // Out of room. Fill what we have and report the shortfall instead of
    // quietly drawing past the edge of the screen.
    layout.columns = maxColumns;
    layout.linesPerColumn = fitPerColumn;
    layout.droppedLines = lineCount - maxColumns * fitPerColumn;
    return layout;
}
