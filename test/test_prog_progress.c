// SPDX-License-Identifier: MIT
// Host unit test for prog_apply_progress() (src/prog_progress.h) — the pure
// mapping from a write/verify engine progress event onto the AppState view
// model. Regression guard for the bug where the write/verify progress screen
// showed "Device ID: 0000" because the DEVID (read during ICSP entry) was not
// copied into the model until the operation finished. The surrounding worker
// (threads/ICSP/storage) is out of host-test scope; only the mapping is tested.
#include "tap.h"
#include <stdint.h>
#include <string.h>

#include "prog_progress.h"

int
main (void)
{
    // Regression: starting from a zeroed model (device_id == 0, as on the very
    // first progress tick), the passed-in DEVID must land in the model. This is
    // exactly the "shows 0000" bug.
    {
        AppState st;
        memset(&st, 0, sizeof(st));
        prog_apply_progress(&st, ProgStageVerify, 0, 1000, 0x1F23);
        CHECK(st.device_id == 0x1F23,
              "device_id surfaced on the first tick (was stuck at 0000)");
        CHECK(st.phase == StWriting, "phase set to StWriting");
        CHECK(st.bytes_done == 0 && st.bytes_total == 1000,
              "byte counters carried through");
        CHECK(strcmp(st.stage, "Verifying") == 0,
              "ProgStageVerify -> \"Verifying\"");
    }

    // Stage-name mapping for the other two stages.
    {
        AppState st;
        memset(&st, 0, sizeof(st));
        prog_apply_progress(&st, ProgStageErase, 10, 20, 0x1F23);
        CHECK(strcmp(st.stage, "Erasing") == 0,
              "ProgStageErase -> \"Erasing\"");
        CHECK(st.bytes_done == 10 && st.bytes_total == 20,
              "erase counters carried through");
    }
    {
        AppState st;
        memset(&st, 0, sizeof(st));
        prog_apply_progress(&st, ProgStageWrite, 512, 4096, 0x1F23);
        CHECK(strcmp(st.stage, "Writing") == 0,
              "ProgStageWrite -> \"Writing\"");
    }

    // device_id tracks the request across successive ticks (not latched once).
    {
        AppState st;
        memset(&st, 0, sizeof(st));
        prog_apply_progress(&st, ProgStageWrite, 0, 100, 0x0000);
        CHECK(st.device_id == 0x0000, "tick with DEVID 0 shows 0");
        prog_apply_progress(&st, ProgStageWrite, 50, 100, 0x1F23);
        CHECK(st.device_id == 0x1F23, "later tick reflects updated DEVID");
    }

    return tap_done("prog_progress");
}
