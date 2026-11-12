"""Whole-game tests (verify/frame.py): mission snapshot, determinism, snapshot files, and
original vs decompiled frames.

  python3 test_frame.py                 all checks below, 60 frames
  python3 test_frame.py -n 100          more frames
  python3 test_frame.py --only a,b      redirect only these decomp rows (default: every decomp row)
  python3 test_frame.py --exclude a,b   redirect all but these
  python3 test_frame.py --calls f,g     per-call checks of these functions inside the frames
  python3 test_frame.py --bisect        find a redirected function that makes frames differ
  python3 test_frame.py --remake        rebuild the mission snapshot first
  python3 test_frame.py --mission 5     another mission (snapshot /tmp/tn-snap/missN.snap)
  python3 test_frame.py --frames-only   skip the determinism / snapshot-file checks

Set TN_BUILD like for the other tests (frame.py reads the build's wmap.csv for redirect mode).
Exit code 0 only when everything matches.
"""
import os, sys, time, argparse, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import frame

# scancodes (set 1)
UP, LEFT, RIGHT, SPACE, CTRL, ENTER = 0x48, 0x4B, 0x4D, 0x39, 0x1D, 0x1C

def script(g, n):
    """Scripted input, the same for every run: walk forward, turn, fire, move the mouse."""
    if n == 2: g.key(UP, True)
    if n == 9: g.key(LEFT, True)
    if n == 12: g.key(LEFT, False)
    if n == 14: g.tap(CTRL)
    if n == 16: g.key(UP, False)
    if n == 18: g.mouse(200, 120)
    if n == 19: g.mouse(buttons=1)
    if n == 20: g.mouse(buttons=0)
    if n == 22: g.tap(SPACE)
    if n == 25: g.key(RIGHT, True)
    if n == 28: g.key(RIGHT, False)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-n', type=int, default=60)
    ap.add_argument('--mission', type=int, default=1)
    ap.add_argument('--only'); ap.add_argument('--exclude', default='')
    ap.add_argument('--calls', default='fix_sincos,fix_sin,MsgAlloc,MsgSend,RefGet')
    ap.add_argument('--remake', action='store_true')
    ap.add_argument('--bisect', action='store_true')
    ap.add_argument('--frames-only', action='store_true')
    a = ap.parse_args()
    fails = 0
    t = time.time()
    snap = frame.mission_snapshot(a.mission, force=a.remake)
    print('snapshot %s (%d bytes, %.1f s)' % (snap, os.path.getsize(snap), time.time() - t))

    if not a.frames_only:
        # 1. determinism: the same frames twice, nothing redirected
        r = frame.compare_frames(snap, 10, names=[], input=script, report=lambda *x: None)
        print('determinism (10 frames, nothing redirected):', 'ok' if r is None else 'DIFFERS %s' % (r,))
        fails += r is not None
        # 2. snapshot file round trip: run 5 frames, save, load into a fresh emulator; both go on
        g = frame.Game(); g.load(snap); g.input = script
        for i in range(5): g.frame()
        tmp = os.path.join(tempfile.mkdtemp(prefix='tnsnap'), 'mid.snap')
        g.save(tmp)
        h = frame.Game(root=frame.game_dir(frame.GAME_DIR + '-b')); h.load(tmp); h.input = script
        bad = None
        for i in range(5):
            g.frame(); h.frame()
            d = frame.compare_states(g, h)
            if d: bad = (i, d[:5]); break
        print('save/load round trip (5 + 5 frames):', 'ok' if bad is None else 'DIFFERS %s' % (bad,))
        fails += bad is not None
        os.remove(tmp)

    # 3. original vs decompiled frames
    names = a.only.split(',') if a.only else None
    excl = [x for x in a.exclude.split(',') if x]
    if a.bisect:
        print('culprit:', frame.bisect_redirect(snap, a.n, names=names, input=script))
        return 1
    t = time.time()
    r = frame.compare_frames(snap, a.n, names=names, exclude=excl, input=script, report=lambda *x: None)
    if r is None: print('frames: %d frames match (%.1f s)' % (a.n, time.time() - t))
    else:
        print('frames: frame %d differs:' % r[0])
        for x in r[1][:30]: print('  ' + x)
        fails += 1

    # 4. single calls inside the frames
    for f in [x for x in a.calls.split(',') if x]:
        try:
            n, bad = frame.check_calls(snap, f, frames=min(a.n, 10), k=40, input=script,
                                       report=lambda *x: None)
        except ValueError as ex:
            print('calls %s: %s' % (f, ex)); continue
        print('calls %s: %d checked, %d differ' % (f, n, len(bad)))
        for c, fn, d in bad[:3]: print('  call %d frame %d: %s' % (c, fn, '; '.join(d[:4])))
        fails += bool(bad)
    print('FAILED' if fails else 'all match')
    return 1 if fails else 0

if __name__ == '__main__':
    sys.exit(main())
