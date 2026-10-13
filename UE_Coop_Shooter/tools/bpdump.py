"""Blueprint -> readable pseudo code (for auditing).

python bpdump.py <file.uasset> [out.txt]
Prints: parent class, variables (+replication), CDO / component net settings,
custom-event RPC flags, and every graph as indented pseudo code traced along
exec wires. Data inputs are rendered as nested expressions.
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from uasset import Package, R

FUNC = [(0x40, 'Net'), (0x80, 'Reliable'), (0x4000, 'Multicast'), (0x200000, 'Server'),
        (0x1000000, 'Client'), (0x800, 'Event')]
CPF = [(0x20, 'Replicated'), (0x100000000, 'RepNotify'), (0x2000, 'Transient'), (0x4, 'EditInstance')]


def flags(v, table):
    return [n for b, n in table if v & b]


class Dumper:
    def __init__(self, path):
        self.pk = pk = Package(path)
        self.nodes = {}
        self.pins = {}      # (node_idx, pin_guid) -> pin
        self.props = {}
        for i, e in enumerate(pk.exports):
            idx = i + 1
            c = pk.cls_name(e)
            if c.startswith('K2Node') or c == 'EdGraphNode_Comment':
                try:
                    p, pins, used, size = pk.node_pins(e)
                except Exception as ex:
                    p, _ = pk.export_props(e); pins = []
                    p['__pinerr'] = str(ex)
                self.nodes[idx] = dict(cls=c, props=p, pins=pins, name=e['name'], outer=e['outer'])
                for pn in pins:
                    self.pins[(idx, pn['id'])] = pn
                    pn['node'] = idx

    # ------------------------------------------------------------------ helpers
    def mref(self, v):
        if isinstance(v, tuple) and v[0] == 'struct':
            d = v[2]
            parent = d.get('MemberParent')
            pname = self.pk.obj(parent[1]) if parent else None
            return d.get('MemberName', '?'), pname, d.get('bSelfContext', False)
        return '?', None, False

    def title(self, idx):
        n = self.nodes[idx]; c = n['cls']; p = n['props']
        if c == 'K2Node_CustomEvent':
            fl = p.get('FunctionFlags', 0)
            f = [x for x in flags(fl, FUNC) if x not in ('Event',)]
            tag = ('  [RPC ' + '|'.join(f) + ']') if 'Net' in f else ''
            return 'CustomEvent %s%s' % (p.get('CustomFunctionName'), tag)
        if c == 'K2Node_Event' or c == 'K2Node_ComponentBoundEvent':
            if c == 'K2Node_ComponentBoundEvent':
                return 'Event %s.%s' % (p.get('ComponentPropertyName'), p.get('DelegatePropertyName'))
            nm, par, _ = self.mref(p.get('EventReference'))
            return 'Event ' + (p.get('CustomFunctionName') or nm)
        if c == 'K2Node_EnhancedInputAction':
            ia = p.get('InputAction')
            return 'InputAction ' + str(self.pk.obj(ia[1]) if ia else '?')
        if c == 'K2Node_InputKey':
            k = p.get('InputKey')
            return 'InputKey ' + str(k[2].get('KeyName') if isinstance(k, tuple) and k[0] == 'struct' else k)
        if c in ('K2Node_CallFunction', 'K2Node_CallArrayFunction', 'K2Node_PromotableOperator',
                 'K2Node_CommutativeAssociativeBinaryOperator', 'K2Node_CallParentFunction'):
            nm, par, selfc = self.mref(p.get('FunctionReference'))
            return nm
        if c in ('K2Node_VariableGet', 'K2Node_VariableSet'):
            nm, par, selfc = self.mref(p.get('VariableReference'))
            return nm
        if c == 'K2Node_MacroInstance':
            g = p.get('MacroGraphReference')
            if isinstance(g, tuple) and g[0] == 'struct':
                mg = g[2].get('MacroGraph')
                return 'Macro ' + str(self.pk.obj(mg[1]) if mg else '?')
            return 'Macro ?'
        if c == 'K2Node_DynamicCast':
            t = p.get('TargetType')
            return 'CastTo ' + str(self.pk.obj(t[1]) if t else '?')
        if c == 'K2Node_Timeline':
            return 'Timeline ' + str(p.get('TimelineName'))
        if c == 'K2Node_SpawnActorFromClass':
            return 'SpawnActor'
        if c == 'K2Node_CreateWidget':
            return 'CreateWidget'
        if c == 'K2Node_PlayMontage':
            nm, par, _ = self.mref(p.get('ProxyFactoryFunctionName')) if 'ProxyFactoryFunctionName' in p else ('PlayMontage', None, 0)
            return 'PlayMontage'
        if c == 'K2Node_AIMoveTo':
            return 'AIMoveTo'
        if c == 'K2Node_Self':
            return 'self'
        if c == 'K2Node_IfThenElse':
            return 'Branch'
        if c == 'K2Node_ExecutionSequence':
            return 'Sequence'
        if c == 'K2Node_Select':
            return 'Select'
        if c == 'K2Node_SwitchInteger':
            return 'SwitchInt'
        if c == 'K2Node_MakeStruct':
            s = p.get('StructType')
            return 'Make' + str(self.pk.obj(s[1]) if s else '')
        if c == 'K2Node_GetSubsystemFromPC':
            s = p.get('CustomClass')
            return 'GetSubsystem<%s>' % (self.pk.obj(s[1]) if s else '?')
        if c == 'K2Node_CallDelegate':
            nm, par, _ = self.mref(p.get('DelegateReference'))
            return 'CallDelegate ' + nm
        if c == 'K2Node_GetInputActionValue':
            ia = p.get('InputAction')
            return 'GetInputActionValue ' + str(self.pk.obj(ia[1]) if ia else '?')
        if c == 'K2Node_FunctionEntry':
            return 'FunctionEntry'
        if c == 'K2Node_FunctionResult':
            return 'Return'
        return c.replace('K2Node_', '')

    def link_src(self, pin):
        if not pin['linked']:
            return None
        ref = pin['linked'][0]
        if ref is None:
            return None
        return self.pins.get(ref)

    def is_exec(self, pin):
        return pin['type']['cat'] == 'exec'

    def literal(self, pin):
        if pin['dobj']:
            return pin['dobj']
        if pin['dtext']:
            return '"%s"' % pin['dtext']
        dv = pin['default'] or pin.get('adefault', '')
        if dv == '':
            cat = pin['type']['cat']
            dv = {'int': '0', 'int64': '0', 'byte': '0', 'real': '0.0', 'float': '0.0', 'double': '0.0', 'bool': 'false'}.get(cat, '∅')
            if pin['name'] == 'self':
                dv = 'self'

        if pin['type']['cat'] in ('string', 'name', 'text') and dv != '∅':
            return '"%s"' % dv
        return dv

    def expr(self, pin, depth=0):
        """Expression for an input data pin."""
        src = self.link_src(pin)
        if src is None:
            return self.literal(pin)
        return self.out_expr(src, depth)

    def out_expr(self, src, depth=0):
        nidx = src['node']; n = self.nodes[nidx]; c = n['cls']
        has_exec = any(self.is_exec(pp) for pp in n['pins'])
        t = self.title(nidx)
        if c == 'K2Node_Knot':
            inp = [pp for pp in n['pins'] if pp['dir'] == 0]
            return self.expr(inp[0], depth) if inp else '?'
        if c == 'K2Node_VariableGet':
            tgt = [pp for pp in n['pins'] if pp['dir'] == 0 and pp['name'] == 'self' and pp['linked']]
            if tgt:
                return '%s.%s' % (self.expr(tgt[0], depth + 1), t)
            return t
        if c == 'K2Node_Self':
            return 'self'
        if has_exec or depth > 6:
            # impure node output -> reference by node
            return '%s#%d.%s' % (t, nidx, src['name'])
        args = self.args(nidx, depth + 1)
        outs = [pp for pp in n['pins'] if pp['dir'] == 1 and not self.is_exec(pp)]
        suffix = '' if len(outs) <= 1 else '.' + src['name']
        return '%s(%s)%s' % (t, args, suffix)

    def args(self, nidx, depth=0):
        n = self.nodes[nidx]
        parts = []
        for pp in n['pins']:
            if pp['dir'] != 0 or self.is_exec(pp) or pp['hidden']:
                continue
            if pp['name'] == 'self' and not pp['linked']:
                continue
            if pp['name'] == 'WorldContextObject':
                continue
            parts.append('%s=%s' % (pp['name'], self.expr(pp, depth)))
        return ', '.join(parts)

    def exec_outs(self, nidx):
        return [pp for pp in self.nodes[nidx]['pins'] if pp['dir'] == 1 and self.is_exec(pp)]

    def follow(self, pin):
        res = []
        for ref in pin['linked']:
            if ref and ref in self.pins:
                res.append(self.pins[ref])
        return res

    def stmt(self, nidx, via_pin=None):
        t = self.title(nidx)
        a = self.args(nidx)
        entry = '' if via_pin is None or via_pin['name'] in ('execute', 'exec', 'Execute') else ' <%s>' % via_pin['name']
        outs = [pp for pp in self.nodes[nidx]['pins'] if pp['dir'] == 1 and not self.is_exec(pp) and pp['linked']]
        rv = ''
        if outs:
            rv = '  -> ' + ', '.join('%s' % pp['name'] for pp in outs)
        return '%s(%s)%s%s  #%d' % (t, a, entry, rv, nidx)

    def trace(self, pin, ind, seen, out):
        targets = self.follow(pin)
        for tp in targets:
            nidx = tp['node']
            n = self.nodes[nidx]
            if n['cls'] == 'K2Node_Knot':
                for o in self.exec_outs(nidx):
                    self.trace(o, ind, seen, out)
                continue
            if nidx in seen:
                out.append('  ' * ind + 'goto %s #%d%s' % (self.title(nidx), nidx,
                           '' if tp['name'] in ('execute', 'exec') else ' <%s>' % tp['name']))
                continue
            seen.add(nidx)
            out.append('  ' * ind + self.stmt(nidx, tp))
            all_eo = self.exec_outs(nidx)
            eo = [o for o in all_eo if o['linked']]
            if len(all_eo) == 1 and len(eo) == 1:
                self.trace(eo[0], ind, seen, out)
            else:
                for o in eo:
                    out.append('  ' * (ind + 1) + '[%s]:' % (o['friendly'] or o['name']))
                    self.trace(o, ind + 2, seen, out)

    def graph_name(self, idx):
        return self.pk.full(idx)

    # ------------------------------------------------------------------ report
    def report(self):
        pk = self.pk; L = []
        bp = next((e for e in pk.exports if pk.cls_name(e).endswith('Blueprint')), pk.exports[0])
        bpp, _ = pk.export_props(bp)
        L.append('# %s  (%s)' % (pk.pkgname or os.path.basename(pk.path), pk.cls_name(bp)))
        par = bpp.get('ParentClass')
        if par:
            L.append('ParentClass: %s' % pk.full(par[1]))
        nv = bpp.get('NewVariables')
        if nv and isinstance(nv[2], list):
            L.append('\n## Variables')
            for v in nv[2]:
                vt = v.get('VarType')
                tdesc = ''
                if vt and vt[0] == 'bin':
                    try:
                        r = R(bytes.fromhex(vt[2]))
                        ptype = pk.pin_type(r)
                        tdesc = ptype['cat'] + ('<%s>' % ptype['subobj'] if ptype['subobj'] else '') + ('[]' if ptype['cont'] == 1 else '')
                    except Exception as ex:
                        tdesc = '?'
                pf = v.get('PropertyFlags', 0)
                f = flags(pf, CPF)
                rep = ''
                if 'Replicated' in f:
                    rep = '  [Replicated%s%s]' % (
                        ' OnRep=' + v.get('RepNotifyFunc') if v.get('RepNotifyFunc') not in (None, 'None') else '',
                        ' Cond=' + v['ReplicationCondition'] if v.get('ReplicationCondition') not in (None, 'COND_None') else '')
                dv = v.get('DefaultValue', '')
                L.append('- %s : %s%s%s' % (v.get('VarName'), tdesc, rep, ('  = ' + dv) if dv else ''))
        # CDO + components
        for i, e in enumerate(pk.exports):
            nm = e['name']
            c = pk.cls_name(e)
            if nm.startswith('Default__') or c == 'SCS_Node' or nm.endswith('_GEN_VARIABLE'):
                try:
                    p, _ = pk.export_props(e)
                except Exception as ex:
                    p = {'err': str(ex)}
                keep = {}
                for k, v in p.items():
                    if k in ('UberGraphFrame', 'NodeGuid', 'VariableGuid', 'CreationMethod', 'BodyInstance', 'ChildNodes',
                             'RelativeLocation', 'RelativeRotation', 'RelativeScale3D', 'AttachChildren'):
                        continue
                    if isinstance(v, tuple) and v[0] == '@':
                        v = pk.obj(v[1])
                    elif isinstance(v, tuple) and v[0] in ('struct', 'arr'):
                        v = str(v)[:160]
                    elif isinstance(v, tuple) and v[0] == 'bin':
                        continue
                    keep[k] = v
                L.append('\n## %s %s' % (c, pk.full(i + 1)))
                for k, v in keep.items():
                    L.append('   %s = %s' % (k, v))
        # RPCs
        L.append('\n## Custom events / RPC')
        for idx, n in sorted(self.nodes.items()):
            if n['cls'] == 'K2Node_CustomEvent':
                L.append('- ' + self.title(idx) + '   (graph %s)' % pk.full(n['outer']))
        # graphs
        graphs = {}
        for idx, n in self.nodes.items():
            graphs.setdefault(n['outer'], []).append(idx)
        for g, lst in sorted(graphs.items(), key=lambda kv: pk.full(kv[0])):
            L.append('\n## Graph %s' % pk.full(g))
            seen = set()
            entries = [i for i in sorted(lst) if self.nodes[i]['cls'] in (
                'K2Node_Event', 'K2Node_CustomEvent', 'K2Node_FunctionEntry', 'K2Node_EnhancedInputAction',
                'K2Node_InputKey', 'K2Node_ComponentBoundEvent')]
            for i in entries:
                n = self.nodes[i]
                outs = [pp for pp in n['pins'] if pp['dir'] == 1 and not self.is_exec(pp) and pp['type']['cat'] != 'delegate']
                params = ', '.join(pp['name'] for pp in outs)
                L.append('\n%s(%s):  #%d' % (self.title(i), params, i))
                seen.add(i)
                for o in self.exec_outs(i):
                    if not o['linked']:
                        continue
                    if o['name'] != 'then':
                        L.append('  [%s]:' % (o['friendly'] or o['name']))
                        self.trace(o, 2, seen, L)
                    else:
                        self.trace(o, 1, seen, L)
            for i in sorted(lst):
                n = self.nodes[i]
                if n['cls'] == 'EdGraphNode_Comment':
                    L.append('  // comment: %s' % n['props'].get('NodeComment'))
            orphan = [i for i in sorted(lst) if i not in seen and any(self.is_exec(pp) for pp in self.nodes[i]['pins'])
                      and self.nodes[i]['cls'] not in ('K2Node_Knot',)]
            orphan_unreached = [i for i in orphan if not any(
                (pp['dir'] == 0 and self.is_exec(pp) and pp['linked']) for pp in self.nodes[i]['pins'])]
            if orphan_unreached:
                L.append('  // exec nodes with no incoming exec (dead / disconnected): ' +
                         ', '.join('%s#%d' % (self.title(i), i) for i in orphan_unreached))
            for i in sorted(lst):
                if self.nodes[i]['props'].get('__pinerr'):
                    L.append('  // !! pin parse error in #%d %s: %s' % (i, self.title(i), self.nodes[i]['props']['__pinerr']))
                if self.nodes[i]['props'].get('EnabledState') not in (None,):
                    L.append('  // node #%d %s EnabledState=%s' % (i, self.title(i), self.nodes[i]['props'].get('EnabledState')))
        return '\n'.join(L)


if __name__ == '__main__':
    d = Dumper(sys.argv[1])
    txt = d.report()
    if len(sys.argv) > 2:
        open(sys.argv[2], 'w', encoding='utf-8').write(txt)
    else:
        sys.stdout.reconfigure(encoding='utf-8'); print(txt)
