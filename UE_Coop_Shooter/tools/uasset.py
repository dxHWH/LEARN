"""Minimal UE 5.5 editor .uasset reader (names / imports / exports / tagged props / graph pins).

Only what is needed to audit Blueprint graphs:
  * package summary, name map, import map, export map
  * tagged property stream (UE5.4+ complete-type-name tags), decoding the few
    value types used by K2Nodes; everything else is skipped by Size
  * UEdGraphPin array that UEdGraphNode serializes after its properties
"""
import struct, sys

class R:
    def __init__(self, data, pos=0):
        self.d, self.p = data, pos
    def i32(self):
        v = struct.unpack_from('<i', self.d, self.p)[0]; self.p += 4; return v
    def u32(self):
        v = struct.unpack_from('<I', self.d, self.p)[0]; self.p += 4; return v
    def i64(self):
        v = struct.unpack_from('<q', self.d, self.p)[0]; self.p += 8; return v
    def u16(self):
        v = struct.unpack_from('<H', self.d, self.p)[0]; self.p += 2; return v
    def u8(self):
        v = self.d[self.p]; self.p += 1; return v
    def i8(self):
        v = struct.unpack_from('<b', self.d, self.p)[0]; self.p += 1; return v
    def f32(self):
        v = struct.unpack_from('<f', self.d, self.p)[0]; self.p += 4; return v
    def f64(self):
        v = struct.unpack_from('<d', self.d, self.p)[0]; self.p += 8; return v
    def guid(self):
        v = self.d[self.p:self.p + 16].hex().upper(); self.p += 16; return v
    def fstr(self):
        n = self.i32()
        if n == 0:
            return ''
        if n < 0:
            n = -n
            s = self.d[self.p:self.p + 2 * n - 2].decode('utf-16-le'); self.p += 2 * n; return s
        if n > 100000:
            raise ValueError('bad fstring len %d at %d' % (n, self.p - 4))
        s = self.d[self.p:self.p + n - 1].decode('latin-1'); self.p += n; return s


class Package:
    def __init__(self, path):
        self.path = path
        self.d = open(path, 'rb').read()
        r = R(self.d)
        assert r.u32() == 0x9E2A83C1
        self.legacy = r.i32()
        if self.legacy != -4:
            r.i32()
        self.ue4 = r.i32()
        self.ue5 = r.i32() if self.legacy <= -8 else 0
        r.i32()  # licensee
        if self.ue5 >= 1016:
            r.p += 20; self.total_header = r.i32()
        ncv = r.i32()
        self.custom = {}
        for _ in range(ncv):
            g = r.guid(); v = r.i32(); self.custom[g] = v
        if self.ue5 < 1016:
            self.total_header = r.i32()
        self.pkgname = r.fstr()
        self.pkgflags = r.u32()
        self.name_count = r.i32(); self.name_off = r.i32()
        if self.ue5 >= 1008:
            self.sop_count = r.i32(); self.sop_off = r.i32()
        else:
            self.sop_count = 0; self.sop_off = 0
        if not (self.pkgflags & 0x80000000):
            r.fstr()
        r.i32(); r.i32()  # gatherable
        self.export_count = r.i32(); self.export_off = r.i32()
        self.import_count = r.i32(); self.import_off = r.i32()
        if self.ue5 >= 1015:
            r.p += 16
        if self.ue5 >= 1014:
            r.i32()
        # rest not needed
        self.read_names(); self.read_imports(); self.read_exports(); self.read_soft_paths()

    def read_names(self):
        r = R(self.d, self.name_off)
        self.names = []
        for _ in range(self.name_count):
            self.names.append(r.fstr()); r.u32()

    def fname(self, r):
        i = r.i32(); num = r.i32()
        if i < 0 or i >= len(self.names):
            raise ValueError('bad fname index %d at %d' % (i, r.p - 8))
        s = self.names[i]
        return s if num == 0 else '%s_%d' % (s, num - 1)

    def read_imports(self):
        r = R(self.d, self.import_off)
        self.imports = []
        for _ in range(self.import_count):
            cp = self.fname(r); cn = self.fname(r); outer = r.i32(); on = self.fname(r)
            if not (self.pkgflags & 0x80000000) and self.ue4 >= 520:
                self.fname(r)
            if self.ue5 >= 1003:
                r.i32()
            self.imports.append(dict(cls=cn, outer=outer, name=on, pkg=cp))

    def read_exports(self):
        r = R(self.d, self.export_off)
        self.exports = []
        for _ in range(self.export_count):
            e = {}
            e['cls'] = r.i32(); e['super'] = r.i32(); e['tmpl'] = r.i32(); e['outer'] = r.i32()
            e['name'] = self.fname(r); e['flags'] = r.u32()
            e['size'] = r.i64(); e['off'] = r.i64()
            r.i32(); r.i32(); r.i32()
            if self.ue5 < 1005:
                r.p += 16
            if self.ue5 >= 1006:
                r.i32()
            r.u32(); r.i32(); r.i32()
            if self.ue5 >= 1003:
                r.i32()
            r.p += 20
            if self.ue5 >= 1010:
                e['sstart'] = r.i64(); e['send'] = r.i64()
            self.exports.append(e)

    def read_soft_paths(self):
        self.soft = []
        if not self.sop_count:
            return
        r = R(self.d, self.sop_off)
        for _ in range(self.sop_count):
            pkg = self.fname(r); asset = self.fname(r); sub = r.fstr()
            self.soft.append('%s.%s%s' % (pkg, asset, (':' + sub) if sub else ''))

    # ---- object references -------------------------------------------------
    def obj(self, idx):
        if idx == 0:
            return None
        if idx > 0:
            return self.exports[idx - 1]['name']
        return self.imports[-idx - 1]['name']

    def cls_name(self, e):
        return self.obj(e['cls']) or 'Class'

    def full(self, idx):
        parts = []
        while idx:
            if idx > 0:
                e = self.exports[idx - 1]; parts.append(e['name']); idx = e['outer']
            else:
                im = self.imports[-idx - 1]; parts.append(im['name']); idx = im['outer']
        return '.'.join(reversed(parts))

    # ---- tagged properties ----------------------------------------------------
    def type_name(self, r):
        names = []
        remaining = 1
        while remaining:
            n = self.fname(r); c = r.i32(); names.append((n, c)); remaining += c - 1
        # rebuild a readable string
        out = []
        def build(i):
            n, c = names[i]; i += 1
            if not c:
                return n, i
            kids = []
            for _ in range(c):
                k, i = build(i); kids.append(k)
            return '%s(%s)' % (n, ','.join(kids)), i
        return build(0)[0], names

    def text(self, r):
        flags = r.u32(); ht = r.i8()
        if ht == -1:
            has = r.i32()
            return r.fstr() if has else ''
        if ht == 0:
            r.fstr(); r.fstr(); return r.fstr()
        if ht == 11:  # StringTableEntry
            self.fname(r); return r.fstr()
        if ht in (1, 2, 3):  # Named / Ordered / Argument format
            fmt = self.text(r)
            n = r.i32(); args = []
            for _ in range(n):
                if ht == 1 or ht == 3:
                    r.fstr()
                args.append(self.fmt_arg(r))
            return fmt
        if ht in (4, 5, 6):  # AsNumber / AsPercent / AsCurrency
            if ht == 6:
                r.fstr()
            self.fmt_arg(r)
            has = r.i32()
            if has:
                r.p += 4 + 1 + 4 * 4 + 1  # FNumberFormattingOptions (approx)
            r.fstr(); return '<num>'
        if ht == 10:  # Transform
            t = self.text(r); r.u8(); return t
        raise ValueError('FText history %d unsupported at %d' % (ht, r.p))

    def fmt_arg(self, r):
        t = r.i8()
        if t in (0, 1, 3):
            return r.i64()
        if t == 2:
            return r.f32()
        if t == 4:
            return self.text(r)
        if t == 5:
            return r.u8()
        raise ValueError('fmt arg type %d' % t)

    def props(self, r, end=None):
        out = {}
        while True:
            name = self.fname(r)
            if name == 'None':
                break
            tstr, tnodes = self.type_name(r)
            size = r.i32(); fl = r.u8()
            aidx = 0
            if fl & 0x01:
                aidx = r.i32()
            if fl & 0x02:
                r.p += 16
            if fl & 0x04:
                ext = r.u8()
                if ext & 0x02:
                    r.u8(); r.u8()
            start = r.p
            t = tnodes[0][0]
            val = None
            try:
                if t == 'BoolProperty':
                    val = bool(fl & 0x10)
                elif t in ('IntProperty',):
                    val = r.i32()
                elif t == 'UInt32Property':
                    val = r.u32()
                elif t == 'Int64Property' or t == 'UInt64Property':
                    val = r.i64()
                elif t == 'FloatProperty':
                    val = r.f32()
                elif t == 'DoubleProperty':
                    val = r.f64()
                elif t in ('NameProperty', 'EnumProperty'):
                    val = self.fname(r)
                elif t == 'ByteProperty':
                    val = r.u8() if size == 1 else self.fname(r)
                elif t == 'StrProperty':
                    val = r.fstr()
                elif t == 'TextProperty':
                    val = self.text(r)
                elif t in ('ObjectProperty', 'ClassProperty', 'WeakObjectProperty', 'InterfaceProperty'):
                    val = ('@', r.i32())
                elif t == 'SoftObjectProperty' or t == 'SoftClassProperty':
                    i = r.i32(); val = ('soft', self.soft[i] if 0 <= i < len(self.soft) else i)
                elif t == 'StructProperty':
                    sname = tnodes[1][0] if len(tnodes) > 1 else ''
                    if fl & 0x08:
                        val = ('bin', sname, self.d[r.p:r.p + size].hex())
                    else:
                        val = ('struct', sname, self.props(r))
                elif t == 'ArrayProperty':
                    inner = tnodes[1][0] if len(tnodes) > 1 else ''
                    cnt = r.i32()
                    items = []
                    if inner == 'StructProperty' and not (fl & 0x08):
                        for _ in range(cnt):
                            items.append(self.props(r))
                    elif inner in ('ObjectProperty', 'ClassProperty'):
                        items = [('@', r.i32()) for _ in range(cnt)]
                    elif inner == 'NameProperty':
                        items = [self.fname(r) for _ in range(cnt)]
                    elif inner == 'StrProperty':
                        items = [r.fstr() for _ in range(cnt)]
                    elif inner == 'IntProperty':
                        items = [r.i32() for _ in range(cnt)]
                    else:
                        items = ('skip', inner, cnt)
                    val = ('arr', inner, items)
                else:
                    val = ('?', tstr)
            except Exception as ex:  # keep going; size tells where the next tag is
                val = ('err', tstr, str(ex))
            r.p = start + size
            key = name if not aidx else '%s[%d]' % (name, aidx)
            out[key] = val
        return out

    def export_props(self, e):
        r = R(self.d, e['off'])
        if self.ue5 >= 1011:
            ctl = r.u8()
            if ctl & 0x02:
                r.u8(); r.u8()
        p = self.props(r)
        return p, r.p

    # ---- graph pins -----------------------------------------------------------
    def pin_type(self, r):
        cat = self.fname(r); sub = self.fname(r); subobj = r.i32()
        cont = r.u8()
        if cont == 3:  # map: value terminal type
            self.fname(r); self.fname(r); r.i32(); r.i32(); r.i32()
            if self.custom_ver_ok():
                r.i32()
        bref = r.i32(); bweak = r.i32()
        # PinSubCategoryMemberReference: MemberParent, MemberName, MemberGuid
        r.i32(); self.fname(r); r.p += 16
        bconst = r.i32(); bwrap = r.i32()
        bsingle = r.i32()
        return dict(cat=cat, sub=sub, subobj=self.obj(subobj), cont=cont, ref=bref)

    def custom_ver_ok(self):
        return True

    def pin_ref(self, r):
        null = r.i32()
        if null:
            return None
        node = r.i32(); g = r.guid()
        return (node, g)

    def pin_ref_array(self, r):
        n = r.i32()
        return [self.pin_ref(r) for _ in range(n)]

    def pin(self, r):
        null = r.i32()
        if null:
            return None
        owner = r.i32(); gid = r.guid()
        # UEdGraphPin::Serialize
        owner2 = r.i32(); pid = r.guid()
        name = self.fname(r)
        friendly = self.text(r)
        srcidx = r.i32()
        tip = r.fstr()
        direction = r.u8()
        ptype = self.pin_type(r)
        dv = r.fstr(); adv = r.fstr(); dobj = r.i32(); dtext = self.text(r)
        linked = self.pin_ref_array(r)
        subpins = self.pin_ref_array(r)
        parent = self.pin_ref(r)
        passthru = self.pin_ref(r)
        pguid = r.guid()
        bits = r.u32()
        return dict(id=pid, name=name, friendly=friendly, dir=direction, type=ptype,
                    default=dv, adefault=adv, dobj=self.obj(dobj), dtext=dtext, linked=linked, hidden=bool(bits & 1))

    def node_pins(self, e):
        p, pos = self.export_props(e)
        r = R(self.d, pos)
        hasguid = r.i32()
        if hasguid:
            r.p += 16
        n = r.i32()
        pins = [self.pin(r) for _ in range(n)]
        return p, [x for x in pins if x], r.p - e['off'], e['size']


if __name__ == '__main__':
    pk = Package(sys.argv[1])
    print('UE4', pk.ue4, 'UE5', pk.ue5, 'names', len(pk.names), 'imports', len(pk.imports), 'exports', len(pk.exports))
    for i, e in enumerate(pk.exports):
        print(i + 1, pk.cls_name(e), pk.full(i + 1), e['size'])
