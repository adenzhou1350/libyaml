"""Balanced LibYAML load-cycle comparison; requires two already-built archives."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import statistics
import subprocess

CASES = [
    ('empty', 0, 0, 30000), ('ordinary-mapping', 4, 0, 12000),
    ('plain-sequence16', 1, 16, 8000), ('anchors1', 2, 1, 16000),
    ('anchors8', 2, 8, 8000), ('anchors16', 2, 16, 5000),
    ('references1', 3, 1, 14000), ('references8', 3, 8, 6000),
    ('references16', 3, 16, 4000), ('references64', 3, 64, 800),
    ('anchors1024', 2, 1024, 40), ('references1024', 3, 1024, 20),
    ('anchors16000', 2, 16000, 3), ('references16000', 3, 16000, 2),
]
ORDERS = [('baseline', 'candidate'), ('candidate', 'baseline'),
          ('candidate', 'baseline'), ('baseline', 'candidate')]


def expected_input(kind, count):
    if kind == 0:
        data = b''
    elif kind == 4:
        data = b'service: worker\nreplicas: 3\nports: [80, 443]\n'
    else:
        text = ''.join(f'- v{i}\n' if kind == 1 else f'- &a{i:08d} v{i}\n'
                       for i in range(count))
        if kind == 3:
            text += f'- *a{count - 1:08d}\n' * count
        data = text.encode()
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return len(data), f'{value:016x}'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--baseline-library', type=Path, required=True)
    parser.add_argument('--candidate-library', type=Path, required=True)
    parser.add_argument('--include-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(exist_ok=False)
    source = Path(__file__).with_name('benchmark.c').resolve()
    libraries = {'baseline': args.baseline_library.resolve(),
                 'candidate': args.candidate_library.resolve()}
    inputs = [source, args.include_dir.resolve() / 'yaml.h', *libraries.values()]
    initial = {str(p): digest(p) for p in inputs}
    def save(name, value):
        (out / name).write_text(json.dumps(value, indent=2), encoding='utf-8')
    flags = ['-std=c99', '-O2', '-UNDEBUG', '-pipe', '-Wall', '-Wextra', '-Werror',
             '-DYAML_DECLARE_STATIC', '-I' + str(args.include_dir.resolve())]
    save('environment.json', {'compiler': subprocess.check_output(
        [args.cc, '--version'], text=True), 'benchmark_flags': flags,
        'inputs': initial, 'library_build_flags': 'Caller-supplied; see README'})
    for arm, library in libraries.items():
        subprocess.run([args.cc, *flags, str(source), str(library), '-o', str(out / arm)],
                       check=True, timeout=30)
    rows = []
    with (out / 'results.jsonl').open('x', encoding='utf-8') as results, \
            (out / 'journal.jsonl').open('x', encoding='utf-8') as journal:
        for block, order in enumerate(ORDERS):
            for name, kind, count, rounds in (CASES if block % 2 == 0 else reversed(CASES)):
                for arm in order:
                    key = {'block': block, 'case': name, 'arm': arm}
                    journal.write(json.dumps({'event': 'START', **key}) + '\n')
                    journal.flush()
                    p = subprocess.run([str(out / arm), str(kind), str(count), str(rounds)],
                                       capture_output=True, text=True, timeout=8)
                    journal.write(json.dumps({'event': 'FINISH', **key,
                                             'exit_code': p.returncode, 'stderr': p.stderr}) + '\n')
                    journal.flush()
                    if p.returncode or p.stderr:
                        raise RuntimeError((key, p.returncode, p.stderr))
                    value = json.loads(p.stdout)
                    assert (value['kind'], value['count'], value['rounds']) == (kind, count, rounds)
                    assert (value['input_bytes'], value['input_fnv64']) == expected_input(kind, count)
                    assert value['full_preflight_verified'] is True
                    assert all(math.isfinite(value[k]) and value[k] > 0
                               for k in ('wall_seconds', 'cpu_seconds'))
                    row = {**key, **value}
                    rows.append(row)
                    results.write(json.dumps(row) + '\n')
                    results.flush()
            print(json.dumps({'completed_block': block}), flush=True)
    assert len(rows) == 112
    summaries = []
    for name, kind, count, rounds in CASES:
        paired = []
        for block in range(4):
            selected = {r['arm']: r for r in rows if r['case'] == name and r['block'] == block}
            assert set(selected) == {'baseline', 'candidate'}
            paired.append(selected['baseline']['wall_seconds'] / selected['candidate']['wall_seconds'])
        rng = random.Random(20261025)
        boots = sorted(statistics.median(paired[rng.randrange(4)] for _ in range(4))
                       for _ in range(20000))
        summaries.append({'case': name, 'paired_speedups': paired,
                          'median_speedup': statistics.median(paired),
                          'bootstrap_median_95': [boots[499], boots[19499]]})
    assert initial == {str(p): digest(p) for p in inputs}
    save('analysis.json', {'cases': summaries, 'processes': len(rows),
                          'scope': 'One-host load cycles; four paired blocks, not application throughput or a universal guarantee'})
    print(json.dumps({'state': 'COMPLETE', 'processes': len(rows), 'output': str(out)}))


if __name__ == '__main__':
    main()
