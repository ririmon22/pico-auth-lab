#!/usr/bin/env python3
"""Compare declared OFF/ON CSVs; bootstrap intervals describe within-boot noise."""
import argparse
import csv
import math
from pathlib import Path
import random
import statistics

CONFIG = ('cpu', 'size_mib', 'runs', 'target_seconds', 'seed', 'thp', 'locked', 'drain')

def load(path, label, drop_switches, execution_id=None):
    with Path(path).open(newline='') as f:
        rows = list(csv.DictReader(f))
    if rows and 'execution_id' in rows[0]:
        selected = execution_id or rows[-1]['execution_id']
        rows = [r for r in rows if r['execution_id'] == selected]
        print(f'{path}: selected execution_id={selected}')
    elif execution_id:
        raise ValueError('Legacy CSV has no execution_id')
    if not rows or any(r['label'] != label for r in rows):
        raise ValueError(f'{path}: expected nonempty {label} data')
    configs = {tuple(r[k] for k in CONFIG) for r in rows}
    if len(configs) != 1:
        raise ValueError(f'{path}: mixed configurations')
    groups = {}
    rejected = 0
    for r in rows:
        v = float(r['metric'])
        if not math.isfinite(v) or v <= 0:
            raise ValueError('Nonpositive/nonfinite metric')
        # Keep keys for tests whose samples were all rejected.
        groups.setdefault(r['test'], [])
        if r['valid'] != '1' or (drop_switches and (int(r['nvcsw']) or int(r['nivcsw']))):
            rejected += 1
            continue
        groups[r['test']].append(v)
    return configs.pop(), groups, rejected

def delta(off, on):
    return (on / off - 1) * 100

def interval(off, on):
    rnd = random.Random(12345)
    samples = []
    for _ in range(5000):
        a = statistics.median(rnd.choices(off, k=len(off)))
        b = statistics.median(rnd.choices(on, k=len(on)))
        samples.append(delta(a, b))
    samples.sort()
    return samples[124], samples[4874]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('off'); p.add_argument('on')
    p.add_argument('--drop-switches', action='store_true')
    p.add_argument('--off-run-id'); p.add_argument('--on-run-id')
    p.add_argument('--allow-unverified', action='store_true', help='Allow missing binary hash sidecars')
    args = p.parse_args()
    try:
        hashes = [Path(x + '.sha256') for x in (args.off, args.on)]
        if all(x.exists() for x in hashes):
            h = [x.read_text().strip() for x in hashes]
            if h[0] != h[1] or len(h[0]) != 64 or any(c not in '0123456789abcdef' for c in h[0]):
                raise ValueError('Binary hashes differ or are invalid')
        elif not args.allow_unverified:
            raise ValueError('Missing CSV.sha256 sidecars; use run.sh or --allow-unverified')
        else:
            print('WARNING: binary identity unverified')
        ca, a, ra = load(args.off, 'off', args.drop_switches,args.off_run_id)
        cb, b, rb = load(args.on, 'on', args.drop_switches,args.on_run_id)
        if ca != cb or a.keys() != b.keys():
            raise ValueError('Configurations/test sets differ')
        print(f'Rejected OFF={ra}, ON={rb}; manual BIOS/hardware metadata comparison still required')
        print('test           n_OFF/ON   OFF median    ON median    ON/OFF change%   bootstrap 95%')
        for test in a:
            x, y = a[test], b[test]
            if not x or not y:
                raise ValueError(f'{test}: no usable samples')
            ma, mb = statistics.median(x), statistics.median(y)
            lo, hi = interval(x, y)
            unit = 'ns/load' if test == 'latency' else 'MiB/s'
            print(f'{test:14} {len(x):3}/{len(y):3} {ma:13.3f} {mb:12.3f} {delta(ma,mb):+14.3f} [{lo:+.3f}, {hi:+.3f}] {unit}')
            if min(len(x), len(y)) < 10:
                print('  WARNING: fewer than 10 usable samples')
        print('Bandwidth: negative change = slower. Latency: positive change = slower.')
        print('Intervals cover resampled within-boot variation, NOT reboot-level causal uncertainty.')
    except (OSError, ValueError, KeyError) as e:
        p.exit(2, f'Error: {e}\n')

if __name__ == '__main__':
    main()
