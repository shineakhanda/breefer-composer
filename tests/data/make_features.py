"""Generates tests/data/features.bcproj + cube.obj: a project exercising
nested sequences, Basic 3D, a native plugin effect, a 3D model clip and an
adjustment layer. Render a frame with:
    breefer --render-frame tests/data/features.bcproj 1.5 out.png 640
"""
import json
import os

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.dirname(os.path.dirname(here))
T = 254016000000

vertices = [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1), (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]
faces = [(1, 2, 3, 4), (5, 8, 7, 6), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 8, 4), (5, 1, 4, 8)]
with open(os.path.join(here, 'cube.obj'), 'w') as out:
    out.write('# unit cube\n')
    for v in vertices:
        out.write('v %d %d %d\n' % v)
    for f in faces:
        out.write('f %d %d %d %d\n' % f)


def param(pid, name, value, lo, hi):
    return {"id": pid, "name": name, "value": value, "min": lo, "max": hi, "keys": []}


demo = os.path.join(repo, 'assets', 'demo.mp4').replace(os.sep, '/')
media = [{"id": "m_demo", "path": demo, "name": "demo.mp4", "duration": 5 * T, "width": 1280, "height": 720,
          "rateNum": 25, "rateDen": 1, "sampleRate": 48000, "channels": 2, "hasVideo": True, "hasAudio": True,
          "codec": "h264", "proxyPath": ""}]
nested = {"id": "seq_nested", "name": "Nested Bars", "width": 1280, "height": 720, "rateNum": 25, "rateDen": 1, "tracks": [
    {"id": "nv1", "name": "V1", "kind": "video", "clips": [
        {"id": "nc1", "mediaId": "m_demo", "start": 0, "duration": 2 * T, "sourceIn": 0, "speed": 1, "effects": []}]},
    {"id": "na1", "name": "A1", "kind": "audio", "clips": []}]}
main = {"id": "seq_main", "name": "Feature Test", "width": 1280, "height": 720, "rateNum": 25, "rateDen": 1, "tracks": [
    {"id": "v3", "name": "V3", "kind": "video", "clips": [
        {"id": "adj", "mediaId": "", "start": T, "duration": T, "sourceIn": 0, "speed": 1, "generator": 4, "effects": [
            {"id": "fxl", "type": "lumetri", "name": "Lumetri Color", "enabled": True,
             "params": [param("saturation", "Saturation", 0, 0, 200)]}]}]},
    {"id": "v2", "name": "V2", "kind": "video", "clips": [
        {"id": "cube", "mediaId": "", "start": 0, "duration": 2 * T, "sourceIn": 0, "speed": 1, "generator": 5,
         "model": os.path.join(here, 'cube.obj').replace(os.sep, '/'), "effects": [
            {"id": "fxm", "type": "model3d", "name": "3D Model", "enabled": True, "params": [
                param("rotateY", "Rotation Y", 35, -720, 720), param("scale", "Scale", 45, 1, 1000),
                param("red", "Red", 240, 0, 255), param("green", "Green", 160, 0, 255), param("blue", "Blue", 40, 0, 255)]}]}]},
    {"id": "v1", "name": "V1", "kind": "video", "clips": [
        {"id": "nestclip", "mediaId": "", "start": 0, "duration": 2 * T, "sourceIn": 0, "speed": 1, "generator": 3,
         "nested": "seq_nested", "effects": [
            {"id": "fxb", "type": "basic3d", "name": "Basic 3D", "enabled": True, "params": [
                param("swivel", "Swivel", 30, -180, 180), param("tilt", "Tilt", 10, -180, 180),
                param("distance", "Distance", 20, -90, 500)]},
            {"id": "fxp", "type": "plugin:org.breefer.sample.posterize", "name": "Posterize", "enabled": True,
             "params": [param("levels", "Levels", 3, 2, 32)]}]}]},
    {"id": "a1", "name": "A1", "kind": "audio", "clips": []}]}
project = {"format": "breefer-project", "version": 2, "id": "p", "name": "Feature Test", "media": media,
           "sequences": [main, nested]}
with open(os.path.join(here, 'features.bcproj'), 'w') as out:
    json.dump(project, out, indent=1)
print('wrote', os.path.join(here, 'features.bcproj'))
