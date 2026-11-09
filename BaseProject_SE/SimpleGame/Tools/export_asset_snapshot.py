"""Export a read-only Blender source into an atomically published runtime overlay.

Run with Blender --background --disable-autoexec source.blend --python this.py -- ...
The source .blend and original GLBs are never saved or overwritten.
"""
import argparse
import json
import os
import re
import shutil
import struct
import sys
import time
import traceback
from pathlib import Path
import bpy


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    args.output.mkdir(parents=True, exist_ok=True)
    generation = args.output / ('generation_' + str(time.time_ns()))
    generation.mkdir()
    report = {'schema': 'ashen_export_report_1', 'assets': [], 'success': False}
    started = time.monotonic()
    try:
        source = Path(bpy.data.filepath)
        stamp = source.stat().st_mtime_ns
        catalog = json.loads(args.manifest.read_text(encoding='utf-8-sig'))
        if catalog['schema'] != 'village_assets_1.0':
            raise ValueError('unsupported manifest')
        for collection in bpy.data.collections:
            collection.hide_viewport = collection.hide_render = False

        def visible(layer):
            layer.exclude = layer.hide_viewport = False
            for child in layer.children:
                visible(child)
        visible(bpy.context.view_layer.layer_collection)
        entries = [('NPC_Modular_Library', [p['mesh'] for p in catalog['npc_parts']], 'RIG_Human')]
        entries += [(p['id'], p['meshes'], p.get('rig', 'RIG_Human')) for p in catalog['npc_presets']]
        entries += [(p['id'], p['meshes'], p['rig']) for p in catalog['animals']]
        entries += [(p['id'], p['meshes'], None) for p in catalog['environment']]
        mapping = {}
        for asset_id, meshes, rig_name in entries:
            if not re.fullmatch(r'[A-Za-z0-9_\-]{1,128}', asset_id):
                raise ValueError('unsafe asset id')
            objects = [bpy.data.objects[name] for name in meshes]
            if rig_name:
                rig = bpy.data.objects[rig_name]
                objects.insert(0, rig)
                rig.location = (0, 0, 0)
                if rig.animation_data:
                    rig.animation_data.action = None
                    for track in rig.animation_data.nla_tracks:
                        track.mute = False
            bpy.ops.object.select_all(action='DESELECT')
            for obj in objects:
                obj.hide_set(False)
                obj.hide_viewport = False
                obj.select_set(True)
            bpy.context.view_layer.objects.active = objects[0]
            bpy.context.scene.frame_set(1)
            path = generation / (asset_id + '.glb')
            bpy.ops.export_scene.gltf(filepath=str(path), use_selection=True, export_format='GLB',
                export_extras=True, export_animations=bool(rig_name), export_animation_mode='NLA_TRACKS', export_apply=False)
            data = path.read_bytes()
            magic, version, size, json_size, kind = struct.unpack_from('<5I', data)
            if magic != 0x46546C67 or version != 2 or size != len(data) or kind != 0x4E4F534A:
                raise ValueError('invalid exported GLB header')
            document = json.loads(data[20:20 + json_size])
            exported = {n.get('name') for n in document.get('nodes', [])}
            if not set(meshes) <= exported:
                raise ValueError('export dropped selected meshes: ' + asset_id)
            clips = {clip.get('name') for clip in document.get('animations', [])}
            if rig_name and not set(catalog['clips']) <= clips:
                raise ValueError('export dropped animation clips: ' + asset_id)
            mapping[asset_id] = path.relative_to(args.output).as_posix()
            report['assets'].append({'id': asset_id, 'bytes': len(data), 'clips': sorted(clips)})
            if rig_name and rig.animation_data:
                for track in rig.animation_data.nla_tracks:
                    track.mute = True
        if source.stat().st_mtime_ns != stamp:
            raise ValueError('source changed during export; retaining previous overlay')
        overlay = {'schema': 'ashen_runtime_overlay_1', 'source': str(source), 'assets': mapping}
        temp = args.output / 'current.tmp'
        temp.write_text(json.dumps(overlay, ensure_ascii=False, indent=2), encoding='utf-8')
        os.replace(temp, args.output / 'current.json')
        report['success'] = True
        # Retain the active and previous snapshots. Delete only our verified generation directories.
        output_root = args.output.resolve()
        generations = sorted((p for p in output_root.iterdir()
            if re.fullmatch(r'generation_[0-9]+', p.name) and p.is_dir()), key=lambda p: p.name, reverse=True)
        for obsolete in generations[2:]:
            resolved = obsolete.resolve()
            if resolved.parent == output_root and not obsolete.is_symlink():
                try:
                    shutil.rmtree(resolved)
                except OSError:
                    pass  # The game may still be reading an old file; a future export retries cleanup.
    except Exception:
        report['error'] = traceback.format_exc()
        raise
    finally:
        report['seconds'] = time.monotonic() - started
        (args.output / 'export_report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
