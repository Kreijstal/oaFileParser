#!/usr/bin/env python3
import json
import argparse

def load_json(path):
    with open(path, 'r') as f:
        return json.load(f)

def build_instance_maps(instances):
    maps = {'by_record': {}, 'by_instance_value': {}, 'by_master_value': {}}
    for table_id, entries in instances.items():
        for e in entries:
            key = (table_id, e['record_index'])
            entry = e.copy()
            entry['table_id'] = table_id
            maps['by_record'][key] = entry
            maps['by_instance_value'].setdefault(e['instance_value'], []).append((table_id, e['record_index'], entry))
            maps['by_master_value'].setdefault(e['master_value'], []).append((table_id, e['record_index'], entry))
    return maps

def match_oid(oid, maps):
    matches = []
    # direct matches to instance/master values
    if oid in maps['by_instance_value']:
        for t, ri, e in maps['by_instance_value'][oid]:
            matches.append((t, ri, e, 'instance_value==oid'))
    if oid in maps['by_master_value']:
        for t, ri, e in maps['by_master_value'][oid]:
            matches.append((t, ri, e, 'master_value==oid'))
    # direct record_index equality
    for (t, ri), e in maps['by_record'].items():
        if ri == oid:
            matches.append((t, ri, e, 'record_index==oid'))
    # scaled heuristics (common shifts: 256, 65536, 16777216)
    scales = [256, 65536, 16777216]
    for s in scales:
        if oid % s == 0:
            candidate = oid // s
            # match candidate to record_index
            for (t, ri), e in maps['by_record'].items():
                if ri == candidate:
                    matches.append((t, ri, e, f'oid/{s}==record_index'))
            # match candidate to instance_value
            if candidate in maps['by_instance_value']:
                for t, ri, e in maps['by_instance_value'][candidate]:
                    matches.append((t, ri, e, f'oid/{s}==instance_value'))
    # deduplicate results by (table_id, record_index)
    uniq = []
    seen = set()
    for m in matches:
        key = (m[0], m[1])
        if key not in seen:
            uniq.append({
                'table_id': m[0],
                'record_index': m[1],
                'instance_value': m[2].get('instance_value'),
                'master_value': m[2].get('master_value'),
                'file_offset': m[2].get('file_offset'),
                'reason': m[3]
            })
            seen.add(key)
    return uniq

def correlate(instances_path, connectivity_path, out_json, out_md):
    instances = load_json(instances_path)
    conn = load_json(connectivity_path)
    maps = build_instance_maps(instances)
    resolved = []
    for cand in conn.get('connectivity_candidates', []):
        net = cand.get('net_name')
        oids = cand.get('oids', [])
        rec = {
            'net_name': net,
            'table_id': cand.get('table_id'),
            'table_index': cand.get('table_index'),
            'file_offset': cand.get('file_offset'),
            'oids': oids,
            'matches': []
        }
        for oid in oids:
            matched = match_oid(oid, maps)
            rec['matches'].append({'oid': oid, 'matched': matched})
        resolved.append(rec)
    with open(out_json, 'w') as f:
        json.dump({'resolved': resolved}, f, indent=2)
    with open(out_md, 'w') as f:
        f.write('# Resolved netlist summary\n\n')
        for r in resolved:
            f.write(f"## Net: {r['net_name']} (table {r.get('table_id')} idx {r.get('table_index')} offset {r.get('file_offset')})\n\n")
            f.write('OIDs:\n\n')
            for m in r['matches']:
                oid = m['oid']
                if m['matched']:
                    f.write(f"- OID {oid} -> matches:\n")
                    for mm in m['matched']:
                        f.write(f"  - table {mm['table_id']} record {mm['record_index']} offset {mm.get('file_offset')} reason: {mm['reason']}\n")
                else:
                    f.write(f"- OID {oid} -> no match\n")
            f.write('\n')
    print(f"Wrote {out_json} and {out_md}")

def main():
    p = argparse.ArgumentParser(description='Correlate instance records and connectivity OIDs')
    p.add_argument('--instances', default='aaic/parsed_instances.json')
    p.add_argument('--connectivity', default='aaic/parsed_connectivity.json')
    p.add_argument('--out-json', default='aaic/resolved_netlist.json')
    p.add_argument('--out-md', default='aaic/resolved_netlist.md')
    args = p.parse_args()
    correlate(args.instances, args.connectivity, args.out_json, args.out_md)

if __name__ == '__main__':
    main()