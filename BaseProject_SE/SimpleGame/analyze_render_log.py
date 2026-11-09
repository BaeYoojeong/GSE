"""Analyze renderer CSV sessions without building or launching the game (Python 3.9+)."""
import argparse
import collections
import csv
import datetime
import math
from pathlib import Path
import statistics


def read_rows(directory, prefix):
    rows = []
    for path in sorted(directory.glob(prefix + '-*.csv')):
        with path.open(encoding='utf-8-sig', newline='') as stream:
            for row in csv.DictReader(stream):
                # A process killed during a write may leave one incomplete row.
                if None in row or None in row.values():
                    continue
                rows.append(row)
    return rows


def number(row, key):
    try:
        value = float(row.get(key, 0))
        return value if math.isfinite(value) else 0
    except (TypeError, ValueError):
        return 0


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered)-1, math.ceil(len(ordered)*fraction)-1)]


def frames(directory, skip):
    unique = {int(number(row, 'frame')): row for row in read_rows(directory, 'frames')}
    return [row for frame, row in sorted(unique.items()) if frame > skip]


def summarize(directory, skip):
    rows = frames(directory, skip)
    if not rows:
        raise ValueError(f'No complete frame records after frame {skip}: {directory}')
    start, end = int(number(rows[0], 'frame')), int(number(rows[-1], 'frame'))
    lines = [f'Session: {directory.resolve()}', f'Frames: {len(rows)} ({start}..{end}); skipped frame IDs <= {skip}',
             f'Unavailable frame IDs inside retained range: {end-start+1-len(rows)}',
             'CPU timings include render preparation, cache IO, logging inside passes, and swap wait.',
             'They are NOT GPU timings. Ring logs may no longer contain early frames/events.', '']
    means = {}
    for key in ['draw_calls', 'instanced_calls', 'instances', 'cpu_render_swap_ms', 'upload_bytes',
                'mesh_builds', 'build_ms', 'disk_ms', 'resident_bytes', 'text_builds']:
        values = [number(row, key) for row in rows]
        means[key] = statistics.mean(values)
        lines.append(f'{key}: avg={means[key]:.3f}, p50={percentile(values,.50):.3f}, '
                     f'p95={percentile(values,.95):.3f}, max={max(values):.3f}')
    totals = {key: sum(number(row, key) for row in rows) for key in
              ['memory_hits', 'memory_misses', 'disk_hits', 'disk_misses', 'disk_writes', 'evictions',
               'cache_failures', 'useful_vertices', 'submitted_vertices', 'mesh_builds']}
    lookup_count = totals['memory_hits'] + totals['memory_misses']
    lines += ['', f'Memory hit rate: {100*totals["memory_hits"]/lookup_count:.2f}%' if lookup_count else 'No memory lookups',
              f'Weighted vertex padding: {totals["submitted_vertices"]/totals["useful_vertices"]:.3f}x'
              if totals['useful_vertices'] else 'No instanced vertices',
              'Totals: ' + ', '.join(f'{key}={value:.0f}' for key, value in totals.items())]
    lines.append('\nPass averages (draw calls / CPU ms):')
    for name in ['ground', 'shadow', 'world', 'post', 'ui', 'other']:
        calls = statistics.mean(number(row, name+'_calls') for row in rows)
        ms = statistics.mean(number(row, name+'_cpu_ms') for row in rows)
        lines.append(f'  {name}: {calls:.3f} / {ms:.3f}')
    batches = [row for row in read_rows(directory, 'batches') if start <= number(row, 'frame') <= end]
    reasons = collections.Counter(row['flush_reason'] for row in batches)
    events = [row for row in read_rows(directory, 'events') if start <= number(row, 'frame') <= end]
    builds = collections.Counter(row['detail'] for row in events if row['category'] == 'mesh_build')
    faults = collections.Counter(row['category'] for row in events if any(part in row['category'] for part in
                                       ['failed', 'invalid', 'full', 'fallback']))
    lines += ['\nFlush reasons (retained batch records): ' + str(dict(reasons)),
              'Fault/fallback events (retained records): ' + str(dict(faults)),
              'Most frequently generated mesh keys:']
    lines += [f'  {key}: {count}' for key, count in builds.most_common(15)]
    lines.append('\nInvestigation priorities (indicators, not proven diagnoses):')
    if totals['evictions'] and totals['mesh_builds']:
        lines.append('- Evictions and builds coexist: inspect hot-key churn, cache limits and whether movement exposed new geometry.')
    if reasons['texture']:
        lines.append('- Texture changes split batches: compare UI calls/text_builds; consider a text atlas if UI dominates.')
    if reasons['vertex_padding']:
        lines.append('- Vertex-size variance splits batches: inspect padding versus CPU draw overhead before increasing the limit.')
    if totals['cache_failures'] or faults:
        lines.append('- Inspect events CSV for corrupt files, disk permissions, atlas pressure and fallback rendering.')
    if means['text_builds']:
        lines.append('- Textures are regenerated: inspect frequently changing text strings separately from mesh geometry.')
    if means['build_ms'] == 0 and means['mesh_builds'] == 0:
        lines.append('- No geometry generation in the retained range; CPU/GPU costs can still remain in uploads, draws and post-processing.')
    lines.append('- Compare identical routes, camera, settings, warm-up and console cadence; fewer calls alone do not prove faster GPU rendering.')
    return lines, means


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--skip-frames', type=int, default=0)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.skip_frames < 0:
        parser.error('--skip-frames must be nonnegative')
    try:
        lines, current = summarize(args.session, args.skip_frames)
        if args.baseline:
            _, baseline = summarize(args.baseline, args.skip_frames)
            lines += ['', f'Baseline: {args.baseline.resolve()}', 'Average changes (matching conditions are the operator\'s responsibility):']
            for key in ['draw_calls', 'cpu_render_swap_ms', 'upload_bytes', 'mesh_builds']:
                change = (1-current[key]/baseline[key])*100 if baseline[key] else None
                lines.append(f'  {key}: baseline={baseline[key]:.3f}, current={current[key]:.3f}, '
                             + (f'reduction={change:.2f}%' if change is not None else 'reduction=n/a (zero baseline)'))
    except (OSError, ValueError, csv.Error) as error:
        parser.exit(1, f'Analysis failed: {error}\n')
    report = '\n'.join(lines)+'\n'
    output = args.output or args.session / ('analysis-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'.txt')
    output.write_text(report, encoding='utf-8')
    print(report)
    print(f'Report written to: {output.resolve()}')


if __name__ == '__main__':
    main()
