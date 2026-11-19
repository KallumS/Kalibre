#!/usr/bin/env python3
"""Brings the family's engines in Kalibre up to date with their repositories.

    python3 tools/update_engines.py                  # which engines have new commits
    python3 tools/update_engines.py update Midular   # copy Midular's new version in, and check it
    python3 tools/update_engines.py update all       # every engine with new commits
    python3 tools/update_engines.py check Midular    # the checks only, nothing copied
    ... --ref BRANCH                                  # from another branch than the one recorded

Reads Engines/engines.json (what comes from where, and the commit Kalibre has).
Copies the files, records the new commit there and in Engines/VENDORED.md, and
says what needs a closer look: things a copy alone can't take care of. See
docs/UPDATING-ENGINES.md. Run from Kalibre's root; needs git and network.
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, 'Engines', 'engines.json')
VENDORED = os.path.join(ROOT, 'Engines', 'VENDORED.md')

# What Kalibre gives the scripts: its REAPER, host and ReaImGui stand-ins.
REAPER_STANDINS = ['Source/App/ReaperScripts.cpp', 'Source/Engine/LuaTool/ToolSession.cpp', 'Source/Engine/LuaTool/GfxTool.cpp']
IMGUI_STANDIN = 'Source/Engine/LuaTool/ImGuiLua.cpp'


def git(args, cwd=None, check=True):
    r = subprocess.run(['git'] + args, cwd=cwd, capture_output=True, text=True)
    if check and r.returncode != 0:
        raise RuntimeError('git %s: %s' % (' '.join(args), r.stderr.strip()))
    return r.stdout


def remote_head(repo, ref):
    out = git(['ls-remote', 'https://github.com/%s' % repo, ref], check=False)
    for line in out.splitlines():
        sha, name = line.split('\t')
        if name in ('refs/heads/' + ref, 'refs/tags/' + ref, ref):
            return sha
    return None


def fetch(repo, ref, old, into):
    """The repository at `ref`, and the commit Kalibre has, side by side."""
    git(['clone', '-q', '--depth', '1', '-b', ref, 'https://github.com/%s' % repo, into])
    git(['fetch', '-q', '--depth', '1', 'origin', old], cwd=into, check=False)
    return git(['rev-parse', 'HEAD'], cwd=into).strip()


def at(clone, commit, path):
    """A file's text at a commit, or None."""
    r = subprocess.run(['git', 'show', '%s:%s' % (commit, path)], cwd=clone, capture_output=True)
    return r.stdout.decode('utf-8', 'replace') if r.returncode == 0 else None


def read(path):
    try:
        with open(path, 'rb') as f:
            return f.read()
    except OSError:
        return None


def dests(d):
    return d if isinstance(d, list) else [d]


# ---------------------------------------------------------------------------------------- checks

def slider_lines(text):
    out = {}
    for m in re.finditer(r'^slider(\d+):([^\n]*)$', text or '', re.M):
        name = m.group(2).split('>', 1)[-1].lstrip('-').strip()
        out[int(m.group(1))] = (name, m.group(2))
    return out


def check_jsfx(path, old, new, notes):
    """Sliders are saved, automated and MIDI-learned by number: those that moved matter."""
    before, after = slider_lines(old), slider_lines(new)
    for n, (name, line) in before.items():
        if n not in after:
            notes.append('%s: slider %d (%s) is gone: songs saved with it lose its setting and automation' % (path, n, name))
        elif after[n][0] != name:
            notes.append('%s: slider %d was "%s", now "%s": saved songs and automation on it now move the new one' % (path, n, name, after[n][0]))
        elif after[n][1] != line:
            notes.append('%s: slider %d (%s) changed its default or range: fine, unless saved values now mean something else' % (path, n, name))
    added = sorted(set(after) - set(before))
    if added:
        notes.append('%s: new sliders %s: nothing to do' % (path, ', '.join(str(n) for n in added)))
    for name in case_clashes(new):
        notes.append('%s: two spellings of %s - EEL2 ignores case, so they are one variable (a likely bug: fix it in its repo)' % (path, ' and '.join(name)))


def case_clashes(text):
    """Names spelled two ways inside one function, or among globals (EEL2 ignores case)."""
    code = re.sub(r'/\*.*?\*/', ' ', text or '', flags=re.S)
    code = re.sub(r'//[^\n]*', ' ', code)
    code = re.sub(r'"(?:\\.|[^"\\])*"', '""', code)
    code = '\n'.join(l for l in code.split('\n') if not re.match(r'\s*(slider\d+:|desc:|tags:|options:|import |in_pin:|out_pin:|@\w+)', l))
    name = re.compile(r'(?<![\w.#$])[A-Za-z_]\w*')
    found, globals_ = [], []
    pieces = re.split(r'\bfunction\s+', code)
    globals_.append(pieces[0])
    for piece in pieces[1:]:
        m = re.match(r'(\w+)\s*\(([^)]*)\)\s*((?:(?:local|instance|global|globals)\s*\([^)]*\)\s*)*)', piece)
        own = set()
        if m:
            own = {w.lower() for w in name.findall(m.group(2) + ' ' + m.group(3)) if w not in ('local', 'instance', 'global', 'globals')}
        spell = {}
        for w in name.findall(piece):
            spell.setdefault(w.lower(), set()).add(w)
        found += [sorted(v) for v in spell.values() if len(v) > 1]
        globals_.append(' '.join(w for w in name.findall(piece[m.end() if m else 0:]) if w.lower() not in own))
    spell = {}
    for w in name.findall(' '.join(globals_)):
        spell.setdefault(w.lower(), set()).add(w)
    found += [sorted(v) for v in spell.values() if len(v) > 1]
    unique = []
    for f in found:
        if f not in unique:
            unique.append(f)
    return unique


def provided(paths, pattern):
    names = set()
    for p in paths:
        text = (read(os.path.join(ROOT, p)) or b'').decode('utf-8', 'replace')
        names |= set(re.findall(pattern, text))
    return names


def check_lua(path, new, notes):
    """REAPER and ReaImGui calls Kalibre's stand-ins don't answer yet."""
    reaper = provided(REAPER_STANDINS, r'"([A-Za-z_][A-Za-z0-9_]*)"')
    imgui = provided([IMGUI_STANDIN], r'\{"([A-Za-z_][A-Za-z0-9_]*)",')
    used = set(re.findall(r'\breaper\.([A-Za-z_][A-Za-z0-9_]*)', new or ''))
    for n in sorted(used - reaper):
        guarded = re.search(r'(if|and|or|not)\s+reaper\.%s\b' % n, new or '')
        notes.append('%s: calls reaper.%s, which Kalibre doesn\'t answer%s' % (
            path, n, ' (it checks for it first, so this may be fine)' if guarded else ': add it to the stand-in, or the script stops there'))
    constants = r'^(Col|Key|Mod|StyleVar|WindowFlags|ChildFlags|Cond|TableFlags|SliderFlags|InputTextFlags|MouseButton|HoveredFlags|FocusedFlags|PopupFlags|ComboFlags|SelectableFlags|TreeNodeFlags|ButtonFlags|DrawFlags|ColorEditFlags|TabBarFlags|TabItemFlags)_'
    for n in sorted(set(re.findall(r'\bImGui\.([A-Za-z_][A-Za-z0-9_]*)', new or '')) - imgui):
        if not re.match(constants, n):
            notes.append('%s: calls ImGui.%s, which Kalibre\'s ReaImGui (ImGuiLua.cpp) hasn\'t got: add it' % (path, n))


def functions_of(text):
    return set(re.findall(r'^function\s+M\.(\w+)', text or '', re.M))


# ------------------------------------------------------------------------------------------ work

def update(engine, ref, copy, report):
    repo, old = engine['repo'], engine['commit']
    ref = ref or engine['ref']
    notes, changed = [], []
    with tempfile.TemporaryDirectory() as tmp:
        clone = os.path.join(tmp, 'repo')
        new = fetch(repo, ref, old, clone)
        report.append('%s: %s %s -> %s' % (engine['name'], ref, old[:7], new[:7]))
        if new == old:
            report.append('  already up to date')
            return False

        # The files.
        for src, dst in engine.get('files', {}).items():
            data = read(os.path.join(clone, src))
            if data is None:
                notes.append('%s is gone from the repository: Kalibre keeps its old copy (%s)' % (src, ', '.join(dests(dst))))
                continue
            for d in dests(dst):
                target = os.path.join(ROOT, d)
                if read(target) == data:
                    continue
                before = (read(target) or b'').decode('utf-8', 'replace')
                changed.append(d)
                if src.endswith('.jsfx') or src.endswith('.jsfx-inc'):
                    check_jsfx(d, before, data.decode('utf-8', 'replace'), notes)
                if src.endswith('.lua'):
                    check_lua(d, data.decode('utf-8', 'replace'), notes)
                if copy:
                    os.makedirs(os.path.dirname(target), exist_ok=True)
                    with open(target, 'wb') as f:
                        f.write(data)
        for src, dst in engine.get('folders', {}).items():
            theirs = {os.path.relpath(os.path.join(dp, f), os.path.join(clone, src))
                      for dp, _, fs in os.walk(os.path.join(clone, src)) for f in fs}
            ours = {os.path.relpath(os.path.join(dp, f), os.path.join(ROOT, dst))
                    for dp, _, fs in os.walk(os.path.join(ROOT, dst)) for f in fs}
            for rel in sorted(theirs):
                data = read(os.path.join(clone, src, rel))
                if read(os.path.join(ROOT, dst, rel)) != data:
                    changed.append(os.path.join(dst, rel))
                    if rel not in ours and rel.endswith(('.cpp', '.mm')):
                        notes.append('new source file %s: check the build picks it up (cmake/App.cmake)' % os.path.join(dst, rel))
                    if copy:
                        os.makedirs(os.path.dirname(os.path.join(ROOT, dst, rel)), exist_ok=True)
                        with open(os.path.join(ROOT, dst, rel), 'wb') as f:
                            f.write(data)
            for rel in sorted(ours - theirs):
                changed.append(os.path.join(dst, rel) + ' (removed)')
                if copy:
                    os.remove(os.path.join(ROOT, dst, rel))

        # Files beside the ones carried that aren't: an import, a new module, a source file.
        carried = set(engine.get('files', {})) | set(engine.get('standins', {})) | set(engine.get('hosts', [])) | set(engine.get('ignore', []))
        folders = {os.path.dirname(s) for s in engine.get('files', {})}
        for f in git(['ls-files'], cwd=clone).splitlines():
            if os.path.dirname(f) in folders and f not in carried and re.search(r'\.(jsfx|jsfx-inc|lua|cpp|h)$', f):
                if not any(f.startswith(x) for x in engine.get('folders', {})):
                    notes.append('%s is in the repository beside the files Kalibre carries, but not carried: needed?' % f)

        # Kalibre's own stand-ins for the repository's REAPER-facing files.
        for src, ours in engine.get('standins', {}).items():
            a, b = at(clone, old, src), at(clone, 'HEAD', src)
            if a != b:
                gone, came = functions_of(a) - functions_of(b), functions_of(b) - functions_of(a)
                notes.append('%s changed; Kalibre stands in for it with %s: make the same change there%s%s' % (
                    src, ours, ('; new functions: ' + ', '.join(sorted(came))) if came else '',
                    ('; gone: ' + ', '.join(sorted(gone))) if gone else ''))
        for src in engine.get('hosts', []):
            if at(clone, old, src) != at(clone, 'HEAD', src):
                notes.append('%s (the host Kalibre\'s own was adapted from) changed: see whether Kalibre\'s wants the same' % src)

        # A port: nothing copied, the change to make by hand.
        if engine['kind'] == 'port':
            stat = git(['diff', '--stat', old, 'HEAD', '--'] + engine['watch'], cwd=clone, check=False).strip()
            if stat:
                notes.append('the model changed, and Kalibre\'s %s is a port of it: see docs/UPDATING-ENGINES.md, "Trombolese"\n      %s' % (
                    engine['port'], stat.replace('\n', '\n      ')))
            else:
                notes.append('nothing in the model changed: only record the new commit')

        if engine['kind'] == 'juce' and engine.get('built'):
            for f in git(['ls-files', 'Source'], cwd=clone).splitlines():
                if f.endswith('.cpp') and f not in engine.get('files', {}):
                    notes.append('new source file %s: add it to engines.json and to %s' % (f, engine['built']))

        for c in changed:
            report.append('  %s %s' % ('copied' if copy else 'would copy', c))
        for n in notes:
            report.append('  LOOK: ' + n)
        if not changed and engine['kind'] != 'port':
            report.append('  no carried file changed')

        if copy:
            engine['commit'] = new
            engine['ref'] = ref
            text = (read(VENDORED) or b'').decode('utf-8')
            text = text.replace('| %s |' % old[:7], '| %s |' % new[:7])
            with open(VENDORED, 'w') as f:
                f.write(text)
        return True


def main(argv):
    with open(MANIFEST) as f:
        manifest = json.load(f)
    engines = manifest['engines']
    ref = None
    if '--ref' in argv:
        i = argv.index('--ref')
        ref = argv[i + 1]
        del argv[i:i + 2]
    command = argv[0] if argv else 'status'

    if command == 'status':
        for e in engines:
            head = remote_head(e['repo'], e['ref'])
            state = 'unreachable' if not head else 'up to date' if head == e['commit'] else 'NEW: %s' % head[:7]
            print('%-28s %-24s %s  %s' % (e['name'], e['ref'], e['commit'][:7], state))
        return 0

    if command in ('update', 'check') and len(argv) > 1:
        wanted = ' '.join(argv[1:])
        chosen = [e for e in engines if wanted == 'all' or e['name'].lower() == wanted.lower()]
        if not chosen:
            print('No engine called "%s". They are: %s' % (wanted, ', '.join(e['name'] for e in engines)))
            return 1
        if wanted == 'all':
            chosen = [e for e in chosen if remote_head(e['repo'], ref or e['ref']) not in (None, e['commit'])]
        report = []
        for e in chosen:
            update(e, ref, command == 'update', report)
        print('\n'.join(report) if report else 'Everything is up to date.')
        if command == 'update' and chosen:
            with open(MANIFEST, 'w') as f:
                json.dump(manifest, f, indent=2)
                f.write('\n')
            print('\nThen build and test (CLAUDE.md), look at each LOOK, and commit.')
        return 0

    print(__doc__)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
