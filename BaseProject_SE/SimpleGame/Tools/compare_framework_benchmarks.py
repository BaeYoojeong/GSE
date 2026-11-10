"""Compare repeated --benchmark / --benchmark-live runs using only Python's standard library."""
import argparse
import csv
import json
import statistics
from pathlib import Path


def compare(root):
    records = [(path, json.loads(path.read_text(encoding='utf-8'))) for path in root.glob('*/framework_benchmark.json')]
    if not records:
        raise ValueError('no benchmark runs found')
    controls = ('world_seed', 'warmup_frames', 'measured_frames', 'vsync', 'gpu_vendor', 'gpu_renderer',
                'opengl_version', 'image_width', 'image_height', 'framebuffer')
    reference = records[0][1]
    for path, record in records:
        if any(record[key] != reference[key] for key in controls):
            raise ValueError('incomparable benchmark controls: ' + str(path))
        if record['gl_errors']:
            raise ValueError('OpenGL errors: ' + str(path))
    results = {'method': 'median of independent run medians; identical seed and framebuffer; swapped execution order',
               'regression_limit_percent': 5, 'controls': {key: reference[key] for key in controls}, 'scenes': {}}
    for scene in sorted({record['mode'] for _, record in records}):
        groups = {backend: [(path, record) for path, record in records if record['mode'] == scene and record['backend'] == backend]
                  for backend in ('FreeGLUT', 'GLFW')}
        if any(len(group) < 2 for group in groups.values()):
            raise ValueError('need at least two independent runs for both backends: ' + scene)
        identity, counters = set(), set()
        row = {'runs': {}, 'metrics': {}}
        for backend, group in groups.items():
            row['runs'][backend] = []
            for path, record in group:
                identity.add(record['image_hash_fnv1a64'])
                with (path.parent / 'framework_frames.csv').open(newline='') as stream:
                    sequence = tuple((f['draw_calls'], f['mesh_builds'], f['upload_bytes']) for f in csv.DictReader(stream))
                counters.add(sequence)
                row['runs'][backend].append({'run': path.parent.name, 'cpu_frame_ms': record['cpu_frame_ms'],
                    'gpu_ms': record['gpu_ms'], 'draw_calls': record['draw_calls'], 'image_hash_fnv1a64': record['image_hash_fnv1a64']})
        row['identical_rendered_pixels'] = len(identity) == 1
        row['identical_per_frame_draw_build_upload_counters'] = len(counters) == 1
        for metric in ('cpu_frame_ms', 'gpu_ms'):
            before = statistics.median(record[metric]['median'] for _, record in groups['FreeGLUT'])
            after = statistics.median(record[metric]['median'] for _, record in groups['GLFW'])
            row['metrics'][metric] = {'FreeGLUT_median_ms': before, 'GLFW_median_ms': after,
                'change_percent': 100 * (after / before - 1),
                'FreeGLUT_p95_ms': statistics.median(record[metric]['p95'] for _, record in groups['FreeGLUT']),
                'GLFW_p95_ms': statistics.median(record[metric]['p95'] for _, record in groups['GLFW'])}
        row['no_material_regression'] = all(value['change_percent'] <= 5 for value in row['metrics'].values())
        results['scenes'][scene] = row
    results['passed'] = all(row['no_material_regression'] and row['identical_rendered_pixels'] and
        row['identical_per_frame_draw_build_upload_counters'] for row in results['scenes'].values())
    results['scope'] = 'This PC, hidden windows, these two controlled workloads; not a guarantee for every driver or scene.'
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runs', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = compare(args.runs)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    for scene, row in report['scenes'].items():
        print(scene, json.dumps(row['metrics']), 'pixels_equal=', row['identical_rendered_pixels'])
    raise SystemExit(0 if report['passed'] else 1)
