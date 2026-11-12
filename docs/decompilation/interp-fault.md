# interp-fault: C 3D path faults at 0x2F44EC / 0x2F44F5 (worker interp-fault)

## Question
g6b: C g3_interpret_object faults in xlate_rotate_point (0x2F44EC) on mission 10's frame object
(obj 0x1b87548, parms 0x396e20, first g3_interpret_object of a frame); asm runs fine.
g7a: C ffpipe_29EDB4 faults at 0x2F44EC/0x2F44F5 with the HUD eye on entities 11/12/16 (m1);
the original hud_269028 with only ffpipe_29EDB4 redirected faults the same way.

## Answer: test-harness artefact, not a C bug
The game's opcode_table (0x3605F4) / poly_variant_table (0x3605E8) are bound to the original
data (TN_LINK_ORIGINAL_DATA) and hold the ORIGINAL asm handler addresses. The asm loop enters
a handler by `jmp [table]` with ebp = bytecode. Our C loop (interp.c interpreter_loop) calls the
slot as a Watcom function. When our C interpreter runs while the handler rows are not
redirected (direct-call tests, check_calls, `redirect(names=[subset])`), it calls the asm
handlers with ebp = whatever the C had (a frame pointer): e.g. do_x_rel passes [ebp+..] garbage
to xlate_rotate_point -> fault at 0x2F44EC; do_eof (`ret`) would return pc and loop forever.
In a real all-C build (our own tables) or under full redirect (compare_frames) the slots hold /
jump to our handlers, so the game itself is not affected.

Evidence (build /tmp/tnb-interp = default build; game /tmp/tn-game-interp; snaps /tmp/tns-interp):
- m10 frame object: C without hooks faults at 0x2F44EC; with the handler hooks the C runs and
  its state equals the asm's byte for byte (diff_state: 0 differences).
- m1, eye on 11/12/16, original hud_269028 over C ffpipe_29EDB4: without hooks faults at
  0x2F44EC; with hooks same state as all-original (only g3d_light_tmp1, a stack pointer, differs;
  masked). Where the original itself faults (#DE in the projection, 0x34C452 / 0x34C357 in some
  states) the C path faults identically.
- Also seen by accident: a hook table where the handler hooks were left as no-ops reproduced the
  fault exactly -> the hooks are the whole difference.

## Fix: shared helper verify/interp_hooks.py
- `interp_hooks.install(g)` (Game or Emu): hook on every 3D handler row (symbols-3d-c.csv notes
  "handler", 72 rows: interp.c handlers, poly variants, fill.c replacement handlers). Reached
  with a return address in our code -> continue at W_handler; reached from the asm loop -> runs
  the original. `remove(g)`, `chain(*setups)` for check_calls, `mask_temps(so, sm)` / MASK for
  g3d_light_tmp1/2, g3d_tmap_plist (stack pointers).
- Install right after Game.load, before any e.redirect(); usable as
  `frame.check_calls(..., setup=interp_hooks.install)`.
- Self-test / regression: `TN_BUILD=... python3 verify/interp_hooks.py` (cases above; exit 0).
  ~6 min.
- Same logic as the copies in test_g6_a / test_g5_b (which already work); those may switch to
  the module (no behaviour change).

## Tests that should use it (C reaching the 3D interpreter without the handler rows redirected)
- test_g6_b.py: install in T.__init__; the skel group's routing of C g3_interpret_object /
  sub_object to asm (to_asm/BACK trampoline) can then go; g_calls -> setup=interp_hooks.install.
- test_g7_a.py: install in T.__init__ (before the callee-redirect fallback), calls/frames
  setup; the "original faults too over the other workers' C" skips on hud_269028 with
  ffpipe_29EDB4 go away (they were this artefact).
- test_g7_c.py (dmgdisp draws through ffpipe_29EDB4), test_g6_c.py and test_g8_a.py if their C
  draws objects / views: add it to direct calls and check_calls setup (harmless otherwise).
- test_frame.py check_calls: pass setup=interp_hooks.install for render/HUD functions.
- Better long-term (merge decision, shared file): call install() in frame.Game.load so every test
  gets it.
- Not needed: compare_frames with every row redirected; test_3d_c (own tables / no game state).

No change to src/: interp.c, xlate and ffpipe.c are correct for this case.

## Check with an existing test
test_g7_a.py `view` group (m1, TN_G7A_N=0.3) with interp_hooks.install monkeypatched into
T.__init__ (/tmp/interp-fault/g7a_view.py): 725 match, 0 differ, 13 skipped (the original
faults), and no "original faults too over the other workers' C" skips any more.
