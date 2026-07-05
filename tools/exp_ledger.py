#!/usr/bin/env python3
"""xenia-thor experiment ledger (SQLite RAG) - anti-repetition DB.

Every perf/correctness experiment + its verdict, queryable. USE BEFORE running any
device experiment (check) and AFTER every result (add). Replaces grep-the-markdown.

  python tools/exp_ledger.py check "<keyword>"      # BEFORE running: has this been tried?
  python tools/exp_ledger.py add <lever> <category> <verdict> <hypothesis> <result> [notes]
  python tools/exp_ledger.py dead [category]         # all DEAD/FLAT/GFX-LOSS levers
  python tools/exp_ledger.py wins                     # all WIN levers
  python tools/exp_ledger.py list [category]          # everything (or by category)

verdicts: DEAD FLAT WIN GFX-LOSS CONFOUNDED OPEN
categories: cpu gpu edram interlock shader rearch draw vertex fill measurement
"""
import sqlite3, sys, os, datetime

DB = os.path.join(os.path.dirname(__file__), "..", "docs", "research", "experiments.db")

def conn():
    c = sqlite3.connect(DB)
    c.execute("""CREATE TABLE IF NOT EXISTS experiments(
        id INTEGER PRIMARY KEY, date TEXT, lever TEXT, category TEXT,
        verdict TEXT, hypothesis TEXT, result TEXT, notes TEXT)""")
    return c

def add(lever, category, verdict, hypothesis, result, notes=""):
    c = conn()
    # de-dupe by lever: update if exists, else insert
    row = c.execute("SELECT id FROM experiments WHERE lever=?", (lever,)).fetchone()
    d = datetime.date.today().isoformat() if not _NODATE else "backfill"
    if row:
        c.execute("UPDATE experiments SET date=?,category=?,verdict=?,hypothesis=?,result=?,notes=? WHERE id=?",
                  (d, category, verdict, hypothesis, result, notes, row[0]))
    else:
        c.execute("INSERT INTO experiments(date,lever,category,verdict,hypothesis,result,notes) VALUES(?,?,?,?,?,?,?)",
                  (d, lever, category, verdict, hypothesis, result, notes))
    c.commit(); print(f"[{verdict}] {lever} recorded")

def _show(rows):
    if not rows: print("  (none)"); return
    for r in rows:
        print(f"  [{r[4]:9}] {r[2]:32} ({r[3]}) {r[1]}\n      H: {r[5]}\n      R: {r[6]}" + (f"\n      N: {r[7]}" if r[7] else ""))

def check(kw):
    c = conn()
    rows = c.execute("SELECT * FROM experiments WHERE lever LIKE ? OR hypothesis LIKE ? OR result LIKE ? OR notes LIKE ? ORDER BY verdict",
                     tuple(f"%{kw}%" for _ in range(4))).fetchall()
    print(f"=== '{kw}': {len(rows)} match(es) - DO NOT re-run DEAD/FLAT ==="); _show(rows)

def by(col, val=None):
    c = conn()
    if col == "dead":
        q = "SELECT * FROM experiments WHERE verdict IN ('DEAD','FLAT','GFX-LOSS')"
        if val: q += f" AND category='{val}'"
    elif col == "wins":
        q = "SELECT * FROM experiments WHERE verdict='WIN'"
    else:
        q = "SELECT * FROM experiments" + (f" WHERE category='{val}'" if val else "")
    _show(c.execute(q + " ORDER BY category,verdict").fetchall())

_NODATE = "--backfill" in sys.argv
if _NODATE: sys.argv.remove("--backfill")

if __name__ == "__main__":
    a = sys.argv[1:]
    if not a: print(__doc__); sys.exit()
    cmd = a[0]
    if cmd == "add": add(*a[1:])
    elif cmd == "check": check(a[1])
    elif cmd == "dead": by("dead", a[1] if len(a) > 1 else None)
    elif cmd == "wins": by("wins")
    elif cmd == "list": by("list", a[1] if len(a) > 1 else None)
    else: print(__doc__)
