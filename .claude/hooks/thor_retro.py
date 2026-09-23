"""Session Stop hook: the end-of-work ritual, enforced (user, 2026-09-21).

Fires when Claude wants to stop. It blocks the stop (decision=block) until:
  1. src/, tools/, android/ and AGENTS.md have no uncommitted changes and
     nothing is unpushed (the user's standing "git commit push");
  2. a retro marker for today exists: .claude/retro-<YYYYMMDD>.done. Claude
     creates it after writing the three retro lines into AGENTS.md's state
     (what was slow, which tool or reflex would have made it fast, whether
     that tool now exists) - the "every issue powers up the MCP" rule made
     mechanical;
  3. today's commits touch the harness (user, 2026-09-22: "every retro
     should lead to improvements in mcp and process"): tools/mcp, tools/thor,
     tools/pc, the in-app debug server, the trap record, or this hook. A
     retro paragraph without a tool change does not pass.
Everything else passes. Delete this hook from .claude/settings.local.json
to switch it off; it is a session-local convenience, not a repo rule.
"""
import datetime
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
HARNESS_PATHS = (
    'tools/mcp/', 'tools/thor/', 'tools/pc/', 'tools/renderdoc/', '.claude/hooks/',
    'android/android_studio_project/app/src/main/java/jp/xenia/emulator/DebugServer.java',
    'android/android_studio_project/app/src/main/java/jp/xenia/emulator/ScreenRoutes.java',
    'src/xenia/ui/debug_api_android.cc', 'src/xenia/kernel/util/kernel_trap',
)


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
    today = datetime.date.today()
    marker = os.path.join(ROOT, '.claude', 'retro-%s.done' % today.strftime('%Y%m%d'))
    if not os.path.exists(marker):
        problems.append('no retro yet today: add three lines to AGENTS.md section 8 (what was slow, '
                        'which tool or reflex would have made it fast, whether it exists now), commit, '
                        'then create ' + os.path.relpath(marker, ROOT))
    # The retro must have changed the harness: any file under the harness
    # paths in a commit made today (author date).
    touched = git('log', '--since=%s 00:00' % today.isoformat(), '--name-only', '--pretty=format:')
    files = [f.strip().replace('\\', '/') for f in touched.splitlines() if f.strip()]
    if not any(f.startswith(p) for f in files for p in HARNESS_PATHS):
        problems.append('no harness change committed today: the retro must improve the MCP or the '
                        'process (tools/mcp, tools/thor, tools/pc, DebugServer.java, '
                        'debug_api_android.cc, kernel_trap, or this hook) - add the tool or the '
                        'reflex the retro names, commit it')
    # Tool scripts compile and hold no control bytes (a heredoc once turned a
    # regex escape into byte 0x08 and a perf_probe verdict never fired).
    try:
        check = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'check_tools.py')],
                               cwd=ROOT, capture_output=True, text=True, timeout=60)
        if check.returncode:
            problems.append('tools/check_tools.py found broken tool scripts: ' +
                            ' | '.join(check.stdout.strip().splitlines()[:5]))
    except Exception:
        pass
    if problems:
        reason = 'Stop ritual (thor_retro.py): ' + '; '.join(problems) + '. Do these, then stop.'
        print(json.dumps({'decision': 'block', 'reason': reason}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
