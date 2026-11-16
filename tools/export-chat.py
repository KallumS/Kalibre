# Writes a session transcript (Claude Code .jsonl) as a readable chat backup in docs/sessions.
# python3 -I tools/export-chat.py <transcript.jsonl> docs/sessions/<date>-chat-history.md
# Edit the heading text below for a new part.
import json, re, sys
src, out = sys.argv[1], sys.argv[2]
EMAIL = re.compile(r'[A-Za-z0-9._%+-]+@gmail\.com')
REMIND = re.compile(r'<system-reminder>.*?</system-reminder>', re.S)

def clean(t):
    t = REMIND.sub('', t)
    t = EMAIL.sub('(email removed)', t)
    return t.strip()

def when(d):
    ts = d.get('timestamp', '')
    return ts[:10] + ' ' + ts[11:16] if ts else ''

def toolLine(b):
    name, inp = b.get('name', ''), b.get('input', {}) or {}
    if 'description' in inp and isinstance(inp['description'], str): what = inp['description']
    elif 'file_path' in inp: what = inp['file_path']
    elif 'subject' in inp: what = inp['subject']
    elif 'query' in inp: what = str(inp['query'])
    elif 'method' in inp: what = inp['method']
    elif 'files' in inp: what = ', '.join(inp['files']) if isinstance(inp['files'], list) else str(inp['files'])
    else:
        vals = [str(v) for v in inp.values() if isinstance(v, (str, int))]
        what = vals[0] if vals else ''
    what = EMAIL.sub('(email removed)', what.replace('\n', ' '))
    if len(what) > 140: what = what[:137] + '...'
    return f'- *{name}*: {what}'

lines = []
mine = replies = tools = 0
open_claude = False
for raw in open(src):
    d = json.loads(raw)
    t = d.get('type')
    if t == 'user' and not d.get('isMeta') and not d.get('isCompactSummary'):
        c = d['message']['content']
        text = c if isinstance(c, str) else '\n'.join(b.get('text', '') for b in c if b.get('type') == 'text')
        text = clean(text)
        if not text or text.startswith('<task-notification>'): continue
        if text == '[Request interrupted by user]':
            lines.append(f'*(Interrupted, {when(d)}: Claude stopped there, before doing any of it.)*\n'); open_claude = False; continue
        lines.append(f'## You ({when(d)})\n\n{text}\n'); mine += 1; open_claude = False
    elif t == 'attachment' and d.get('attachment', {}).get('type') == 'queued_command':
        text = clean(str(d['attachment'].get('prompt', '')))
        if not text or text.startswith('<task-notification>'): continue
        lines.append(f'## You ({when(d)}, while Claude was working)\n\n{text}\n'); mine += 1; open_claude = False
    elif t == 'assistant':
        for b in d['message'].get('content', []):
            if b.get('type') == 'text' and b.get('text', '').strip():
                lines.append(f'## Claude ({when(d)})\n\n{clean(b["text"])}\n'); replies += 1; open_claude = True
            elif b.get('type') == 'tool_use':
                if not open_claude: lines.append(f'## Claude ({when(d)})\n'); open_claude = True
                lines.append(toolLine(b)); tools += 1
        # a tool line list ends with a blank line before the next heading
        if lines and lines[-1].startswith('- '): pass
body = []
for l in lines:
    if l.startswith('## ') and body and body[-1].startswith('- '): body.append('')
    body.append(l)
head = f'''# Chat history: building Kalibre (16 November 2026)

The second part of the conversation, from the first test on the Mac (the
plug-in scan crash) on. The first part is in `2026-11-15-chat-history.md`.
Kept so the reasons and the route aren't lost when the session's context is
compacted. Your messages and Claude's replies are in full; each tool call (a
command, a file written, a search) is listed in one line by what it did,
without its output. Private details (an email address) and the harness's
internal reminders are removed, as are the automatic "background task
finished" notices. Messages you sent while Claude was working are marked so.

{mine} messages from you, {replies} replies, {tools} tool calls.

'''
open(out, 'w').write(head + '\n'.join(body) + '\n')
print(mine, replies, tools)
