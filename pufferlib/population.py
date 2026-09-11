"""Descriptor archive prototype; standard library only, no trainer dependency.

Input: versioned descriptor schema + common panel identifier + candidate records.
Each record has id, descriptors and per-opponent {score, games}. Scores are
means of bounded [0,1] episode outcomes. Selection never mutates a live league.
"""
import argparse
import json
import math


def number(x, lo=0, hi=1):
    if isinstance(x, bool) or not isinstance(x, (int, float)) or not math.isfinite(x) or not lo <= x <= hi:
        raise ValueError(f"invalid number: {x!r}")
    return x


def validate(data):
    if data.get('version') != 1 or not data.get('panel_id') or not data.get('schema_id'):
        raise ValueError('version, panel_id and schema_id required')
    schema, records = data['schema'], data['records']
    if not schema or not records:
        raise ValueError('empty schema or records')
    ids = [r['id'] for r in records]
    if any(not isinstance(i, str) or not i for i in ids) or len(set(ids)) != len(ids):
        raise ValueError('candidate IDs must be unique nonempty strings')
    panel = set(records[0]['payoffs'])
    if not panel:
        raise ValueError('empty opponent panel')
    for spec in schema.values():
        number(spec.get('weight', 1), 0, 1e6)
        if spec['kind'] not in ('scalar', 'distribution'):
            raise ValueError('unknown descriptor kind')
        if spec['kind']=='distribution' and (type(spec.get('size')) is not int or spec['size']<1):
            raise ValueError('positive distribution size required')
    if sum(s.get('weight', 1) for s in schema.values()) <= 0:
        raise ValueError('descriptor weights all zero')
    for r in records:
        if set(r['payoffs']) != panel or set(r['descriptors']) != set(schema):
            raise ValueError('all candidates require the same panel and descriptors')
        for p in r['payoffs'].values():
            number(p['score'])
            if type(p['games']) is not int or p['games'] < 2:
                raise ValueError('at least two games per matchup required')
        for key, spec in schema.items():
            x = r['descriptors'][key]
            if spec['kind'] == 'scalar':
                number(x)
            else:
                if not isinstance(x, list) or len(x) != spec['size'] or not x:
                    raise ValueError('descriptor dimension mismatch')
                for v in x:
                    number(v, 0, 1e12)
                if sum(x) <= 0:
                    raise ValueError('empty distribution')
    return records


def distance(a, b, schema):
    """Weighted RMS: scalar differences and normalized Jensen-Shannon distances."""
    total = weight = 0
    for key, spec in schema.items():
        x, y = a['descriptors'][key], b['descriptors'][key]
        if spec['kind'] == 'scalar':
            d2 = (x-y)**2
        else:
            sx, sy = sum(x), sum(y)
            d2 = 0
            for u, v in zip(x, y):
                p, q = u/sx, v/sy
                m = (p+q)/2
                if p: d2 += .5*p*math.log2(p/m)
                if q: d2 += .5*q*math.log2(q/m)
        w = spec.get('weight', 1)
        total += w*max(0, d2)
        weight += w
    return math.sqrt(total/weight)


def select(data, capacity=16, novelty=.15, quality_gap=.15, competitive=True, margin=.02):
    records = validate(data)
    if type(capacity) is not int or capacity < 1: raise ValueError('capacity must be positive')
    for x in (novelty, quality_gap, margin): number(x)
    panel = sorted(records[0]['payoffs'])
    quality = {r['id']: sum(p['score'] for p in r['payoffs'].values())/len(panel) for r in records}
    ranked = sorted(records, key=lambda r: (-quality[r['id']], r['id']))
    selected = [ranked[0]]
    reasons = {ranked[0]['id']: 'quality_anchor'}
    # Conservative simultaneous per-matchup bounds (union bound over records/panel).
    def bound(r, o, upper):
        p = r['payoffs'][o]
        eps = math.sqrt(math.log(2*len(records)*len(panel)/.05)/(2*p['games']))
        return min(1,p['score']+eps) if upper else max(0,p['score']-eps)
    remaining = ranked[1:]
    admissions = {ranked[0]['id']: {'nearest_descriptor_distance': None, 'coverage_lower_advantage': None}}
    while remaining and len(selected) < capacity:
        options = []
        for r in remaining:
            coverage = max(bound(r,o,False)-max(bound(s,o,True) for s in selected) for o in panel)
            novel = min(distance(r,s,data['schema']) for s in selected)
            useful = competitive and coverage > margin
            diverse = quality[r['id']] >= quality[ranked[0]['id']]-quality_gap and novel >= novelty
            if useful or diverse:
                options.append((useful, coverage if useful else novel, quality[r['id']], r['id'], r))
        if not options: break
        best = max(options, key=lambda x: x[:4])
        r = best[-1]
        selected.append(r); remaining.remove(r)
        reasons[r['id']] = 'matchup_coverage' if best[0] else 'descriptor_novelty'
        admissions[r['id']] = {
            'nearest_descriptor_distance': min(distance(r,s,data['schema']) for s in selected[:-1]),
            'coverage_lower_advantage': max(bound(r,o,False)-max(bound(s,o,True) for s in selected[:-1]) for o in panel)}
    return {'version': 1, 'panel_id': data['panel_id'], 'schema_id': data['schema_id'],
            'selected': [{'id': r['id'], 'reason': reasons[r['id']], 'quality': quality[r['id']], **admissions[r['id']]} for r in selected],
            'rejected': [r['id'] for r in remaining],
            'capacity_reached': len(selected) == capacity,
            'note': 'Restricted-panel heuristic, not a Nash solver or an optimality certificate.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', nargs='+'); parser.add_argument('--output')
    parser.add_argument('--capacity', type=int, default=16)
    parser.add_argument('--novelty', type=float, default=.15)
    parser.add_argument('--quality-gap', type=float, default=.15)
    parser.add_argument('--no-competitive', action='store_true')
    args = parser.parse_args()
    data = None
    for path in args.input:
        with open(path) as f: part = json.load(f)
        validate(part)
        if data is None: data = part
        else:
            if any(part[k] != data[k] for k in ('version','schema_id','schema','panel_id')):
                raise ValueError('cannot mix evaluation panels or descriptor schemas')
            data['records'].extend(part['records'])
    result = select(data,args.capacity,args.novelty,args.quality_gap,not args.no_competitive)
    encoded = json.dumps(result,indent=2,allow_nan=False)+'\n'
    if args.output:
        with open(args.output,'x') as f: f.write(encoded)
    else: print(encoded,end='')


if __name__ == '__main__': main()
