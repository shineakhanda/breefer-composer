"""Checks every relative link and #anchor in the repository's Markdown files.
Run from the repository root:  python tests/check_docs.py"""
import glob
import os
import re
import sys

FENCE = chr(96) * 3
files = glob.glob('*.md') + glob.glob('docs/*.md')


def strip_code(text):
    return re.sub(FENCE + r'.*?' + FENCE, '', text, flags=re.S)


def slug(heading):
    heading = heading.strip().lower()
    heading = re.sub(r'[^\w\- ]', '', heading)
    return heading.replace(' ', '-')


anchors = {}
for f in files:
    text = strip_code(open(f, encoding='utf-8').read())
    anchors[f.replace(os.sep, '/')] = {slug(m.group(2)) for m in re.finditer(r'^(#{1,6})\s+(.*)$', text, re.M)}

problems = 0
for f in files:
    text = strip_code(open(f, encoding='utf-8').read())
    for m in re.finditer(r'\]\(([^)\s]+)\)', text):
        link = m.group(1)
        if link.startswith(('http', 'mailto')):
            continue
        path, _, anchor = link.partition('#')
        target = os.path.normpath(os.path.join(os.path.dirname(f), path)) if path else f
        target = target.replace(os.sep, '/')
        if not os.path.exists(target):
            print('MISSING', f, '->', link)
            problems += 1
        elif anchor and target.endswith('.md') and anchor not in anchors.get(target, set()):
            print('BAD ANCHOR', f, '->', link)
            problems += 1
print('checked %d files, %d problem(s)' % (len(files), problems))
sys.exit(1 if problems else 0)
