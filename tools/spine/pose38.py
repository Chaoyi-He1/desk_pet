"""Pose evaluation for Spine 3.8 skeletons read by skel38: applies one animation at a
time on top of the setup pose and computes bone world transforms (with IK and
world-space transform constraints) and attachment world vertices.

Standard skeletal-animation math, written independently of the Spine Runtimes.
"""
import math
from bisect import bisect_right

DEG = math.pi / 180


def cosd(a):
    return math.cos(a * DEG)


def sind(a):
    return math.sin(a * DEG)


def atan2d(y, x):
    return math.atan2(y, x) / DEG


def wrap180(r):
    r = math.fmod(r, 360.0)
    if r > 180:
        r -= 360
    elif r <= -180:
        r += 360
    return r


def bezier_y(cx1, cy1, cx2, cy2, x):
    """y of the cubic bezier (0,0)-(cx1,cy1)-(cx2,cy2)-(1,1) at the parameter where its x equals `x`."""
    lo, hi = 0.0, 1.0
    for _ in range(30):
        t = (lo + hi) / 2
        u = 1 - t
        bx = 3 * u * u * t * cx1 + 3 * u * t * t * cx2 + t * t * t
        if bx < x:
            lo = t
        else:
            hi = t
    t = (lo + hi) / 2
    u = 1 - t
    return 3 * u * u * t * cy1 + 3 * u * t * t * cy2 + t * t * t


def locate(frames, time):
    """Returns (i, percent) with frames[i].time <= time < frames[i+1].time, or (last, None) after the end."""
    times = [f[0] for f in frames]
    if time >= times[-1]:
        return len(frames) - 1, None
    i = bisect_right(times, time) - 1
    t0, t1 = times[i], times[i + 1]
    p = 0.0 if t1 <= t0 else (time - t0) / (t1 - t0)
    c = frames[i][2]
    if c == "stepped":
        p = 0.0
    elif isinstance(c, tuple):
        p = bezier_y(c[0], c[1], c[2], c[3], p)
    return i, p


def lerp(a, b, p):
    return a + (b - a) * p


class Bone:
    __slots__ = ("data", "parent", "children", "x", "y", "rotation", "scale_x", "scale_y", "shear_x", "shear_y",
                 "ax", "ay", "arotation", "ascale_x", "ascale_y", "ashear_x", "ashear_y", "applied_valid",
                 "a", "b", "c", "d", "wx", "wy", "sorted")

    def __init__(self, data):
        self.data = data
        self.parent = None
        self.children = []

    def setup(self):
        d = self.data
        self.x, self.y, self.rotation = d.x, d.y, d.rotation
        self.scale_x, self.scale_y, self.shear_x, self.shear_y = d.scale_x, d.scale_y, d.shear_x, d.shear_y

    def update(self):
        self.world(self.x, self.y, self.rotation, self.scale_x, self.scale_y, self.shear_x, self.shear_y)

    def world(self, x, y, rotation, sx, sy, shx, shy):
        self.ax, self.ay, self.arotation, self.ascale_x, self.ascale_y, self.ashear_x, self.ashear_y = x, y, rotation, sx, sy, shx, shy
        self.applied_valid = True
        p = self.parent
        if p is None:
            ry = rotation + 90 + shy
            self.a, self.b = cosd(rotation + shx) * sx, cosd(ry) * sy
            self.c, self.d = sind(rotation + shx) * sx, sind(ry) * sy
            self.wx, self.wy = x, y
            return
        pa, pb, pc, pd = p.a, p.b, p.c, p.d
        self.wx = pa * x + pb * y + p.wx
        self.wy = pc * x + pd * y + p.wy
        mode = self.data.transform_mode
        if mode == "normal":
            ry = rotation + 90 + shy
            la, lb = cosd(rotation + shx) * sx, cosd(ry) * sy
            lc, ld = sind(rotation + shx) * sx, sind(ry) * sy
            self.a, self.b = pa * la + pb * lc, pa * lb + pb * ld
            self.c, self.d = pc * la + pd * lc, pc * lb + pd * ld
        elif mode == "onlyTranslation":
            ry = rotation + 90 + shy
            self.a, self.b = cosd(rotation + shx) * sx, cosd(ry) * sy
            self.c, self.d = sind(rotation + shx) * sx, sind(ry) * sy
        elif mode == "noRotationOrReflection":
            s = pa * pa + pc * pc
            if s > 0.0001:
                s = abs(pa * pd - pb * pc) / s
                pb = pc * s
                pd = pa * s
                prx = atan2d(pc, pa)
            else:
                pa = pc = 0
                prx = 90 - atan2d(pd, pb)
            rx = rotation + shx - prx
            ry = rotation + shy - prx + 90
            la, lb = cosd(rx) * sx, cosd(ry) * sy
            lc, ld = sind(rx) * sx, sind(ry) * sy
            self.a, self.b = pa * la - pb * lc, pa * lb - pb * ld
            self.c, self.d = pc * la + pd * lc, pc * lb + pd * ld
        else:  # noScale / noScaleOrReflection
            cs, sn = cosd(rotation), sind(rotation)
            za, zc = pa * cs + pb * sn, pc * cs + pd * sn
            s = math.hypot(za, zc)
            if s > 0.00001:
                s = 1 / s
            za *= s
            zc *= s
            s = math.hypot(za, zc)
            if mode == "noScale" and pa * pd - pb * pc < 0:
                s = -s
            r = math.pi / 2 + math.atan2(zc, za)
            zb, zd = math.cos(r) * s, math.sin(r) * s
            la, lb = cosd(shx) * sx, cosd(90 + shy) * sy
            lc, ld = sind(shx) * sx, sind(90 + shy) * sy
            self.a, self.b = za * la + zb * lc, za * lb + zb * ld
            self.c, self.d = zc * la + zd * lc, zc * lb + zd * ld

    def update_applied(self):
        self.applied_valid = True
        p = self.parent
        if p is None:
            self.ax, self.ay = self.wx, self.wy
            self.arotation = atan2d(self.c, self.a)
            self.ascale_x = math.hypot(self.a, self.c)
            self.ascale_y = math.hypot(self.b, self.d)
            self.ashear_x = 0
            self.ashear_y = atan2d(self.a * self.b + self.c * self.d, self.a * self.d - self.b * self.c)
            return
        pa, pb, pc, pd = p.a, p.b, p.c, p.d
        pid = 1 / (pa * pd - pb * pc)
        dx, dy = self.wx - p.wx, self.wy - p.wy
        self.ax = dx * pd * pid - dy * pb * pid
        self.ay = dy * pa * pid - dx * pc * pid
        ia, idd, ib, ic = pid * pd, pid * pa, pid * pb, pid * pc
        ra = ia * self.a - ib * self.c
        rb = ia * self.b - ib * self.d
        rc = idd * self.c - ic * self.a
        rd = idd * self.d - ic * self.b
        self.ashear_x = 0
        self.ascale_x = math.hypot(ra, rc)
        if self.ascale_x > 0.0001:
            det = ra * rd - rb * rc
            self.ascale_y = det / self.ascale_x
            self.ashear_y = atan2d(ra * rb + rc * rd, det)
            self.arotation = atan2d(rc, ra)
        else:
            self.ascale_x = 0
            self.ascale_y = math.hypot(rb, rd)
            self.ashear_y = 0
            self.arotation = 90 - atan2d(rd, rb)


class Ik:
    def __init__(self, data, bones):
        self.data = data
        self.bones = [bones[i] for i in data.bones]
        self.target = bones[data.target]

    def setup(self):
        d = self.data
        self.mix, self.softness, self.bend, self.compress, self.stretch = d.mix, d.softness, d.bend, d.compress, d.stretch

    def update(self):
        if len(self.bones) == 1:
            ik1(self.bones[0], self.target.wx, self.target.wy, self.compress, self.stretch, self.data.uniform, self.mix)
        else:
            ik2(self.bones[0], self.bones[1], self.target.wx, self.target.wy, self.bend, self.stretch, self.softness, self.mix)


def ik1(bone, tx0, ty0, compress, stretch, uniform, alpha):
    if not bone.applied_valid:
        bone.update_applied()
    p = bone.parent
    pa, pb, pc, pd = p.a, p.b, p.c, p.d
    rot = -bone.ashear_x - bone.arotation
    mode = bone.data.transform_mode
    if mode == "onlyTranslation":
        tx, ty = tx0 - bone.wx, ty0 - bone.wy
    else:
        if mode == "noRotationOrReflection":
            s = abs(pa * pd - pb * pc) / (pa * pa + pc * pc)
            pb, pd = -pc * s, pa * s
            rot += atan2d(pc, pa)
        x, y = tx0 - p.wx, ty0 - p.wy
        d = pa * pd - pb * pc
        tx = (x * pd - y * pb) / d - bone.ax
        ty = (y * pa - x * pc) / d - bone.ay
    rot += atan2d(ty, tx)
    if bone.ascale_x < 0:
        rot += 180
    rot = wrap180(rot)
    sx, sy = bone.ascale_x, bone.ascale_y
    if compress or stretch:
        if mode in ("noScale", "noScaleOrReflection"):
            tx, ty = tx0 - bone.wx, ty0 - bone.wy
        b = bone.data.length * sx
        dd = math.hypot(tx, ty)
        if (compress and dd < b) or (stretch and dd > b and b > 0.0001):
            s = (dd / b - 1) * alpha + 1
            sx *= s
            if uniform:
                sy *= s
    bone.world(bone.ax, bone.ay, bone.arotation + rot * alpha, sx, sy, bone.ashear_x, bone.ashear_y)


def ik2(parent, child, target_x, target_y, bend, stretch, softness, alpha):
    if alpha == 0:
        child.update()
        return
    if not parent.applied_valid:
        parent.update_applied()
    if not child.applied_valid:
        child.update_applied()
    px, py, psx, psy, csx = parent.ax, parent.ay, parent.ascale_x, parent.ascale_y, child.ascale_x
    sx = psx
    if psx < 0:
        psx, os1, s2 = -psx, 180, -1
    else:
        os1, s2 = 0, 1
    if psy < 0:
        psy, s2 = -psy, -s2
    if csx < 0:
        csx, os2 = -csx, 180
    else:
        os2 = 0
    cx = child.ax
    a, b, c, d = parent.a, parent.b, parent.c, parent.d
    u = abs(psx - psy) <= 0.0001
    if not u:
        cy = 0
        cwx, cwy = a * cx + parent.wx, c * cx + parent.wy
    else:
        cy = child.ay
        cwx, cwy = a * cx + b * cy + parent.wx, c * cx + d * cy + parent.wy
    pp = parent.parent
    a, b, c, d = pp.a, pp.b, pp.c, pp.d
    idet = 1 / (a * d - b * c)
    x, y = cwx - pp.wx, cwy - pp.wy
    dx = (x * d - y * b) * idet - px
    dy = (y * a - x * c) * idet - py
    l1 = math.hypot(dx, dy)
    l2 = child.data.length * csx
    if l1 < 0.0001:
        ik1(parent, target_x, target_y, False, stretch, False, alpha)
        child.world(cx, cy, 0, child.ascale_x, child.ascale_y, child.ashear_x, child.ashear_y)
        return
    x, y = target_x - pp.wx, target_y - pp.wy
    tx = (x * d - y * b) * idet - px
    ty = (y * a - x * c) * idet - py
    dd = tx * tx + ty * ty
    if softness != 0:
        softness *= psx * (csx + 1) / 2
        td = math.sqrt(dd)
        sd = td - l1 - l2 * psx + softness
        if sd > 0:
            p = min(1, sd / (softness * 2)) - 1
            p = (sd - softness * (1 - p * p)) / td
            tx -= p * tx
            ty -= p * ty
            dd = tx * tx + ty * ty
    a1 = a2 = None
    if u:
        l2 *= psx
        cs = (dd - l1 * l1 - l2 * l2) / (2 * l1 * l2)
        if cs < -1:
            cs = -1
        elif cs > 1:
            cs = 1
            if stretch:
                sx *= (math.sqrt(dd) / (l1 + l2) - 1) * alpha + 1
        a2 = math.acos(cs) * bend
        a = l1 + l2 * cs
        b = l2 * math.sin(a2)
        a1 = math.atan2(ty * a - tx * b, tx * a + ty * b)
    else:
        a = psx * l2
        b = psy * l2
        aa, bb, ta = a * a, b * b, math.atan2(ty, tx)
        c = bb * l1 * l1 + aa * dd - aa * bb
        c1, c2 = -2 * bb * l1, bb - aa
        d = c1 * c1 - 4 * c2 * c
        if d >= 0:
            q = math.sqrt(d)
            if c1 < 0:
                q = -q
            q = -(c1 + q) / 2
            r0, r1 = q / c2, c / q
            r = r0 if abs(r0) < abs(r1) else r1
            if r * r <= dd:
                y = math.sqrt(dd - r * r) * bend
                a1 = ta - math.atan2(y, r)
                a2 = math.atan2(y / psy, (r - l1) / psx)
        if a1 is None:
            min_angle, min_x, min_y = math.pi, l1 - a, 0
            min_dist = min_x * min_x
            max_angle, max_x, max_y = 0, l1 + a, 0
            max_dist = max_x * max_x
            c = -a * l1 / (aa - bb)
            if -1 <= c <= 1:
                c = math.acos(c)
                x = a * math.cos(c) + l1
                y = b * math.sin(c)
                d = x * x + y * y
                if d < min_dist:
                    min_angle, min_dist, min_x, min_y = c, d, x, y
                if d > max_dist:
                    max_angle, max_dist, max_x, max_y = c, d, x, y
            if dd <= (min_dist + max_dist) / 2:
                a1 = ta - math.atan2(min_y * bend, min_x)
                a2 = min_angle * bend
            else:
                a1 = ta - math.atan2(max_y * bend, max_x)
                a2 = max_angle * bend
    os_ = math.atan2(cy, cx) * s2
    rotation = parent.arotation
    a1 = wrap180((a1 - os_) / DEG + os1 - rotation)
    parent.world(px, py, rotation + a1 * alpha, sx, parent.ascale_y, 0, 0)
    rotation = child.arotation
    a2 = wrap180(((a2 + os_) / DEG - child.ashear_x) * s2 + os2 - rotation)
    child.world(cx, cy, rotation + a2 * alpha, child.ascale_x, child.ascale_y, child.ashear_x, child.ashear_y)


class TransformC:
    """Transform constraint: world or local space, absolute or relative."""

    def __init__(self, data, bones):
        self.data = data
        self.bones = [bones[i] for i in data.bones]
        self.target = bones[data.target]

    def setup(self):
        self.mixes = list(self.data.mixes)

    def update(self):
        if self.data.local:
            return self._update_local()
        if self.data.relative:
            return self._update_relative_world()
        rot_mix, tr_mix, sc_mix, sh_mix = self.mixes
        t = self.target
        ta, tb, tc, td = t.a, t.b, t.c, t.d
        reflect = DEG if ta * td - tb * tc > 0 else -DEG
        off = self.data.offsets
        off_rot, off_shy = off[0] * reflect, off[5] * reflect
        for bone in self.bones:
            modified = False
            if rot_mix != 0:
                a, b, c, d = bone.a, bone.b, bone.c, bone.d
                r = math.atan2(tc, ta) - math.atan2(c, a) + off_rot
                r = (r + math.pi) % (2 * math.pi) - math.pi
                r *= rot_mix
                cs, sn = math.cos(r), math.sin(r)
                bone.a, bone.b = cs * a - sn * c, cs * b - sn * d
                bone.c, bone.d = sn * a + cs * c, sn * b + cs * d
                modified = True
            if tr_mix != 0:
                wx = off[1] * ta + off[2] * tb + t.wx
                wy = off[1] * tc + off[2] * td + t.wy
                bone.wx += (wx - bone.wx) * tr_mix
                bone.wy += (wy - bone.wy) * tr_mix
                modified = True
            if sc_mix > 0:
                s = math.hypot(bone.a, bone.c)
                if s:
                    s = (s + (math.hypot(ta, tc) - s + off[3]) * sc_mix) / s
                bone.a *= s
                bone.c *= s
                s = math.hypot(bone.b, bone.d)
                if s:
                    s = (s + (math.hypot(tb, td) - s + off[4]) * sc_mix) / s
                bone.b *= s
                bone.d *= s
                modified = True
            if sh_mix > 0:
                b, d = bone.b, bone.d
                by = math.atan2(d, b)
                r = math.atan2(td, tb) - math.atan2(tc, ta) - (by - math.atan2(bone.c, bone.a))
                r = (r + math.pi) % (2 * math.pi) - math.pi
                r = by + (r + off_shy) * sh_mix
                s = math.hypot(b, d)
                bone.b, bone.d = math.cos(r) * s, math.sin(r) * s
                modified = True
            if modified:
                bone.applied_valid = False

    def _update_relative_world(self):
        rot_mix, tr_mix, sc_mix, sh_mix = self.mixes
        t = self.target
        ta, tb, tc, td = t.a, t.b, t.c, t.d
        reflect = DEG if ta * td - tb * tc > 0 else -DEG
        off = self.data.offsets
        off_rot, off_shy = off[0] * reflect, off[5] * reflect
        for bone in self.bones:
            if rot_mix != 0:
                a, b, c, d = bone.a, bone.b, bone.c, bone.d
                r = math.atan2(tc, ta) + off_rot
                r = (r + math.pi) % (2 * math.pi) - math.pi
                r *= rot_mix
                cs, sn = math.cos(r), math.sin(r)
                bone.a, bone.b = cs * a - sn * c, cs * b - sn * d
                bone.c, bone.d = sn * a + cs * c, sn * b + cs * d
            if tr_mix != 0:
                wx = off[1] * ta + off[2] * tb + t.wx
                wy = off[1] * tc + off[2] * td + t.wy
                bone.wx += wx * tr_mix
                bone.wy += wy * tr_mix
            if sc_mix > 0:
                s = (math.hypot(ta, tc) - 1 + off[3]) * sc_mix + 1
                bone.a *= s
                bone.c *= s
                s = (math.hypot(tb, td) - 1 + off[4]) * sc_mix + 1
                bone.b *= s
                bone.d *= s
            if sh_mix > 0:
                r = math.atan2(td, tb) - math.atan2(tc, ta)
                r = (r + math.pi) % (2 * math.pi) - math.pi
                b, d = bone.b, bone.d
                r = math.atan2(d, b) + (r - math.pi / 2 + off_shy) * sh_mix
                s = math.hypot(b, d)
                bone.b, bone.d = math.cos(r) * s, math.sin(r) * s
            bone.applied_valid = False

    def _update_local(self):
        rot_mix, tr_mix, sc_mix, sh_mix = self.mixes
        t = self.target
        if not t.applied_valid:
            t.update_applied()
        off = self.data.offsets
        rel = self.data.relative
        for bone in self.bones:
            if not bone.applied_valid:
                bone.update_applied()
            rotation, x, y = bone.arotation, bone.ax, bone.ay
            sx, sy, shy = bone.ascale_x, bone.ascale_y, bone.ashear_y
            if rel:
                if rot_mix != 0:
                    rotation += (t.arotation + off[0]) * rot_mix
                if tr_mix != 0:
                    x += (t.ax + off[1]) * tr_mix
                    y += (t.ay + off[2]) * tr_mix
                if sc_mix != 0:
                    sx *= (t.ascale_x - 1 + off[3]) * sc_mix + 1
                    sy *= (t.ascale_y - 1 + off[4]) * sc_mix + 1
                if sh_mix != 0:
                    shy += (t.ashear_y + off[5]) * sh_mix
            else:
                if rot_mix != 0:
                    rotation += wrap180(t.arotation - rotation + off[0]) * rot_mix
                if tr_mix != 0:
                    x += (t.ax - x + off[1]) * tr_mix
                    y += (t.ay - y + off[2]) * tr_mix
                if sc_mix != 0:
                    if sx:
                        sx = (sx + (t.ascale_x - sx + off[3]) * sc_mix) / sx
                    if sy:
                        sy = (sy + (t.ascale_y - sy + off[4]) * sc_mix) / sy
                if sh_mix != 0:
                    shy += wrap180(t.ashear_y - shy + off[5]) * sh_mix
            bone.world(x, y, rotation, sx, sy, bone.ashear_x, shy)


class Slot:
    __slots__ = ("data", "color", "attachment", "deform")

    def __init__(self, data):
        self.data = data


class Pose:
    def __init__(self, sk, skin=0):
        self.sk = sk
        self.skin = skin
        self.bones = [Bone(b) for b in sk.bones]
        for b in self.bones:
            if b.data.parent >= 0:
                b.parent = self.bones[b.data.parent]
                b.parent.children.append(b)
        self.slots = [Slot(s) for s in sk.slots]
        self.iks = [Ik(d, self.bones) for d in sk.iks]
        self.tcs = [TransformC(d, self.bones) for d in sk.transforms]
        self._build_cache()
        self.setup()

    # --- update order (constraints need their inputs updated first) ---
    def _build_cache(self):
        cache, reset = [], []
        for b in self.bones:
            b.sorted = False

        def sort_bone(b):
            if b.sorted:
                return
            if b.parent is not None:
                sort_bone(b.parent)
            b.sorted = True
            cache.append(b)

        def sort_reset(bones):
            for b in bones:
                if b.sorted:
                    sort_reset(b.children)
                b.sorted = False

        cons = [(c.data.order, c) for c in self.iks] + [(c.data.order, c) for c in self.tcs]
        for _, c in sorted(cons, key=lambda x: x[0]):
            if isinstance(c, Ik):
                sort_bone(c.target)
                parent = c.bones[0]
                sort_bone(parent)
                if len(c.bones) > 1 and c.bones[-1] not in cache:
                    reset.append(c.bones[-1])
                cache.append(c)
                sort_reset(parent.children)
                c.bones[-1].sorted = True
            else:
                sort_bone(c.target)
                if c.data.local:
                    for child in c.bones:
                        sort_bone(child.parent)
                        if child not in cache:
                            reset.append(child)
                else:
                    for child in c.bones:
                        sort_bone(child)
                cache.append(c)
                for child in c.bones:
                    sort_reset(child.children)
                for child in c.bones:
                    child.sorted = True
        for b in self.bones:
            sort_bone(b)
        self.cache, self.cache_reset = cache, reset

    def setup(self):
        for b in self.bones:
            b.setup()
        for c in self.iks:
            c.setup()
        for c in self.tcs:
            c.setup()
        for s in self.slots:
            s.color = list(s.data.color)
            s.attachment = self.sk.attachment(s.data.index, s.data.attachment, self.skin)
            s.deform = None
        self.draw_order = list(range(len(self.slots)))

    def set_attachment(self, slot, name):
        att = self.sk.attachment(slot.data.index, name, self.skin) if name else None
        if att is not slot.attachment:
            slot.attachment = att
            slot.deform = None

    def apply(self, anim, time):
        """Setup pose + `anim` at `time` (seconds), then world transforms."""
        self.setup()
        for tl in anim.timelines:
            f = tl.frames
            k = tl.kind
            if k in ("rotate", "translate", "scale", "shear"):
                if time < f[0][0]:
                    continue
                bone = self.bones[tl.target]
                bd = bone.data
                i, p = locate(f, time)
                if k == "rotate":
                    if p is None:
                        r = f[i][1]
                    else:
                        prev = f[i][1]
                        r = prev + wrap180(f[i + 1][1] - prev) * p
                    bone.rotation = bd.rotation + wrap180(r)
                    continue
                if p is None:
                    x, y = f[i][1]
                else:
                    x = lerp(f[i][1][0], f[i + 1][1][0], p)
                    y = lerp(f[i][1][1], f[i + 1][1][1], p)
                if k == "translate":
                    bone.x, bone.y = bd.x + x, bd.y + y
                elif k == "scale":
                    bone.scale_x, bone.scale_y = x * bd.scale_x, y * bd.scale_y
                else:
                    bone.shear_x, bone.shear_y = bd.shear_x + x, bd.shear_y + y
            elif k == "attachment":
                slot = self.slots[tl.target]
                if time < f[0][0]:
                    continue
                i, _ = locate(f, time)
                self.set_attachment(slot, f[i][1])
            elif k in ("color", "twocolor"):
                if time < f[0][0]:
                    continue
                slot = self.slots[tl.target]
                i, p = locate(f, time)
                c0 = f[i][1] if k == "color" else f[i][1][0]
                if p is None:
                    slot.color = list(c0)
                else:
                    c1 = f[i + 1][1] if k == "color" else f[i + 1][1][0]
                    slot.color = [lerp(c0[j], c1[j], p) for j in range(4)]
            elif k == "ik":
                if time < f[0][0]:
                    continue
                c = self.iks[tl.target]
                i, p = locate(f, time)
                mix, soft, bend, compress, stretch = f[i][1]
                if p is not None:
                    mix = lerp(mix, f[i + 1][1][0], p)
                    soft = lerp(soft, f[i + 1][1][1], p)
                c.mix, c.softness, c.bend, c.compress, c.stretch = mix, soft, bend, compress, stretch
            elif k == "transform":
                if time < f[0][0]:
                    continue
                c = self.tcs[tl.target]
                i, p = locate(f, time)
                v = f[i][1]
                c.mixes = list(v) if p is None else [lerp(v[j], f[i + 1][1][j], p) for j in range(4)]
            elif k == "deform":
                skin, slot_i, aname = tl.target
                slot = self.slots[slot_i]
                att = self.sk.skins[skin][1][slot_i][aname]
                cur = slot.attachment
                if cur is None or (cur is not att and getattr(cur, "deform_parent", None) is not att):
                    continue
                if time < f[0][0]:
                    slot.deform = None
                    continue
                i, p = locate(f, time)
                v0 = f[i][1]
                slot.deform = list(v0) if p is None else [a + (b - a) * p for a, b in zip(v0, f[i + 1][1])]
            elif k == "draworder":
                if time < f[0][0]:
                    continue
                i, _ = locate(f, time)
                if f[i][1] is not None:
                    self.draw_order = list(f[i][1])
        self.update_world()

    def update_world(self):
        for b in self.cache_reset:
            b.ax, b.ay, b.arotation = b.x, b.y, b.rotation
            b.ascale_x, b.ascale_y, b.ashear_x, b.ashear_y = b.scale_x, b.scale_y, b.shear_x, b.shear_y
            b.applied_valid = True
        for item in self.cache:
            item.update()

    # --- geometry ---
    def world_vertices(self, slot):
        """World-space vertex list [(x, y), ...] for the slot's region/mesh attachment."""
        att = slot.attachment
        bone = self.bones[slot.data.bone]
        if att.type == "region":
            return [(ox * bone.a + oy * bone.b + bone.wx, ox * bone.c + oy * bone.d + bone.wy) for ox, oy in att.offset]
        verts = att.vertices
        deform = slot.deform
        if att.bones is None:
            v = deform if deform is not None else verts
            return [(v[i] * bone.a + v[i + 1] * bone.b + bone.wx, v[i] * bone.c + v[i + 1] * bone.d + bone.wy)
                    for i in range(0, len(v), 2)]
        out = []
        bi = wi = fi = 0
        bones = att.bones
        for _ in range(att.vertex_count):
            n = bones[bi]
            bi += 1
            wx = wy = 0.0
            for _ in range(n):
                b = self.bones[bones[bi]]
                vx, vy, w = verts[wi], verts[wi + 1], verts[wi + 2]
                if deform is not None:
                    vx += deform[fi]
                    vy += deform[fi + 1]
                wx += (vx * b.a + vy * b.b + b.wx) * w
                wy += (vx * b.c + vy * b.d + b.wy) * w
                bi += 1
                wi += 3
                fi += 2
            out.append((wx, wy))
        return out
