#!/usr/bin/env python3
"""Write data-free generated inputs for the public CI AppImage build.

These placeholders exist only to compile and package the public launcher.
They are not usable game data; a runtime generated from the user's own game
files is required to start the game.
"""
from pathlib import Path
import sys


out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)

(out / "dm_hud_exp.h").write_text("""#pragma once
#include <stdint.h>
static const uint32_t k_exp[100] = {0};
""")

(out / "dm_vault_tab.h").write_text("""#pragma once
#define VT_NPART 11
static const char *const vt_part_name[VT_NPART] = {""};
typedef struct { uint8_t kind, part, tier; uint16_t row, lvl; int16_t set; const char *key, *icon; } vt_row;
static const vt_row vt_rows[1] = {{0}};
#define VT_NROW 0
static const char *const vt_set_key[1] = {""};
#define VT_NSET 0
""")

(out / "dm_bright_levels.inc").write_text("{ 0, 0, 0, -1 },\n")

(out / "dm_touch_native_art.c").write_text("""#include "dm_touch_native_art.h"
#include <stddef.h>
const dm_ta_atlas dm_ta_atlases[DM_TA_ATLASES] = {{0}};
const char dm_ta_manifest_sha[] = "";
const char dm_ta_geometry_sha[] = "";
const char dm_ta_binding_sha[] = "";
static dm_ta_glyph empty_glyph(uint32_t id) {
    return (dm_ta_glyph){DM_TA_UNKNOWN,-1,-1,-1,-1,-1,id,"CI placeholder"};
}
dm_ta_glyph dm_ta_skill(uint32_t id) { return empty_glyph(id); }
dm_ta_glyph dm_ta_current(const dm_tg_snapshot *s,uint64_t b,uint64_t n) {
    (void)s; (void)b; (void)n; return empty_glyph(UINT32_MAX);
}
dm_ta_glyph dm_ta_current_set(const dm_tg_snapshot *s,uint64_t b,uint64_t n,int set) {
    (void)s; (void)b; (void)n; (void)set; return empty_glyph(UINT32_MAX);
}
int dm_ta_frame_at(int atlas,int frame,dm_ta_frame *out) {
    (void)atlas; (void)frame; (void)out; return 0;
}
""")
