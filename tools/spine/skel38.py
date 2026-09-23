"""Reader for Spine 3.8 binary skeletons (.skel) and libGDX texture atlases (.atlas).

Written from the public description of the Spine binary export format; it does not use
or include the Spine Runtimes. Only what Azur Lane's SD (chibi) models need is kept, but
every section is parsed so the stream stays aligned.
"""
import struct
from dataclasses import dataclass, field

TRANSFORM_MODES = ["normal", "onlyTranslation", "noRotationOrReflection", "noScale", "noScaleOrReflection"]
ATTACHMENT_TYPES = ["region", "boundingbox", "mesh", "linkedmesh", "path", "point", "clipping"]


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


@dataclass
class Timeline:
    kind: str          # rotate, translate, scale, shear, attachment, color, twocolor, deform, draworder, ik, transform, event
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
    paths: int
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
        r.boolean(); r.boolean()
        a.vertex_count = r.varint()
        a.bones, a.vertices = _read_vertices(r, a.vertex_count)
        r.floats(a.vertex_count // 3)
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

    paths = r.varint()
    for _ in range(paths):
        r.string(); r.varint(); r.boolean()
        [r.varint() for _ in range(r.varint())]
        r.varint()
        r.varint(); r.varint(); r.varint()
        r.floats(5)

    skins = []
    default = _read_skin(r, True, nonessential)
    if default:
        skins.append(default)
    for _ in range(r.varint()):
        skins.append(_read_skin(r, False, nonessential))

    # Resolve linked meshes: copy geometry from the parent mesh.
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
        r.varint()
        for _ in range(r.varint()):
            kind = r.byte()
            n = r.varint()
            frames_with_curves(Timeline("path", None), n,
                               (lambda: r.floats(2)) if kind == 2 else r.f32)
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
    rotate: bool


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
                cur.rotate = val in ("true", "90")
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
