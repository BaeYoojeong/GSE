"""Run with Blender --background --python this_file -- --output <Assets/Blender>."""
import bpy
import sys
import math
import json
import argparse
from pathlib import Path
from mathutils import Vector

args = argparse.ArgumentParser()
args.add_argument('--output', required=True)
options = args.parse_args(sys.argv[sys.argv.index('--') + 1:])
out = Path(options.output).resolve()
out.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
palette = {}
assets = []

def material(name, color, emission=1):
    if name not in palette:
        m = bpy.data.materials.new(name)
        m.diffuse_color = (*color, 1)
        m['game_emission'] = emission
        palette[name] = m
    return palette[name]

stone = material('Charcoal limestone', (.28, .31, .32))
edge = material('Cut stone edges', (.43, .45, .43))
wood = material('Aged oak', (.18, .12, .09))
gold = material('Old brass', (.68, .43, .16))
light = material('Warm window', (1, .58, .18), 3.6)
red = material('Ash ember', (1, .19, .07), 2.8)
roofs = [material('Slate '+str(i), c) for i,c in enumerate([(.20,.28,.30),(.35,.20,.17),(.24,.27,.19),(.29,.23,.34)])]
plasters = [material('Plaster '+str(i), c) for i,c in enumerate([(.53,.48,.37),(.47,.48,.43),(.50,.43,.36),(.42,.45,.50)])]
collection = None
offset = 0

def mesh(name, points, faces, mat):
    data = bpy.data.meshes.new(name)
    data.from_pydata(points, [], faces)
    data.update()
    obj = bpy.data.objects.new(name, data)
    collection.objects.link(obj)
    obj.location.x = offset
    obj.data.materials.append(mat)
    return obj

def box(name, lo, hi, mat):
    x,y,z = lo; X,Y,Z = hi
    return mesh(name, [(x,y,z),(X,y,z),(X,Y,z),(x,Y,z),(x,y,Z),(X,y,Z),(X,Y,Z),(x,Y,Z)],
        [(0,3,2,1),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7),(4,5,6,7)], mat)

def roof(z, mat, width=36, depth=29):
    return mesh('Pitched roof', [(-width,-depth,z),(width,-depth,z),(width,depth,z),(-width,depth,z),
        (-width,0,z+24),(width,0,z+24)], [(0,1,5,4),(4,5,2,3),(0,4,3),(1,2,5),(0,3,2,1)], mat)

def export(name):
    triangles = []
    view = Vector((1,1,2)).normalized()
    lamp = Vector((-.4,.6,1)).normalized()
    for obj in collection.objects:
        data = obj.data
        data.calc_loop_triangles()
        mat = data.materials[0]
        for face in data.loop_triangles:
            n = face.normal
            if n.dot(view) <= .001: continue
            points = [data.vertices[i].co for i in face.vertices]
            depth = sum(p.dot(view) for p in points)/3
            shade = .65 + .45 * max(0, n.dot(lamp))
            color = [min(1, float(c)*shade) for c in mat.diffuse_color[:3]]
            vertices = []
            for p in points:
                vertices.append((.707106781*(p.x-p.y), .3535533905*(p.x+p.y)-p.z,
                                 *color, 1, 0, 0, float(mat['game_emission'])))
            triangles.append((depth, vertices))
    triangles.sort(key=lambda item:item[0])
    vertices = [v for _,tri in triangles for v in tri]
    with (out/(name+'.ashmesh')).open('w', encoding='ascii') as f:
        f.write('ASHEN_MESH_V1 '+str(len(vertices))+'\n')
        for v in vertices: f.write(' '.join(format(n,'.7g') for n in v)+'\n')
    assets.append({'name':name,'vertices':len(vertices),'triangles':len(triangles)})

for index, name in enumerate(['cottage_0','cottage_1','cottage_2','cottage_3','tower_dark','tower_lit','keeper_dark','keeper_enraged']):
    collection = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(collection)
    offset = index * 160
    if name.startswith('cottage'):
        i = int(name[-1])
        box('Stone foundation',(-30,-24,0),(30,24,9),stone)
        box('Plastered walls',(-28,-22,9),(28,22,52),plasters[i])
        for x in [-27,0,25]: box('Timber frame',(x,22,9),(x+3,24,53),wood)
        box('Crossbeam',(-30,22,30),(30,25,34),wood)
        box('Door',(-7,24,9),(7,26,30),wood)
        for x in [-21,14]:
            box('Window frame',(x-2,24,36),(x+9,26,48),wood)
            box('Window glow',(x,26,38),(x+7,27,46),light)
        roof(53, roofs[i])
        box('Chimney',(15,-10,56),(24,0,90),stone)
        box('Front step',(-11,23,0),(11,32,4),edge)
    elif name.startswith('tower'):
        box('Foundation',(-34,-28,0),(34,28,10),stone)
        for x in [-26,18]:
            for y in [-20,12]: box('Pillar',(x,y,10),(x+8,y+8,106),edge)
        box('Bell support',(-29,-23,104),(29,23,113),wood)
        roof(113,roofs[0],35,28)
        box('Bell crown',(-8,-7,76),(8,7,91),gold)
        box('Bell body',(-15,-12,60),(15,12,78),gold)
        box('Bell rim',(-20,-15,56),(20,15,62),gold)
        box('Clapper',(-3,-3,48),(3,3,64),edge)
        box('Altar',(-18,24,10),(18,38,28),stone)
        for x in [-28,25]:
            box('Lantern',(x,24,34),(x+4,29,51),light if name.endswith('lit') else gold)
    else:
        for x in [-18,6]: box('Armored leg',(x,-9,0),(x+12,9,29),stone)
        box('Armor',(-23,-13,28),(23,13,67),stone)
        for x in [-33,23]: box('Shoulder',(x,-17,50),(x+10,17,73),edge)
        box('Helmet',(-17,-12,66),(17,12,91),gold)
        box('Helmet brim',(-23,-15,66),(23,15,71),gold)
        box('Eye slit',(-12,13,77),(12,15,81),red if name.endswith('enraged') else light)
        box('Bell hammer',(28,-6,7),(34,0,76),wood)
        box('Hammer head',(20,-14,68),(42,8,88),edge)
    export(name)

bpy.context.scene['game_projection'] = 'isometric 2.5D: x=.707(x-y), y=.3535(x+y)-z'
bpy.ops.wm.save_as_mainfile(filepath=str(out/'medieval_library.blend'))
(out/'manifest.json').write_text(json.dumps({'generator':'Blender','version':bpy.app.version_string,
    'projection':'fixed isometric','assets':assets},ensure_ascii=False,indent=2), encoding='utf-8')
print('ASHEN_ASSETS_READY',len(assets),sum(a['vertices'] for a in assets))
