"""Reader for Spine 3.8 skeletons (binary .skel and JSON) and libGDX texture atlases (.atlas).

Written from the public description of the Spine export formats; it does not use or
include the Spine Runtimes. What the renderer needs is kept (bones, slots, IK, transform
and path constraints, skins with region/mesh/linked-mesh/clipping/path attachments,
animations); nonessential data is parsed and dropped so the stream stays aligned.
Both readers return the same dataclasses with the same conventions, see read_skel_json.
"""
import json
import os
import struct
from dataclasses import dataclass, field

TRANSFORM_MODES = ["normal", "onlyTranslation", "noRotationOrReflection", "noScale", "noScaleOrReflection"]
ATTACHMENT_TYPES = ["region", "boundingbox", "mesh", "linkedmesh", "path", "point", "clipping"]
BLEND_MODES = ["normal", "additive", "multiply", "screen"]
POSITION_MODES = ["fixed", "percent"]
SPACING_MODES = ["length", "fixed", "percent"]
ROTATE_MODES = ["tangent", "chain", "chainScale"]


class Reader:
    def __init__(self, data: bytes):
        self.b = data
        self.i = 0
        self.strings = []

    def byte(self):
        v = self.b[self.i]
        self.i += 1
        return v

    def sbyte(self):
        v = self.byte()
        return v - 256 if v > 127 else v

    def boolean(self):
        return self.byte() != 0

    def varint(self, optimize_positive=True):
        result = shift = 0
        while True:
            b = self.byte()
            result |= (b & 0x7F) << shift
            shift += 7
            if not b & 0x80:
                break
        result &= 0xFFFFFFFF  # the writer uses 32-bit ints; negative values take 5 bytes
        if not optimize_positive:
            return (result >> 1) ^ -(result & 1)
        return result - (1 << 32) if result & 0x80000000 else result

    def i32(self):
        v = struct.unpack_from(">i", self.b, self.i)[0]
        self.i += 4
        return v

    def u16(self):
        v = struct.unpack_from(">H", self.b, self.i)[0]
        self.i += 2
        return v

    def f32(self):
        v = struct.unpack_from(">f", self.b, self.i)[0]
        self.i += 4
        return v

    def floats(self, n):
        v = list(struct.unpack_from(">%df" % n, self.b, self.i))
        self.i += 4 * n
        return v

    def string(self):
        n = self.varint()
        if n == 0:
            return None
        if n == 1:
            return ""
        s = self.b[self.i:self.i + n - 1].decode("utf-8", "replace")
        self.i += n - 1
        return s

    def string_ref(self):
        idx = self.varint()
        return None if idx == 0 else self.strings[idx - 1]

    def color(self):
        v = self.i32() & 0xFFFFFFFF
        return ((v >> 24) & 255) / 255, ((v >> 16) & 255) / 255, ((v >> 8) & 255) / 255, (v & 255) / 255


@dataclass
class BoneData:
    index: int
    name: str
    parent: int
    rotation: float
    x: float
    y: float
    scale_x: float
    scale_y: float
    shear_x: float
    shear_y: float
    length: float
    transform_mode: str


@dataclass
class SlotData:
    index: int
    name: str
    bone: int
    color: tuple
    dark: object
    attachment: object
    blend: int


@dataclass
class IkData:
    name: str
    order: int
    bones: list
    target: int
    mix: float
    softness: float
    bend: int
    compress: bool
    stretch: bool
    uniform: bool


@dataclass
class TransformData:
    name: str
    order: int
    bones: list
    target: int
    local: bool
    relative: bool
    offsets: list  # rotation, x, y, scaleX, scaleY, shearY
    mixes: list    # rotate, translate, scale, shear


@dataclass
class PathData:
    name: str
    order: int
    bones: list
    target: int            # slot index holding the path attachment
    position_mode: str     # fixed | percent
    spacing_mode: str      # length | fixed | percent
    rotate_mode: str       # tangent | chain | chainScale
    rotation: float        # offset rotation, degrees
    position: float
    spacing: float
    rotate_mix: float
    translate_mix: float


@dataclass
class Attachment:
    name: str
    type: str
    path: str = None
    # region
    rotation: float = 0
    x: float = 0
    y: float = 0
    scale_x: float = 1
    scale_y: float = 1
    width: float = 0
    height: float = 0
    color: tuple = (1, 1, 1, 1)
    # vertex attachments
    bones: list = None       # weighted: [count, bone, ..., count, bone...]
    vertices: list = None    # unweighted xy pairs, or weighted (x, y, w) triples
    vertex_count: int = 0
    uvs: list = None
    triangles: list = None
    hull: int = 0
    # linked mesh
    parent_name: str = None
    skin_name: str = None
    inherit_deform: bool = True
    # clipping
    end_slot: int = -1
    # path: cumulative setup-pose curve lengths, one per curve
    closed: bool = False
    constant_speed: bool = True
    lengths: list = None


@dataclass
class Timeline:
    kind: str          # rotate, translate, scale, shear, attachment, color, twocolor, deform, draworder, ik, transform,
                       # path_position, path_spacing, path_mix, event
    target: object     # bone/slot/constraint index, or (skin, slot, attachment) for deform
    frames: list = field(default_factory=list)  # list of (time, value, curve)


@dataclass
class Animation:
    name: str
    timelines: list
    duration: float


@dataclass
class Skeleton:
    hash: str
    version: str
    x: float
    y: float
    width: float
    height: float
    bones: list
    slots: list
    iks: list
    transforms: list
    paths: list        # PathData
    skins: list        # list of (name, {slot_index: {attachment_name: Attachment}})
    events: list
    animations: list

    def animation(self, name):
        for a in self.animations:
            if a.name == name:
                return a
        return None

    def attachment(self, slot, name, skin=0):
        if name is None:
            return None
        att = self.skins[skin][1].get(slot, {}).get(name)
        if att is None and skin != 0:
            att = self.skins[0][1].get(slot, {}).get(name)
        return att


def _read_vertices(r, count):
    if not r.boolean():
        return None, r.floats(count * 2)
    bones, verts = [], []
    for _ in range(count):
        n = r.varint()
        bones.append(n)
        for _ in range(n):
            bones.append(r.varint())
            verts.extend(r.floats(3))
    return bones, verts


def _read_curve(r):
    kind = r.byte()
    if kind == 1:
        return "stepped"
    if kind == 2:
        return tuple(r.floats(4))
    return None  # linear


def _read_attachment(r, slot, att_name, nonessential):
    name = r.string_ref() or att_name
    kind = ATTACHMENT_TYPES[r.byte()]
    a = Attachment(name=name, type=kind)
    if kind == "region":
        a.path = r.string_ref() or name
        a.rotation, a.x, a.y, a.scale_x, a.scale_y, a.width, a.height = r.floats(7)
        a.color = r.color()
    elif kind == "boundingbox":
        a.vertex_count = r.varint()
        a.bones, a.vertices = _read_vertices(r, a.vertex_count)
        if nonessential:
            r.i32()
    elif kind == "mesh":
        a.path = r.string_ref() or name
        a.color = r.color()
        a.vertex_count = r.varint()
        a.uvs = r.floats(a.vertex_count * 2)
        a.triangles = [r.u16() for _ in range(r.varint())]
        a.bones, a.vertices = _read_vertices(r, a.vertex_count)
        a.hull = r.varint()
        if nonessential:
            [r.u16() for _ in range(r.varint())]
            a.width, a.height = r.floats(2)
    elif kind == "linkedmesh":
        a.path = r.string_ref() or name
        a.color = r.color()
        a.skin_name = r.string_ref()
        a.parent_name = r.string_ref()
        a.inherit_deform = r.boolean()
        if nonessential:
            a.width, a.height = r.floats(2)
    elif kind == "path":
        a.closed, a.constant_speed = r.boolean(), r.boolean()
        a.vertex_count = r.varint()
        a.bones, a.vertices = _read_vertices(r, a.vertex_count)
        a.lengths = r.floats(a.vertex_count // 3)
        if nonessential:
            r.i32()
    elif kind == "point":
        a.rotation, a.x, a.y = r.floats(3)
        if nonessential:
            r.i32()
    elif kind == "clipping":
        a.end_slot = r.varint()
        a.vertex_count = r.varint()
        a.bones, a.vertices = _read_vertices(r, a.vertex_count)
        if nonessential:
            r.i32()
    return a


def _read_skin(r, is_default, nonessential):
    if is_default:
        slot_count = r.varint()
        if slot_count == 0:
            return None
        name = "default"
    else:
        name = r.string_ref()
        for _ in range(r.varint()):
            r.varint()
        for _ in range(3):  # ik, transform, path constraint references
            for _ in range(r.varint()):
                r.varint()
        slot_count = r.varint()
    table = {}
    for _ in range(slot_count):
        slot = r.varint()
        for _ in range(r.varint()):
            att_name = r.string_ref()
            table.setdefault(slot, {})[att_name] = _read_attachment(r, slot, att_name, nonessential)
    return name, table


def read_skel(data: bytes) -> Skeleton:
    r = Reader(data)
    hash_, version = r.string(), r.string()
    x, y, w, h = r.floats(4)
    nonessential = r.boolean()
    if nonessential:
        r.f32(); r.string(); r.string()
    r.strings = [r.string() for _ in range(r.varint())]

    bones = []
    for i in range(r.varint()):
        name = r.string()
        parent = r.varint() if i else -1
        vals = r.floats(8)
        mode = TRANSFORM_MODES[r.varint()]
        r.boolean()  # skin required
        if nonessential:
            r.i32()
        bones.append(BoneData(i, name, parent, vals[0], vals[1], vals[2], vals[3], vals[4], vals[5], vals[6], vals[7], mode))

    slots = []
    for i in range(r.varint()):
        name = r.string()
        bone = r.varint()
        color = r.color()
        dark = r.i32()
        att = r.string_ref()
        blend = r.varint()
        slots.append(SlotData(i, name, bone, color, None if dark == -1 else dark, att, blend))

    iks = []
    for _ in range(r.varint()):
        name = r.string()
        order = r.varint()
        r.boolean()
        cb = [r.varint() for _ in range(r.varint())]
        target = r.varint()
        mix, softness = r.f32(), r.f32()
        bend = r.sbyte()
        compress, stretch, uniform = r.boolean(), r.boolean(), r.boolean()
        iks.append(IkData(name, order, cb, target, mix, softness, bend, compress, stretch, uniform))

    transforms = []
    for _ in range(r.varint()):
        name = r.string()
        order = r.varint()
        r.boolean()
        cb = [r.varint() for _ in range(r.varint())]
        target = r.varint()
        local, relative = r.boolean(), r.boolean()
        offsets = r.floats(6)
        mixes = r.floats(4)
        transforms.append(TransformData(name, order, cb, target, local, relative, offsets, mixes))

    paths = []
    for _ in range(r.varint()):
        name = r.string()
        order = r.varint()
        r.boolean()
        cb = [r.varint() for _ in range(r.varint())]
        target = r.varint()
        pm, sm, rm = POSITION_MODES[r.varint()], SPACING_MODES[r.varint()], ROTATE_MODES[r.varint()]
        rotation, position, spacing, rotate_mix, translate_mix = r.floats(5)
        paths.append(PathData(name, order, cb, target, pm, sm, rm, rotation, position, spacing, rotate_mix, translate_mix))

    skins = []
    default = _read_skin(r, True, nonessential)
    if default:
        skins.append(default)
    for _ in range(r.varint()):
        skins.append(_read_skin(r, False, nonessential))

    _resolve_linked_meshes(skins)

    events = []
    for _ in range(r.varint()):
        name = r.string_ref()
        r.varint(False); r.f32(); r.string()
        audio = r.string()
        if audio is not None:
            r.floats(2)
        events.append((name, audio))

    animations = []
    for _ in range(r.varint()):
        animations.append(_read_animation(r, r.string(), bones, slots, skins, events))

    sk = Skeleton(hash_, version, x, y, w, h, bones, slots, iks, transforms, paths, skins, events, animations)
    return sk


def _resolve_linked_meshes(skins):
    """Linked meshes take the geometry (not the region) of their parent mesh."""
    for sname, table in skins:
        for slot, atts in table.items():
            for aname, a in atts.items():
                if a.type != "linkedmesh":
                    continue
                src_skin = next((t for n, t in skins if n == a.skin_name), skins[0][1]) if a.skin_name else table
                parent = src_skin.get(slot, {}).get(a.parent_name) or skins[0][1].get(slot, {}).get(a.parent_name)
                if parent is None:
                    continue
                a.type = "mesh"
                a.vertex_count, a.uvs, a.triangles, a.bones, a.vertices, a.hull = (
                    parent.vertex_count, parent.uvs, parent.triangles, parent.bones, parent.vertices, parent.hull)
                a.deform_parent = parent if a.inherit_deform else None


def _read_animation(r, name, bones, slots, skins, events):
    tls = []
    duration = 0.0

    def frames_with_curves(tl, n, read_value):
        nonlocal duration
        for f in range(n):
            t = r.f32()
            v = read_value()
            c = _read_curve(r) if f < n - 1 else None
            tl.frames.append((t, v, c))
            duration = max(duration, t)
        tls.append(tl)

    for _ in range(r.varint()):  # slot timelines
        slot = r.varint()
        for _ in range(r.varint()):
            kind = r.byte()
            n = r.varint()
            if kind == 0:
                tl = Timeline("attachment", slot)
                for _ in range(n):
                    t = r.f32()
                    tl.frames.append((t, r.string_ref(), None))
                    duration = max(duration, t)
                tls.append(tl)
            elif kind == 1:
                frames_with_curves(Timeline("color", slot), n, r.color)
            elif kind == 2:
                frames_with_curves(Timeline("twocolor", slot), n, lambda: (r.color(), r.i32()))
    for _ in range(r.varint()):  # bone timelines
        bone = r.varint()
        for _ in range(r.varint()):
            kind = r.byte()
            n = r.varint()
            if kind == 0:
                frames_with_curves(Timeline("rotate", bone), n, r.f32)
            else:
                frames_with_curves(Timeline(("translate", "scale", "shear")[kind - 1], bone), n, lambda: tuple(r.floats(2)))
    for _ in range(r.varint()):  # ik timelines
        idx = r.varint()
        n = r.varint()
        frames_with_curves(Timeline("ik", idx), n,
                           lambda: (r.f32(), r.f32(), r.sbyte(), r.boolean(), r.boolean()))
    for _ in range(r.varint()):  # transform constraint timelines
        idx = r.varint()
        n = r.varint()
        frames_with_curves(Timeline("transform", idx), n, lambda: tuple(r.floats(4)))
    for _ in range(r.varint()):  # path constraint timelines
        idx = r.varint()
        for _ in range(r.varint()):
            kind = r.byte()
            n = r.varint()
            frames_with_curves(Timeline(("path_position", "path_spacing", "path_mix")[kind], idx), n,
                               (lambda: tuple(r.floats(2))) if kind == 2 else r.f32)
    for _ in range(r.varint()):  # deform timelines
        skin = r.varint()
        for _ in range(r.varint()):
            slot = r.varint()
            for _ in range(r.varint()):
                aname = r.string_ref()
                att = skins[skin][1][slot][aname]
                weighted = att.bones is not None
                length = len(att.vertices) // 3 * 2 if weighted else len(att.vertices)
                n = r.varint()

                def read_deform(att=att, weighted=weighted, length=length):
                    end = r.varint()
                    if end == 0:
                        return [0.0] * length if weighted else list(att.vertices)
                    start = r.varint()
                    d = [0.0] * length
                    d[start:start + end] = r.floats(end)
                    if not weighted:
                        d = [d[k] + att.vertices[k] for k in range(length)]
                    return d

                frames_with_curves(Timeline("deform", (skin, slot, aname)), n, read_deform)
    n = r.varint()  # draw order
    if n:
        tl = Timeline("draworder", None)
        count = len(slots)
        for _ in range(n):
            t = r.f32()
            offsets = r.varint()
            order = [-1] * count
            unchanged = []
            orig = 0
            for _ in range(offsets):
                s = r.varint()
                while orig != s:
                    unchanged.append(orig)
                    orig += 1
                order[orig + r.varint()] = orig
                orig += 1
            while orig < count:
                unchanged.append(orig)
                orig += 1
            for k in range(count - 1, -1, -1):
                if order[k] == -1:
                    order[k] = unchanged.pop()
            tl.frames.append((t, order, None))
            duration = max(duration, t)
        tls.append(tl)
    n = r.varint()  # events
    if n:
        tl = Timeline("event", None)
        for _ in range(n):
            t = r.f32()
            ev = r.varint()
            r.varint(False); r.f32()
            if r.boolean():
                r.string()
            if events[ev][1] is not None:
                r.floats(2)
            tl.frames.append((t, events[ev][0], None))
            duration = max(duration, t)
        tls.append(tl)
    return Animation(name, tls, duration)


# ----------------------------------------------------------------------------- JSON

def _hex_color(s, default=(1.0, 1.0, 1.0, 1.0)):
    """"RRGGBBAA" (or "RRGGBB") -> (r, g, b, a) floats, like Reader.color."""
    if not s:
        return default
    s = s.strip().lstrip("#")
    v = [int(s[i:i + 2], 16) / 255 for i in range(0, len(s) - 1, 2)]
    if len(v) == 3:
        v.append(1.0)
    return tuple(v[:4])


def _hex_rgb888(s):
    """Dark (tint black) colour "RRGGBB" -> int, like the binary slot/timeline dark colour."""
    return None if not s else int(s.strip().lstrip("#")[:6], 16)


def _json_curve(f):
    c = f.get("curve")
    if c is None:
        return None
    if isinstance(c, str):
        return "stepped" if c == "stepped" else None
    if isinstance(c, (list, tuple)):  # before 3.8: [cx1, cy1, cx2, cy2]
        return tuple(float(x) for x in c[:4])
    return float(c), float(f.get("c2", 0.0)), float(f.get("c3", 1.0)), float(f.get("c4", 1.0))


def _json_vertices(m, length):
    """(bones, vertices) as _read_vertices returns them. `length` is the float count of the
    unweighted form (2 per vertex); a different length means the weighted form
    [boneCount, bone, x, y, weight, ...] per vertex."""
    v = m.get("vertices") or []
    if len(v) == length:
        return None, [float(x) for x in v]
    bones, verts = [], []
    i = 0
    while i < len(v):
        n = int(v[i])
        i += 1
        bones.append(n)
        for _ in range(n):
            bones.append(int(v[i]))
            verts.extend((float(v[i + 1]), float(v[i + 2]), float(v[i + 3])))
            i += 4
    return bones, verts


def _json_attachment(m, att_name, slot_ix):
    name = m.get("name", att_name)
    kind = m.get("type", "region")
    if kind in ("skinnedmesh", "weightedmesh"):  # very old exports
        kind = "mesh"
    if kind == "weightedlinkedmesh":
        kind = "linkedmesh"
    a = Attachment(name=name, type=kind)
    if kind == "region":
        a.path = m.get("path", name)
        a.rotation, a.x, a.y = float(m.get("rotation", 0)), float(m.get("x", 0)), float(m.get("y", 0))
        a.scale_x, a.scale_y = float(m.get("scaleX", 1)), float(m.get("scaleY", 1))
        a.width, a.height = float(m.get("width", 32)), float(m.get("height", 32))
        a.color = _hex_color(m.get("color"))
    elif kind in ("mesh", "linkedmesh"):
        a.path = m.get("path", name)
        a.color = _hex_color(m.get("color"))
        a.width, a.height = float(m.get("width", 0)), float(m.get("height", 0))
        if "parent" in m:
            a.type = "linkedmesh"
            a.skin_name = m.get("skin")
            a.parent_name = m["parent"]
            a.inherit_deform = bool(m.get("deform", True))
        else:
            a.type = "mesh"
            a.uvs = [float(x) for x in m["uvs"]]
            a.vertex_count = len(a.uvs) // 2
            a.triangles = [int(x) for x in m["triangles"]]
            a.bones, a.vertices = _json_vertices(m, len(a.uvs))
            a.hull = int(m.get("hull", 0))
    elif kind == "boundingbox":
        a.vertex_count = int(m.get("vertexCount", 0))
        a.bones, a.vertices = _json_vertices(m, a.vertex_count * 2)
    elif kind == "path":
        a.closed = bool(m.get("closed", False))
        a.constant_speed = bool(m.get("constantSpeed", True))
        a.vertex_count = int(m.get("vertexCount", 0))
        a.bones, a.vertices = _json_vertices(m, a.vertex_count * 2)
        a.lengths = [float(x) for x in m.get("lengths", [])]
    elif kind == "point":
        a.rotation, a.x, a.y = float(m.get("rotation", 0)), float(m.get("x", 0)), float(m.get("y", 0))
    elif kind == "clipping":
        end = m.get("end")
        a.end_slot = slot_ix[end] if end in slot_ix else -1
        a.vertex_count = int(m.get("vertexCount", 0))
        a.bones, a.vertices = _json_vertices(m, a.vertex_count * 2)
    return a


def _json_skins(d, slot_ix):
    raw = d.get("skins") or []
    if isinstance(raw, dict):  # before 3.8: {skin name: {slot: {attachment: map}}}
        raw = [{"name": n, "attachments": atts} for n, atts in raw.items()]
    skins = []
    for sm in raw:
        table = {}
        for sname, atts in (sm.get("attachments") or {}).items():
            si = slot_ix[sname]
            for aname, am in atts.items():
                table.setdefault(si, {})[aname] = _json_attachment(am, aname, slot_ix)
        skins.append((sm.get("name", "default"), table))
    # the binary format always stores the default skin first; deform timelines rely on the index
    skins.sort(key=lambda s: s[0] != "default")
    return skins


def _draw_order(offsets, count, slot_of):
    """Draw order (position -> slot index) from Spine's sparse (slot, offset) list."""
    order = [-1] * count
    unchanged = []
    orig = 0
    for slot, off in offsets:
        s = slot_of(slot)
        while orig != s:
            unchanged.append(orig)
            orig += 1
        order[orig + off] = orig
        orig += 1
    while orig < count:
        unchanged.append(orig)
        orig += 1
    for k in range(count - 1, -1, -1):
        if order[k] == -1:
            order[k] = unchanged.pop()
    return order


def _json_animation(name, m, ix, slots, skins, skin_ix):
    tls = []
    duration = 0.0

    def add(tl, frames, value):
        nonlocal duration
        if not frames:
            return
        n = len(frames)
        for k, f in enumerate(frames):
            t = float(f.get("time", 0.0))
            tl.frames.append((t, value(f), _json_curve(f) if k < n - 1 else None))
            duration = max(duration, t)
        tls.append(tl)

    for sname, tmap in (m.get("slots") or {}).items():
        si = ix["slot"][sname]
        for kind, frames in tmap.items():
            if kind == "attachment":
                tl = Timeline("attachment", si)
                for f in frames:
                    t = float(f.get("time", 0.0))
                    tl.frames.append((t, f.get("name"), None))
                    duration = max(duration, t)
                if tl.frames:
                    tls.append(tl)
            elif kind == "color":
                add(Timeline("color", si), frames, lambda f: _hex_color(f.get("color")))
            elif kind == "twoColor":
                add(Timeline("twocolor", si), frames, lambda f: (_hex_color(f.get("light")), _hex_rgb888(f.get("dark")) or 0))
    for bname, tmap in (m.get("bones") or {}).items():
        bi = ix["bone"][bname]
        for kind, frames in tmap.items():
            if kind == "rotate":
                add(Timeline("rotate", bi), frames, lambda f: float(f.get("angle", 0.0)))
            elif kind in ("translate", "shear"):
                add(Timeline(kind, bi), frames, lambda f: (float(f.get("x", 0.0)), float(f.get("y", 0.0))))
            elif kind == "scale":
                add(Timeline(kind, bi), frames, lambda f: (float(f.get("x", 1.0)), float(f.get("y", 1.0))))
    for cname, frames in (m.get("ik") or {}).items():
        add(Timeline("ik", ix["ik"][cname]), frames,
            lambda f: (float(f.get("mix", 1.0)), float(f.get("softness", 0.0)), 1 if f.get("bendPositive", True) else -1,
                       bool(f.get("compress", False)), bool(f.get("stretch", False))))
    for cname, frames in (m.get("transform") or {}).items():
        add(Timeline("transform", ix["transform"][cname]), frames,
            lambda f: (float(f.get("rotateMix", 1.0)), float(f.get("translateMix", 1.0)),
                       float(f.get("scaleMix", 1.0)), float(f.get("shearMix", 1.0))))
    for cname, tmap in (m.get("path") or {}).items():
        ci = ix["path"][cname]
        for kind, frames in tmap.items():
            if kind == "position":
                add(Timeline("path_position", ci), frames, lambda f: float(f.get("position", 0.0)))
            elif kind == "spacing":
                add(Timeline("path_spacing", ci), frames, lambda f: float(f.get("spacing", 0.0)))
            elif kind == "mix":
                add(Timeline("path_mix", ci), frames,
                    lambda f: (float(f.get("rotateMix", 1.0)), float(f.get("translateMix", 1.0))))
    for skin_name, smap in (m.get("deform") or m.get("ffd") or {}).items():
        ski = skin_ix[skin_name]
        for sname, amap in smap.items():
            si = ix["slot"][sname]
            for aname, frames in amap.items():
                att = skins[ski][1][si][aname]
                weighted = att.bones is not None
                length = len(att.vertices) // 3 * 2 if weighted else len(att.vertices)

                def deform(f, att=att, weighted=weighted, length=length):
                    v = f.get("vertices")
                    if v is None:
                        return [0.0] * length if weighted else list(att.vertices)
                    start = int(f.get("offset", 0))
                    d = [0.0] * length
                    d[start:start + len(v)] = [float(x) for x in v]
                    if not weighted:
                        d = [d[k] + att.vertices[k] for k in range(length)]
                    return d

                add(Timeline("deform", (ski, si, aname)), frames, deform)
    frames = m.get("drawOrder") or m.get("draworder")
    if frames:
        tl = Timeline("draworder", None)
        for f in frames:
            t = float(f.get("time", 0.0))
            offs = [(o["slot"], int(o.get("offset", 0))) for o in (f.get("offsets") or [])]
            tl.frames.append((t, _draw_order(offs, len(slots), lambda s: ix["slot"][s]), None))
            duration = max(duration, t)
        tls.append(tl)
    frames = m.get("events")
    if frames:
        tl = Timeline("event", None)
        for f in frames:
            t = float(f.get("time", 0.0))
            tl.frames.append((t, f.get("name"), None))
            duration = max(duration, t)
        tls.append(tl)
    return Animation(name, tls, duration)


def read_skel_json(text) -> Skeleton:
    """Spine 3.8 JSON skeleton (str, bytes or an already parsed dict) -> the same Skeleton that
    read_skel builds from the binary export. Conventions shared with read_skel: indices
    instead of names, colours as float tuples, dark colours as RGB888 ints, IK bend +1/-1,
    blend mode 0..3, default skin first, linked meshes resolved to meshes, deform frames of
    unweighted meshes holding absolute vertices (setup + offset) and of weighted meshes
    holding offsets, draw-order frames as full position -> slot lists."""
    d = json.loads(text) if isinstance(text, (str, bytes, bytearray)) else text
    head = d.get("skeleton") or {}
    ix = {"bone": {}, "slot": {}, "ik": {}, "transform": {}, "path": {}}

    bones = []
    for i, b in enumerate(d.get("bones") or []):
        ix["bone"][b["name"]] = i
        parent = ix["bone"][b["parent"]] if b.get("parent") is not None else -1
        bones.append(BoneData(i, b["name"], parent, float(b.get("rotation", 0)), float(b.get("x", 0)),
                              float(b.get("y", 0)), float(b.get("scaleX", 1)), float(b.get("scaleY", 1)),
                              float(b.get("shearX", 0)), float(b.get("shearY", 0)), float(b.get("length", 0)),
                              b.get("transform", "normal")))

    slots = []
    for i, sm in enumerate(d.get("slots") or []):
        ix["slot"][sm["name"]] = i
        blend = sm.get("blend", "normal")
        slots.append(SlotData(i, sm["name"], ix["bone"][sm["bone"]], _hex_color(sm.get("color")),
                              _hex_rgb888(sm.get("dark")), sm.get("attachment"),
                              BLEND_MODES.index(blend) if blend in BLEND_MODES else 0))

    iks = []
    for c in d.get("ik") or []:
        ix["ik"][c["name"]] = len(iks)
        iks.append(IkData(c["name"], int(c.get("order", 0)), [ix["bone"][n] for n in c["bones"]], ix["bone"][c["target"]],
                          float(c.get("mix", 1)), float(c.get("softness", 0)), 1 if c.get("bendPositive", True) else -1,
                          bool(c.get("compress", False)), bool(c.get("stretch", False)), bool(c.get("uniform", False))))

    transforms = []
    for c in d.get("transform") or []:
        ix["transform"][c["name"]] = len(transforms)
        offsets = [float(c.get(k, 0)) for k in ("rotation", "x", "y", "scaleX", "scaleY", "shearY")]
        mixes = [float(c.get(k, 1)) for k in ("rotateMix", "translateMix", "scaleMix", "shearMix")]
        transforms.append(TransformData(c["name"], int(c.get("order", 0)), [ix["bone"][n] for n in c["bones"]],
                                        ix["bone"][c["target"]], bool(c.get("local", False)),
                                        bool(c.get("relative", False)), offsets, mixes))

    paths = []
    for c in d.get("path") or []:
        ix["path"][c["name"]] = len(paths)
        paths.append(PathData(c["name"], int(c.get("order", 0)), [ix["bone"][n] for n in c["bones"]],
                              ix["slot"][c["target"]], c.get("positionMode", "percent"), c.get("spacingMode", "length"),
                              c.get("rotateMode", "tangent"), float(c.get("rotation", 0)), float(c.get("position", 0)),
                              float(c.get("spacing", 0)), float(c.get("rotateMix", 1)), float(c.get("translateMix", 1))))

    skins = _json_skins(d, ix["slot"])
    _resolve_linked_meshes(skins)
    skin_ix = {n: i for i, (n, _) in enumerate(skins)}

    events = [(n, (e or {}).get("audio")) for n, e in (d.get("events") or {}).items()]
    animations = [_json_animation(n, am, ix, slots, skins, skin_ix) for n, am in (d.get("animations") or {}).items()]
    return Skeleton(head.get("hash"), head.get("spine"), float(head.get("x", 0)), float(head.get("y", 0)),
                    float(head.get("width", 0)), float(head.get("height", 0)), bones, slots, iks, transforms, paths,
                    skins, events, animations)


def is_json_skeleton(data: bytes) -> bool:
    head = data[:64].lstrip(b"\xef\xbb\xbf \t\r\n")
    return head[:1] == b"{"


def load_skeleton(path) -> Skeleton:
    """Reads a Spine 3.8 skeleton file, JSON or binary whatever its name or extension."""
    with open(path, "rb") as f:
        data = f.read()
    if is_json_skeleton(data):
        try:
            return read_skel_json(data.decode("utf-8-sig"))
        except (ValueError, UnicodeDecodeError):
            pass  # a binary file that happens to start with '{'
    return read_skel(data)


def find_skeleton_files(folder):
    """Skeleton files in an unpacked Spine folder: *.skel, *.json skeletons, and extension-less
    JSON files (Azur Lane ships some skeletons as bare TextAssets). Sorted by name."""
    out = []
    for f in sorted(os.listdir(folder)):
        p = os.path.join(folder, f)
        if not os.path.isfile(p):
            continue
        stem, ext = os.path.splitext(f)
        if ext == ".skel":
            out.append(p)
        elif ext in ("", ".json", ".txt") and os.path.exists(os.path.join(folder, stem + ".atlas")):
            with open(p, "rb") as fh:
                if is_json_skeleton(fh.read(64)):
                    out.append(p)
    return out


# ----------------------------------------------------------------------------- atlas

@dataclass
class Region:
    name: str
    page: str
    x: int
    y: int
    width: int      # unrotated size
    height: int
    orig_w: int
    orig_h: int
    offset_x: int   # from the left of the original image
    offset_y: int   # from the BOTTOM of the original image (libGDX convention)
    rotate: bool    # True for any rotation (kept for older callers)
    degrees: int = 0  # how the packer rotated the region on the page: 0, 90, 180 or 270


def read_atlas(text: str):
    """Returns {region name: Region} and {page name: (w, h)}."""
    regions, pages = {}, {}
    page = None
    cur = None
    for raw in text.splitlines():
        line = raw.rstrip()
        if not line:
            page = None
            cur = None
            continue
        if page is None:
            page = line.strip()
            pages[page] = None
            continue
        if ":" in line:
            key, val = [s.strip() for s in line.split(":", 1)]
            parts = [p.strip() for p in val.split(",")]
            if cur is None:  # page header
                if key == "size":
                    pages[page] = (int(parts[0]), int(parts[1]))
                continue
            if key == "rotate":
                # "true" means 90; newer packers also write the angle itself (90, 180, 270)
                cur.degrees = 90 if val == "true" else (0 if val == "false" else int(val) % 360)
                cur.rotate = cur.degrees != 0
            elif key == "xy":
                cur.x, cur.y = int(parts[0]), int(parts[1])
            elif key == "size":
                cur.width, cur.height = int(parts[0]), int(parts[1])
            elif key == "orig":
                cur.orig_w, cur.orig_h = int(parts[0]), int(parts[1])
            elif key == "offset":
                cur.offset_x, cur.offset_y = int(parts[0]), int(parts[1])
            continue
        cur = Region(line.strip(), page, 0, 0, 0, 0, 0, 0, 0, 0, False)
        regions[cur.name] = cur
    return regions, pages
