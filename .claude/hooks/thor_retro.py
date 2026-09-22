"""Session Stop hook: the end-of-work ritual, enforced (user, 2026-09-21).

Fires when Claude wants to stop. It blocks the stop (decision=block) until:
  1. src/, tools/, android/ and AGENTS.md have no uncommitted changes and
     nothing is unpushed (the user's standing "git commit push");
  2. a retro marker for today exists: .claude/retro-<YYYYMMDD>.done. Claude
     creates it after writing the three retro lines into AGENTS.md's state
     (what was slow, which tool or reflex would have made it fast, whether
     that tool now exists) - the "every issue powers up the MCP" rule made
     mechanical.
Everything else passes. Delete this hook from .claude/settings.local.json
to switch it off; it is a session-local convenience, not a repo rule.
"""
import datetime
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))


def git(*args):
    try:
        return subprocess.run(['git', *args], cwd=ROOT, capture_output=True, text=True,
                              timeout=30).stdout.strip()
    except Exception:
        return ''


def main():
    try:
        sys.stdin.read()
    except Exception:
        pass
    problems = []
    dirty = [l for l in git('status', '--porcelain', '--', 'src', 'tools', 'android', 'AGENTS.md', 'CLAUDE.md').splitlines()
             if l and not l.startswith('??')]
    if dirty:
        problems.append('%d modified tracked files under src/tools/android/AGENTS.md are not committed' % len(dirty))
    unpushed = git('log', '--oneline', 'origin/master..HEAD')
    if unpushed:
        problems.append('%d commits are not pushed' % len(unpushed.splitlines()))
    marker = os.path.join(ROOT, '.claude', 'retro-%s.done' % datetime.date.today().strftime('%Y%m%d'))
    if not os.path.exists(marker):
        problems.append('no retro yet today: add three lines to AGENTS.md section 8 (what was slow, '
                        'which tool or reflex would have made it fast, whether it exists now), commit, '
                        'then create ' + os.path.relpath(marker, ROOT))
    if problems:
        reason = 'Stop ritual (thor_retro.py): ' + '; '.join(problems) + '. Do these, then stop.'
        print(json.dumps({'decision': 'block', 'reason': reason}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
