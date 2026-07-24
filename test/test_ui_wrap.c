// SPDX-License-Identifier: MIT
// Host unit test for ui_wrap_hard() (src/ui_wrap.h) — the hard character-wrap
// used by the Done/Error screens so long, space-less filenames don't run off
// the 128 px display. Regression guard: a 32-char timestamped dump filename on
// one centered line overflowed the screen; every wrapped line must now measure
// within the width budget, and no characters may be lost or reordered.
// canvas_string_width is modeled as 6 px/char so wrap points are deterministic.
#include "tap.h"
#include <stdint.h>
#include <string.h>

#include "ui_wrap.h"

static uint16_t
measure6 (void *ctx, const char *s)
{
    (void)ctx;
    return (uint16_t)(strlen(s) * 6);
}

// Join produced lines back into one string, to assert nothing is lost/dup'd.
static void
join (char lines[][UI_WRAP_LINE_CAP], int n, char *out)
{
    out[0] = '\0';
    for (int i = 0; i < n; i++)
    {
        strcat(out, lines[i]);
    }
}

int
main (void)
{
    char lines[UI_WRAP_MAX_LINES][UI_WRAP_LINE_CAP];
    char joined[128];

    // The actual offending filename. maxw=120 => 20 chars per line at 6 px.
    {
        const char *fn = "pic_1F23_2026-07-12_16-38-02.bin"; // 32 chars
        int n = ui_wrap_hard(fn, 120, measure6, NULL, lines, UI_WRAP_MAX_LINES);
        CHECK(n == 2, "32-char filename wraps onto 2 lines");
        CHECK(strcmp(lines[0], "pic_1F23_2026-07-12_") == 0,
              "line 1 packs the max chars that fit");
        CHECK(strcmp(lines[1], "16-38-02.bin") == 0, "line 2 holds the rest");
        // The regression: no line may exceed the width budget.
        int over = 0;
        for (int i = 0; i < n; i++)
        {
            if ((int)measure6(NULL, lines[i]) > 120)
            {
                over = 1;
            }
        }
        CHECK(!over, "no wrapped line exceeds the width budget");
        join(lines, n, joined);
        CHECK(strcmp(joined, fn) == 0,
              "every character preserved, in order (no loss/dup)");
    }

    // A short string stays on one line, unchanged.
    {
        int n = ui_wrap_hard("Verified OK", 120, measure6, NULL, lines,
                             UI_WRAP_MAX_LINES);
        CHECK(n == 1 && strcmp(lines[0], "Verified OK") == 0,
              "short string is not wrapped");
    }

    // Guaranteed progress: a maxw narrower than one glyph still terminates,
    // emitting one char per line (no infinite loop / no dropped chars).
    {
        int n =
            ui_wrap_hard("abc", 1, measure6, NULL, lines, UI_WRAP_MAX_LINES);
        CHECK(n == 3 && strcmp(lines[0], "a") == 0 &&
                  strcmp(lines[1], "b") == 0 && strcmp(lines[2], "c") == 0,
              "glyph wider than maxw still advances one char per line");
    }

    return tap_done("ui_wrap");
}
