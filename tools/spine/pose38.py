"""Pose evaluation for Spine 3.8 skeletons read by skel38: applies one animation (plus
optional overlay animations such as facial expressions) on top of the setup pose and
computes bone world transforms (with IK, transform and path constraints) and attachment
world vertices.

Standard skeletal-animation and bezier math, written independently of the Spine Runtimes.
"""
import math
from bisect import bisect_left, bisect_right

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


class PathC:
    """Path constraint: moves and rotates bones along the path attachment of a slot.

    Spine 3.8 semantics: the path attachment's vertices come in triples
    (in-handle, point, out-handle); consecutive points and the handles between them form
    cubic bezier curves (plus one closing curve for closed paths). Samples are placed along
    the path at `position` and then every `spacing` (per bone, or scaled by the bone length
    in 'length' mode). With constantSpeed the distances are measured along the current
    world-space curves; otherwise they are looked up in the setup-pose curve lengths stored
    in the attachment and mapped linearly to the curve parameter. Past the ends of an open
    path the samples continue in a straight line along the end tangents.

    Arc lengths are estimated the way the editor and the game do, so bones land where they
    do there: each curve's length is the sum of 4 chords (uniform parameter steps), and a
    sample inside a curve is placed by linear interpolation over 10 chords of that curve.
    """

    CURVE_STEPS = 4     # chords per curve for the path length table
    SEGMENT_STEPS = 10  # chords per curve for placing a sample inside its curve

    def __init__(self, data, pose):
        self.data = data
        self.pose = pose
        self.bones = [pose.bones[i] for i in data.bones]
        self.target = pose.slots[data.target]

    def setup(self):
        d = self.data
        self.position, self.spacing = d.position, d.spacing
        self.rotate_mix, self.translate_mix = d.rotate_mix, d.translate_mix

    def update(self):
        att = self.target.attachment
        if att is None or att.type != "path":
            return
        rot_mix, tr_mix = self.rotate_mix, self.translate_mix
        if rot_mix == 0 and tr_mix == 0:
            return
        d = self.data
        tangents = d.rotate_mode == "tangent"
        chain_scale = d.rotate_mode == "chainScale"
        pct_spacing = d.spacing_mode == "percent"
        bones = self.bones
        n_spaces = len(bones) if tangents else len(bones) + 1
        spaces = [0.0] * n_spaces
        lengths = [0.0] * len(bones)  # world bone lengths, for chainScale
        if chain_scale or not pct_spacing:
            for i in range(n_spaces - 1):
                bone = bones[i]
                setup_len = bone.data.length
                if setup_len < 1e-5:
                    continue  # a zero-length bone takes no room on the path
                world_len = math.hypot(setup_len * bone.a, setup_len * bone.c)
                lengths[i] = world_len
                if pct_spacing:
                    spaces[i + 1] = self.spacing
                else:
                    step = setup_len + self.spacing if d.spacing_mode == "length" else self.spacing
                    spaces[i + 1] = step * world_len / setup_len
        else:
            for i in range(1, n_spaces):
                spaces[i] = self.spacing
        samples = self.sample(att, spaces, d.position_mode == "percent", pct_spacing)

        bx, by = samples[0][0], samples[0][1]
        off_rot = d.rotation
        if off_rot == 0:
            tip = d.rotate_mode == "chain"
        else:
            tip = False
            tb = self.pose.bones[self.target.data.bone]
            off_rot *= DEG if tb.a * tb.d - tb.b * tb.c > 0 else -DEG
        for i, bone in enumerate(bones):
            bone.wx += (bx - bone.wx) * tr_mix
            bone.wy += (by - bone.wy) * tr_mix
            nx, ny = samples[i + 1][:2] if i + 1 < len(samples) else (bx, by)
            dx, dy = nx - bx, ny - by
            if chain_scale and lengths[i] != 0:
                s = (math.hypot(dx, dy) / lengths[i] - 1) * rot_mix + 1
                bone.a *= s
                bone.c *= s
            bx, by = nx, ny
            if rot_mix > 0:
                a, b, c, dd = bone.a, bone.b, bone.c, bone.d
                if tangents:
                    r = samples[i][2]
                elif spaces[i + 1] == 0:
                    r = samples[i + 1][2]
                else:
                    r = math.atan2(dy, dx)
                r -= math.atan2(c, a)
                if tip:
                    # the next bone starts at this bone's rotated tip rather than on the path
                    cs, sn = math.cos(r), math.sin(r)
                    ln = bone.data.length
                    bx += (ln * (cs * a - sn * c) - dx) * rot_mix
                    by += (ln * (sn * a + cs * c) - dy) * rot_mix
                else:
                    r += off_rot
                if r > math.pi:
                    r -= 2 * math.pi
                elif r < -math.pi:
                    r += 2 * math.pi
                r *= rot_mix
                cs, sn = math.cos(r), math.sin(r)
                bone.a, bone.b = cs * a - sn * c, cs * b - sn * dd
                bone.c, bone.d = sn * a + cs * c, sn * b + cs * dd
            bone.applied_valid = False

    def sample(self, att, spaces, pct_position, pct_spacing):
        """[(x, y, tangent angle in radians)] at position, position + spaces[1], ..."""
        pts = self.pose.world_vertices(self.target, att)
        n = len(pts) // 3  # path vertices (handle, point, handle)
        closed = att.closed
        curves = [(pts[3 * k + 1], pts[3 * k + 2], pts[3 * k + 3], pts[3 * k + 4]) for k in range(n - 1)]
        if closed:
            curves.append((pts[3 * n - 2], pts[3 * n - 1], pts[0], pts[1]))
        if not curves:
            return [(pts[1][0], pts[1][1], 0.0)] * len(spaces) if pts else [(0.0, 0.0, 0.0)] * len(spaces)
        nc = len(curves)
        if att.constant_speed:
            chords = [None] * nc  # SEGMENT_STEPS tables, built for the curves that get samples
            cum = []
            total = 0.0
            for c in curves:
                total += self._chords(c, self.CURVE_STEPS)[-1]
                cum.append(total)
            setup_total = att.lengths[nc - 1] if att.lengths and len(att.lengths) >= nc else total
        else:
            chords = None
            cum = list(att.lengths[:nc])
            total = cum[-1]
            setup_total = total
        position = self.position
        if pct_position:
            position *= total
        elif att.constant_speed and setup_total:
            position *= total / setup_total  # fixed positions follow the path when it stretches
        k = total if pct_spacing else 1.0

        out = []
        for i, space in enumerate(spaces):
            position += space * k if i else space
            p = position
            if closed:
                p = math.fmod(p, total) if total else 0.0
                if p < 0:
                    p += total
            elif p < 0:
                out.append(self._extend(pts[1], pts[2], p, backwards=True))
                continue
            elif p > total:
                out.append(self._extend(pts[3 * n - 2], pts[3 * n - 3], p - total, backwards=False))
                continue
            ci = min(bisect_left(cum, p), nc - 1)
            start = cum[ci - 1] if ci else 0.0
            span = cum[ci] - start
            u = (p - start) / span if span > 0 else 0.0
            if chords is not None:
                if chords[ci] is None:
                    chords[ci] = self._chords(curves[ci], self.SEGMENT_STEPS)
                u = self._arc_to_t(chords[ci], u * chords[ci][-1])
            out.append(self._bezier(curves[ci], u))
        return out

    def _chords(self, c, n):
        """Cumulative chord lengths of a bezier at `n` uniform parameter steps."""
        out = []
        acc = 0.0
        px, py = c[0]
        for j in range(1, n + 1):
            x, y, _ = self._bezier(c, j / n, angle=False)
            acc += math.hypot(x - px, y - py)
            out.append(acc)
            px, py = x, y
        return out

    def _arc_to_t(self, cum, s):
        n = len(cum)
        j = min(bisect_left(cum, s), n - 1)
        prev = cum[j - 1] if j else 0.0
        seg = cum[j] - prev
        f = (s - prev) / seg if seg > 0 else 0.0
        return (j + min(max(f, 0.0), 1.0)) / n

    @staticmethod
    def _bezier(c, t, angle=True):
        """(x, y, tangent angle) of the cubic bezier `c` at parameter t. At the very start
        (t < 0.001) the tangent is the direction of the first handle."""
        (x0, y0), (x1, y1), (x2, y2), (x3, y3) = c
        if angle and (t < 1e-5 or t != t):
            return x0, y0, math.atan2(y1 - y0, x1 - x0)
        u = 1 - t
        b0, b1, b2, b3 = u * u * u, 3 * u * u * t, 3 * u * t * t, t * t * t
        x = x0 * b0 + x1 * b1 + x2 * b2 + x3 * b3
        y = y0 * b0 + y1 * b1 + y2 * b2 + y3 * b3
        if not angle:
            return x, y, 0.0
        if t < 0.001:
            return x, y, math.atan2(y1 - y0, x1 - x0)
        # tangent: the point minus the quadratic bezier of the first three control points
        # (de Casteljau), which is parallel to the derivative
        qx = x0 * u * u + x1 * 2 * u * t + x2 * t * t
        qy = y0 * u * u + y1 * 2 * u * t + y2 * t * t
        return x, y, math.atan2(y - qy, x - qx)

    @staticmethod
    def _extend(end, other, dist, backwards):
        """Continue past an end point in a straight line: before the start along
        start->first handle (dist < 0), after the end along last handle->end."""
        ex, ey = end
        if backwards:
            r = math.atan2(other[1] - ey, other[0] - ex)
        else:
            r = math.atan2(ey - other[1], ex - other[0])
        return ex + dist * math.cos(r), ey + dist * math.sin(r), r


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
        self.paths = [PathC(d, self) for d in getattr(sk, "paths", None) or []]
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

        def sort_path_attachment(att, slot_bone):
            if att is None or att.type != "path":
                return
            if att.bones is None:
                sort_bone(slot_bone)
                return
            i = 0
            while i < len(att.bones):
                n = att.bones[i]
                for bi in att.bones[i + 1:i + 1 + n]:
                    sort_bone(self.bones[bi])
                i += 1 + n

        cons = [(c.data.order, c) for c in self.iks] + [(c.data.order, c) for c in self.tcs]
        cons += [(c.data.order, c) for c in self.paths]  # ties: IK, then transform, then path
        for _, c in sorted(cons, key=lambda x: x[0]):
            if isinstance(c, PathC):
                # the path's own bones first (every attachment the slot may show), then the
                # constrained bones; their children are updated again after the constraint
                si = c.data.target
                slot_bone = self.bones[self.sk.slots[si].bone]
                for _, table in self.sk.skins:
                    for att in table.get(si, {}).values():
                        sort_path_attachment(att, slot_bone)
                for bone in c.bones:
                    sort_bone(bone)
                cache.append(c)
                for bone in c.bones:
                    sort_reset(bone.children)
                for bone in c.bones:
                    bone.sorted = True
            elif isinstance(c, Ik):
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
        for c in self.paths:
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

    def apply(self, anim, time, overlays=None):
        """Setup pose + `anim` at `time` (seconds), then world transforms.

        `overlays` [(anim, time), ...] are applied in order on top of the base animation
        without going back to the setup pose, like higher animation tracks at full alpha:
        whatever they key (e.g. an expression's face attachments and eye bones) replaces
        the base animation's value, everything else keeps it."""
        self.setup()
        self._apply_timelines(anim, time)
        for oanim, otime in overlays or ():
            self._apply_timelines(oanim, otime)
        self.update_world()

    def _apply_timelines(self, anim, time):
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
            elif k in ("path_position", "path_spacing", "path_mix"):
                if time < f[0][0]:
                    continue
                c = self.paths[tl.target]
                i, p = locate(f, time)
                v = f[i][1]
                if k == "path_mix":
                    if p is not None:
                        v = (lerp(v[0], f[i + 1][1][0], p), lerp(v[1], f[i + 1][1][1], p))
                    c.rotate_mix, c.translate_mix = v
                else:
                    if p is not None:
                        v = lerp(v, f[i + 1][1], p)
                    if k == "path_position":
                        c.position = v
                    else:
                        c.spacing = v
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

    def update_world(self):
        for b in self.cache_reset:
            b.ax, b.ay, b.arotation = b.x, b.y, b.rotation
            b.ascale_x, b.ascale_y, b.ashear_x, b.ashear_y = b.scale_x, b.scale_y, b.shear_x, b.shear_y
            b.applied_valid = True
        for item in self.cache:
            item.update()

    # --- geometry ---
    def world_vertices(self, slot, att=None):
        """World-space vertex list [(x, y), ...] for the slot's region/mesh/clipping/path attachment."""
        att = att or slot.attachment
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
