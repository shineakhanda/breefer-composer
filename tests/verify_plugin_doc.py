"""Extracts the 'first plugin' example from docs/PLUGINS.md, builds it, and writes a test project."""
import json
import os
import re
import subprocess
import sys

repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = sys.argv[1]
os.makedirs(out, exist_ok=True)

doc = open(os.path.join(repo, 'docs', 'PLUGINS.md'), encoding='utf-8').read()
fence = chr(96) * 3
m = re.search(fence + r'c\n// invert\.c\n(.*?)' + fence, doc, re.S)
assert m, 'invert.c block not found in PLUGINS.md'
src = os.path.join(out, 'invert.c')
open(src, 'w').write(m.group(1))

subprocess.check_call(['gcc', '-O2', '-Wall', '-Wextra', '-shared', '-I', os.path.join(repo, 'sdk', 'include'),
                       src, '-o', os.path.join(out, 'invert.dll'), '-static-libgcc'])
print('compiled without warnings')

p = json.load(open(os.path.join(repo, 'tests', 'data', 'features.bcproj')))
main = p['sequences'][0]
main['tracks'] = [
    {"id": "v1", "name": "V1", "kind": "video", "clips": [
        {"id": "c1", "mediaId": "m_demo", "start": 0, "duration": 254016000000, "sourceIn": 0, "speed": 1, "effects": [
            {"id": "e1", "type": "plugin:com.example.invert", "name": "Invert", "enabled": True, "params": [
                {"id": "amount", "name": "Amount (%)", "value": 100, "min": 0, "max": 100, "keys": []}]}]}]},
    {"id": "a1", "name": "A1", "kind": "audio", "clips": []}]
p['sequences'] = [main]
json.dump(p, open(os.path.join(out, 'invert.bcproj'), 'w'))
print('project written')
