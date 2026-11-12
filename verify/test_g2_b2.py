"""Differential test: worker g2b2 (batch G2), original code vs decompiled C for module M053
(src/game/objects/objdamage.c): object damage, kill credit, the delayed death list.

Uses the harness of test_g2_b.py (class T: cmp / fake / logger / world).  Each case starts from
one snapshot and runs our C (W_name thunk) and then the original with the same registers /
stack args; it compares eax (masked to the return width), the esp delta, every byte of state
(diff_state) and a log of the faked callees.  Inputs on which the original faults are skipped.

World: random entities of every class with damage records that make sense (parts {cur, max},
subsystems {max, cur}, armour, weapons) written into the PBA / vehicle / class 3 / class 4
tables, the per-class hit tables, the death list and the mode globals.  Real code: RndRange and
the LG rnd stream, the message pool (MsgAlloc / MsgSend, pool set up by the original
MsgPoolInit), the AI message builders (aiacc_*), fix_fast_pyth_dist and the RES system
(static_2863B8 reads a copy of the game's RESSIM.RES).  Fakes (logging, hooked at the original
address and at our C for decompiled callees): sound, radio, statistics, sims, HUD, the armour
meter (objacc_28B938), weapon_use_ammo, fire_292C2C, obj55_28927C, damage_27D540/27E064.
Usage: test_g2_b2.py [group ...]   groups: init records damage credit deaths misc
       TN_G2B2_N scales the random case counts (default 1).
Exit code 0 = everything matches.
"""
import os, sys, struct, shutil
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_g2_b as base
from test_g2_b import T, u, s8, s16, s32, ENTS, ESZ, PBA, PSZ, VEH, VSZ, CLS, GAME, MISSION, NENT, GAME_TIME, PLAYER

N = float(os.environ.get('TN_G2B2_N', '1'))
def n(k): return max(1, int(k * N))

A = dict(
    static_2863B8=0x2863B8, static_286488=0x286488, objdmg_rand_amount=0x28648C,
    objdmg_systems=0x2864D8, static_286794=0x286794, objdmg_status_str=0x286994,
    objdmg_apply=0x286B10, objdmg_get_hp=0x286B94, objdmg_drain=0x286BE0, objdmg_mark=0x286C7C,
    obj_owner=0x286D38, objdmg_snd_done=0x286D9C, objdmg_credit=0x286DC8,
    objdmg_hit_sound=0x287110, static_287168=0x287168, obj_damage=0x287218,
    obj_destroy=0x287E5C, objdmg_death_time=0x287F3C, objdmg_death_add=0x287F9C,
    static_288040=0x288040, static_28808C=0x28808C, objdmg_alert=0x2880EC,
    objdmg_repair_all=0x288130,
)
base.A.update(A)

DEATHS, DCOUNT = 0x396A10, 0x396E1C


class T2(T):
    def __init__(self):
        T.__init__(self)
        self.cov = {}

    def covinc(self, k, v=1):
        self.cov[k] = self.cov.get(k, 0) + v

    # ---------------- fakes
    def fakes(self):
        e, fr = self.e, self.frnd
        if getattr(self, 'faked', False): return
        self.faked = True
        B = lambda a=None: fr.getrandbits(32)
        self.logger('damage_27E064', 0x27E064, 1)
        self.dmgp = e.alloc(16)
        def d540(a):
            e.log.append(('damage_27D540', a[0] & 0xFFFF))
            if fr.random() < 0.3: return 0
            e.w32(self.dmgp, u(e.u32(GAME_TIME) + fr.choice([-20000, -1, 0, 1, 5000])) if fr.random() < 0.8 else B())
            return self.dmgp
        self.fake('damage_27D540', 0x27D540, d540, 1)
        self.logger('mfddata_2665E8', 0x2665E8, 1, mask=(0xFFFF,),
                    ret=lambda a: fr.choice([0, 1, 3, 7, 8, 31, 0xFF, 0x80]) | fr.getrandbits(8) << 8 if fr.random() < 0.9 else B())
        self.logger('mstat_2C6FD8', 0x2C6FD8, 1, mask=(0xFFFF,))
        self.logger('mstat_2C701C', 0x2C701C, 2, mask=(0xFFFF, 0xFFFF))
        self.logger('flash_28E040', 0x28E040, 4, mask=(0xFFFF, 0xFFFF, ~0, ~0))
        self.logger('flash_28E350', 0x28E350, 2, mask=(0xFFFF, ~0), ret=B)
        self.logger('fire_292C2C', 0x292C2C, 2, mask=(0xFFFF, 0xFFFF))
        self.logger('objacc_28B938', 0x28B938, 2, ret=lambda a: fr.choice([0, 0, 1, 2, 0x100, 0xFF]) | fr.getrandbits(8) << 8)
        self.logger('hudview_26C5F4', 0x26C5F4, 1)
        self.logger('helm_262A88', 0x262A88, 1, mask=(0xFFFF,))
        self.logger('sfx_28C300', 0x28C300, 1, mask=(0xFFFF,), ret=B)
        self.logger('sfx_28CB24', 0x28CB24, 2, mask=(~0, 0xFFFF))
        self.logger('simtype_27B450', 0x27B450, 1, mask=(0xFFFF,))
        self.logger('pbasim_28A140', 0x28A140, 2, mask=(0xFFFF, ~0))
        self.logger('vehsim_28EAA0', 0x28EAA0, 2, mask=(0xFFFF, ~0))
        self.logger('aircraft_2844F4', 0x2844F4, 1, mask=(0xFFFF,))
        self.logger('instsim_288554', 0x288554, 2, mask=(0xFFFF, ~0))
        self.logger('RadioAck', 0x2CEC20, 1)
        self.logger('RadioMessage', 0x2CEA24, 1)
        self.logger('SndPlay', 0x247B60, 7, mask=(0xFFFF, ~0, ~0, ~0, ~0, ~0, ~0), ret=lambda a: fr.choice([-1, 0, 3]))
        self.logger('snd_chan_add_callback', 0x24733C, 3, ptr=(1,), ret=B)
        self.logger('snd_play_hit', 0x248FBC, 3, mask=(~0, ~0, 0xFF))
        self.logger('obj55_28927C', 0x28927C, 1, mask=(0xFFFF,), ret=lambda a: fr.choice([0, 0, 0, 1]) | fr.getrandbits(8) << 8)
        self.logger('weapon_use_ammo', 0x28F950, 3, mask=(0xFFFF, ~0, ~0), ret=lambda a: fr.choice([0, 0, 1, 3]) | fr.getrandbits(8) << 8)

    # ---------------- world
    def part(self):
        r = self.rnd
        mx = r.choice([1, 7, 8, 10, 100, 250, 1000, 3000, 0x7FFF, -5]) if r.random() < 0.5 else r.randrange(1, 2000)
        cur = r.choice([0, mx, mx // 2, 1, (mx * 73) // 100, (mx * 37) // 100, (mx * 73) // 100 + 1]) if r.random() < 0.5 else r.randrange(0, max(1, mx) + 1)
        f4 = r.choice([0, 5, 100, cur]) if r.random() < 0.7 else r.getrandbits(15)
        f6 = r.choice([0, 0, 5, 30, 100, 0xFF])
        return struct.pack('<hhhB', cur, mx, f4, f6)

    def sysrec(self):
        r = self.rnd
        mx = r.choice([8, 9, 16, 100, 1000, 4000, 0xFFFF]) if r.random() < 0.6 else r.randrange(8, 3000)
        if r.random() < 0.02: mx = r.choice([0, 3, 7])
        cur = r.choice([0, mx, mx // 2, 1, mx * 40 // 100, mx * 75 // 100, 7, 8]) if r.random() < 0.5 else r.randrange(0, mx + 1)
        return struct.pack('<BHH', r.getrandbits(8), mx, cur & 0xFFFF)

    def weapons(self, a, k):
        r = self.rnd
        for j in range(k):
            self.e.wr(a + 15 * j, bytes([r.randrange(0x30), r.choice([0, 1, 2, 3, 4, 7])]) + bytes(r.getrandbits(8) for _ in range(13)))

    def records(self, cls, b):
        e, r = self.e, self.rnd
        if cls == 1:
            for k in range(4): e.wr(b + 0x15 + 7 * k, self.part())
            ns = r.choice([0, 1, 2, 4, 6, 8])
            sysb = e.alloc(5 * 21)
            for k in range(21): e.wr(sysb + 5 * k, self.sysrec())
            e.w32(b + 0x9C, sysb if r.random() < 0.97 else 0)
            e.w16(b + 0xA0, ns if r.random() < 0.9 else r.randrange(9, 21))
            self.weapons(b + 0x45, 4)
            e.wr(b + 0x81, bytes([r.choice([0, 1, 2, 3, 4, 0xFF])]))
            e.wr(b + 0x86 + 0x14, bytes([r.choice([0, 1, 2, 3, 7, 8, 0x21])]))
            e.w32(b + 0xD, r.choice([8, 8, 2, 5, 0]))
            e.wr(b + 0x11, bytes([r.choice([0, 1])]))
        elif cls == 2:
            e.wr(b + 6, self.part())
            e.wr(b + 0xD, self.sysrec()); e.wr(b + 0x12, self.sysrec())
            self.weapons(b + 0x17, 2)
            e.wr(b + 0x35, bytes([r.choice([0, 1, 2, 0xFF])]))
            e.wr(b + 0x37 + 0x14, bytes([r.choice([0, 1, 2, 3])]))
        elif cls == 3:
            for k in range(3): e.wr(b + 6 + 7 * k, self.part())
            for k in range(3): e.wr(b + 0x1B + 5 * k, self.sysrec())
            self.weapons(b + 0x2A, 2)
            e.wr(b + 0x4A, bytes([r.choice([0, 1, 2])]))
        elif cls == 4:
            np = r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 0xFF, 0x80])
            for k in range(min(np, 8)): e.wr(b + 0x10 + 7 * k, self.part())
            e.wr(b + 0x51, self.sysrec())
            self.weapons(b + 0x33, 2)
            e.wr(b + 0x56 + 0x14, bytes([r.choice([0, 1, 2, 4])]))
            e.wr(b + 0xC, bytes([np, r.choice([0, 1, 2, 0xFF])]))

    def dmg_world(self):
        e, r = self.e, self.rnd
        self.world(classes=[0, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 6, 7, 8, 8])
        done = set()
        for i in self.ents:
            cls = e.rd(ENTS + i * ESZ + 1, 1)[0]
            slot = s16(e.u16(ENTS + i * ESZ + 3))
            if cls in (1, 2, 3, 4) and 0 <= slot < CLS[cls][2] and (cls, slot) not in done:
                done.add((cls, slot))
                self.records(cls, CLS[cls][0] + slot * CLS[cls][1])
            if r.random() < 0.8: e.wr(ENTS + i * ESZ + 0x21, bytes([r.choice([0, 1, 2, 3, 5, 11])]))
            e.wr(ENTS + i * ESZ + 0x23, bytes([r.choice([0, 1, 2, 3, 4, 5])]))
            e.wr(ENTS + i * ESZ + 2, bytes([r.choice([0, 1, 2, 4, 7, 0xC, 0xD, 0xE, 0x10, 0x17, 3])]))
        # owners of class 5 / 8 objects, class 8 "credited" flags
        for c in (5, 8):
            bb, sz, cnt = CLS[c]
            for sl in range(12):
                e.w16(bb + sl * sz + 6, r.choice(self.ents + [0]))
                if c == 8: e.wr(bb + sl * sz + 0xA, bytes([r.choice([0, 0, 1])]))
        # player's squad list (6-byte records, 0-terminated) for objdmg_credit
        lst = e.alloc(6 * 6)
        k = r.choice([0, 1, 2, 4])
        for j in range(k): e.wr(lst + 6 * j, struct.pack('<h', r.choice(self.ents)) + bytes(4))
        e.wr(lst + 6 * k, bytes(6))
        for s in range(12): e.w32(PBA + s * PSZ + 0xBE, lst)
        e.w32(PBA + self.pslot * PSZ + 0xBE, lst)
        e.w32(PBA + self.pslot * PSZ + 0xD, r.choice([2, 5, 8, 0]))
        # hit tables, modes, counters
        tab = bytes(r.choice([0, 0, 0xFF, 1, 5, 20, 40, 60]) if r.random() < 0.8 else r.getrandbits(8) for _ in range(0x120))
        e.wr(0x3968F0, tab)
        e.wr(0x35B72F, bytes([1, 4, 1, 3, 5, 1, 1, 1, 1]) if r.random() < 0.8 else bytes(r.randrange(0, 8) for _ in range(9)))
        e.w32(0x396E18, r.choice([0, 0, 0, 0, 1, 2, 3, 5]))
        e.w32(0x396E10, r.choice([0, 0, 0, 1]))
        e.w32(0x396E14, r.getrandbits(32))
        e.wr(0x441366, bytes([r.choice([0, 0, 0, 1])]))
        e.wr(0x396E1E, bytes([r.choice([0, 0, 1, 2, 0xFF])]))
        e.w16(0x397A8A, r.choice(self.ents + [0, 0]))
        e.wr(0x35BF38, bytes([r.choice([0, 1, 2, 3])]))
        e.w32(0x35B738, u(r.choice([-1, -1, 0, 2])))
        e.w32(0x35B73C, u(r.choice([0, 0, 1, 2, 3, 5, -1])))
        e.w32(0x35B7EC, r.choice(self.ents + [0]))
        e.wr(0x35B72E, bytes([r.choice([0, 2, 3, 4, 0xFF])]))
        e.wr(0x442900, bytes([r.randrange(5)]))
        e.wr(0x39639C, struct.pack('<5h', *[r.choice([50, 100, 150, 200, 0, -10]) for _ in range(5)]))
        e.wr(0x39640C, bytes(r.randrange(0, 0x22) for _ in range(0x40 * 9)))
        if r.random() < 0.3: e.w32(MISSION, r.choice([0x1D, 0x24]))
        # death list
        dl = bytearray(0x400)
        cnt = 0
        for j in range(128):
            if r.random() < 0.15:
                struct.pack_into('<hIh', dl, 8 * j, r.choice(self.ents + [NENT + 2]),
                                 u(e.u32(GAME_TIME) + r.choice([-1000, -1, 0, 1, 1000, 100000])), r.choice(self.ents + [0]))
                cnt += 1
        e.wr(DEATHS, bytes(dl))
        e.w16(DCOUNT, r.choice([cnt, cnt, 0, 127, 128, cnt + 1]))

    def ent_rec(self, ent):
        """the records static_286794 gives for ent (Python view), or None"""
        e = self.e
        cls = e.rd(ENTS + (s16(ent)) * ESZ + 1, 1)[0]
        slot = s16(e.u16(ENTS + s16(ent) * ESZ + 3))
        if cls not in (1, 2, 3, 4): return None
        b = CLS[cls][0] + slot * CLS[cls][1]
        if cls == 1: return (b + 0x15, 4, e.u32(b + 0x9C), e.u16(b + 0xA0), b + 0x86, b + 0x45, s8(e.rd(b + 0x81, 1)[0]))
        if cls == 2: return (b + 6, 1, b + 0xD, 2, b + 0x37, b + 0x17, s8(e.rd(b + 0x35, 1)[0]))
        if cls == 3: return (b + 6, 3, b + 0x1B, 3, 0, b + 0x2A, s8(e.rd(b + 0x4A, 1)[0]))
        return (b + 0x10, s8(e.rd(b + 0xC, 1)[0]), b + 0x51, 1, b + 0x56, b + 0x33, s8(e.rd(b + 0xD, 1)[0]))

    def amount(self):
        r = self.rnd
        return r.choice([0, 1, 2, 3, 10, 50, 100, 300, 1000, 5000, -3]) if r.random() < 0.7 else r.randrange(0, 2000)

    def victim(self):
        r = self.rnd
        return self.player if r.random() < 0.25 else self.ent()

    # ================================================================ groups
    def setup(self):
        e = self.e
        if getattr(self, 'set', False): return
        self.set = True
        shutil.copy(os.path.join(GAME, 'TNOVA', 'DATA', 'RESSIM.RES'), os.path.join(self.dir, 'RESSIM.RES'))
        e.call(0x2E1320)                       # ResInit
        e.call(0x2EC5B0, eax=48)               # MsgPoolInit (original)
        self.fakes()

    def g_init(self):
        e, rnd = self.e, self.rnd
        self.setup()
        for i in range(n(6)):
            e.wr(0x3968F0, bytes(rnd.getrandbits(8) for _ in range(0x120)))
            e.wr(0x396A10, bytes(rnd.getrandbits(8) for _ in range(0x400)))
            e.w32(0x35B704, rnd.getrandbits(32))
            self.cmp('static_2863B8', mask=0)
            self.cmp('static_286488', mask=0)
        t = e.rd(0x3968F0, 0x120)
        if not any(t): print('   warning: class hit tables empty after init')

    def g_records(self):
        e, rnd = self.e, self.rnd
        self.setup()
        out = e.alloc(0x40)
        bufs = [out + 4 * k for k in range(8)]
        sysb = e.alloc(5 * 21)
        for i in range(n(2500)):
            self.dmg_world()
            k = i % 9
            ent = self.victim()
            if k == 0:
                e.wr(out, bytes(rnd.getrandbits(8) for _ in range(0x40)))
                self.cmp('static_286794', (ent, bufs[0], bufs[1], bufs[2]), tuple(bufs[3:]), mask=0xFF)
            elif k == 1:
                st, r_ = self.cmp('objdmg_status_str', (out, ent), mask=0)
            elif k == 2: self.cmp('objdmg_get_hp', (ent,), mask=0xFFFF)
            elif k == 3: self.cmp('obj_owner', (ent,), mask=0xFFFF)
            elif k == 4:
                v = rnd.choice([0, 1, 2, 3, 5, 6, 100, 2999, -1, -7, -300]) if rnd.random() < 0.5 else rnd.randrange(-3000, 3000)
                self.cmp('objdmg_rand_amount', (u(v),))
            elif k == 5:
                ns = rnd.choice([0, 1, 2, 3, 5, 8, 10, 13, 17, 20])
                for j in range(21): e.wr(sysb + 5 * j, self.sysrec())
                st, _ = self.cmp('objdmg_systems', (ent, sysb, ns, u(self.amount())), mask=0)
            elif k == 6: self.cmp('objdmg_mark', (ent, rnd.choice(self.ents + [0])), mask=0)
            elif k == 7: self.cmp('objdmg_repair_all', mask=0)
            else: self.cmp('objdmg_snd_done', mask=0)

    def g_damage(self):
        e, rnd = self.e, self.rnd
        self.setup()
        for i in range(n(4000)):
            self.dmg_world()
            ent = self.victim()
            att = rnd.choice(self.ents + [0, self.player])
            dt = rnd.choice([0, 1, 2, 3, 4, 7]) if rnd.random() < 0.9 else rnd.getrandbits(8)
            mode = rnd.choice([1, 1, 2, 3, 4, 5, 5, 0])
            amt = self.amount()
            if rnd.random() < 0.08 and ent != 0:
                e.w32(MISSION, 0x1D); e.w32(0x35B7EC, s16(ent)); e.wr(0x35B72E, bytes([rnd.choice([2, 3, 5])]))
            if rnd.random() < 0.05:
                e.w32(MISSION, 0x24)
                e.wr(ENTS + s16(ent) * ESZ + 1, bytes([2, rnd.choice([7, 7, 6])]))
                e.wr(ENTS + s16(att) * ESZ + 1, bytes([8, rnd.choice([0xD, 0xD, 3])]))
            k = i % 4
            n0 = len(e.log)
            if k == 0:
                self.cmp('static_287168', (ent, att, dt, mode), (u(amt),), mask=0)
            elif k == 1:
                self.cmp('objdmg_apply', (ent, att, dt, mode), (u(amt),), mask=0)
            elif k == 2:
                rec = self.ent_rec(ent)
                if rec is None: continue
                parts, np_, sysb, ns, armor, wl, nw = rec
                if rnd.random() < 0.05: sysb = 0
                if rnd.random() < 0.05: wl = 0
                self.cmp('obj_damage', (ent, att, dt, parts), (u(np_), sysb, ns, armor, wl, u(nw), mode, u(amt)), mask=0)
            else:
                self.cmp('objdmg_drain', (ent, rnd.choice(self.ents + [0, 0]), u(self.amount())), mask=0)
            for x in e.log[n0:]:
                self.covinc(x[0])

    def g_credit(self):
        e, rnd = self.e, self.rnd
        self.setup()
        for i in range(n(2000)):
            self.dmg_world()
            ent = self.victim()
            k = i % 3
            n0 = len(e.log)
            if k == 0:
                att = rnd.choice(self.ents + [0, self.player])
                if rnd.random() < 0.4:
                    att = self.player
                    e.wr(ENTS + att * ESZ + 1, bytes([rnd.choice([1, 1, 2, 3, 4])]))
                    vs = [x for x in self.ents if e.rd(ENTS + x * ESZ + 1, 1)[0] == 1 and x != att]
                    if vs:
                        ent = rnd.choice(vs)
                        e.wr(ENTS + ent * ESZ + 2, bytes([rnd.choice([0, 1, 2, 0xD, 0xD, 5])]))
                self.cmp('objdmg_credit', (ent, att, u(self.amount()), u(rnd.randrange(-500, 500))),
                         (rnd.choice([0, 1, 0x100]),), mask=0)
            elif k == 1:
                self.cmp('objdmg_hit_sound', (rnd.choice(self.ents + [0]), u(self.amount()), rnd.getrandbits(32)), mask=0)
            else:
                self.cmp('obj_destroy', (ent, rnd.choice([1, 2, 3, 5]), rnd.choice(self.ents + [0])), mask=0)
            for x in e.log[n0:]:
                self.covinc(x[0])

    def g_deaths(self):
        e, rnd = self.e, self.rnd
        self.setup()
        for i in range(n(2000)):
            self.dmg_world()
            ent = self.victim()
            k = i % 5
            if k == 0: self.cmp('objdmg_death_time', (ent, rnd.choice(self.ents)))
            elif k == 1:
                if rnd.random() < 0.3: ent = s16(e.u16(DEATHS + 8 * rnd.randrange(128)))
                self.cmp('objdmg_death_add', (ent, rnd.choice(self.ents + [0])), mask=0)
            elif k == 2:
                if rnd.random() < 0.6: ent = s16(e.u16(DEATHS + 8 * rnd.randrange(128)))
                self.cmp('static_288040', (ent,), mask=0)
            elif k == 3:
                now = u(e.u32(GAME_TIME) + rnd.choice([-5000, 0, 1, 2000, 200000]))
                self.cmp('static_28808C', (now,), mask=0)
            else: self.cmp('objdmg_alert', (ent,), mask=0)


GROUPS = ['init', 'records', 'damage', 'credit', 'deaths']

def main():
    sel = sys.argv[1:] or GROUPS
    t = T2()
    try:
        for g in sel:
            if hasattr(t, 'g_' + g): t.group(g, getattr(t, 'g_' + g))
    finally:
        t.close()
    print('total %d match, %d differ, %d skipped' % (t.total_ok, t.total_bad, t.total_skip))
    if os.environ.get('TN_G2B2_VERBOSE'): print('callee coverage', sorted(t.cov.items()))
    if sel == GROUPS:
        missing = [k for k in A if k not in t.per]
        if missing: print('not compared:', ' '.join(missing)); return 1
    return 0 if t.total_bad == 0 else 1

if __name__ == '__main__':
    sys.exit(main())
