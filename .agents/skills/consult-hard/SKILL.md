---
name: consult-hard
description: Consult heavyweight external models (OpenAI Codex gpt-5.5 + Google Gemini 3.x Pro) for really hard rethinking — adversarial red-team of a plan, independent from-scratch alternatives, or a deep second opinion on a stuck/walled problem. Use when an analysis is contested, a lever hit a wall, a decision is high-stakes, or the user asks to "consult codex / gemini / chatgpt 5.5 for the hard stuff." Packages the full context, runs BOTH models at max reasoning in parallel, and reconciles their answers with our own.
---

# consult-hard — second-brain rethinking via Codex (gpt-5.5) + Gemini (3.x Pro)

Two frontier reasoning models, consulted in parallel on genuinely hard problems, then
reconciled. Use it to break out of a wall, stress-test a conclusion before committing
effort, or get fundamentally different framings. It is a *thinking* aid — it does not touch
the device or the repo (read-only consultation).

## When to use
- A perf lever or design hit a **wall** and you want an independent attack on the assumptions
  (e.g. "is the BD binning floor really irreducible?").
- A conclusion is **contested** in our own notes (e.g. Burnout bubble: GPU pipeline vs guest JIT).
- A decision is **high-stakes / hard-to-reverse** (a big refactor direction).
- The user explicitly says to consult codex / gemini / "chatgpt 5.5" / "gemini pro" for hard stuff.
- NOT for trivia, quick lookups, or anything cheap to just check directly — these calls cost
  minutes and tokens.

## One-time setup (already installed: codex-cli 0.140.0, gemini 0.46.0)
The user logs in themselves (interactive browser OAuth — do NOT try to automate it):
- **Codex:** `codex login` → "Sign in with ChatGPT" (uses their ChatGPT plan). Verify: `codex login status`.
  Alt: set `OPENAI_API_KEY`.
- **Gemini:** run `gemini` once → "Login with Google" (free tier) → exit the TUI. Verify: `gemini -p "ping"`.
  Alt: set `GEMINI_API_KEY` (from Google AI Studio).

Check auth before a real consult: `codex login status` and `gemini -p "ok"` (cheap probe).

## Models (verified on THIS machine 2026-06-16 — user preference baked in)
- **Codex:** `gpt-5.5` at `model_reasoning_effort=high` (user-requested "high thinking"; `xhigh`
  is the max if you want even deeper). `gpt-5.5-codex` is NOT a real id. Auth = ChatGPT login (done).
- **Gemini:** `gemini-3-flash-preview` (user prefers a 3.x flash over 3.1-pro). Auth here is the
  **Google OAuth login** (copied creds in `~/.gemini`), which exposes a NARROWER model set than the
  API-key path. **Confirmed working on this OAuth login:** `gemini-2.5-pro`, `gemini-2.5-flash`,
  `gemini-3-flash-preview`. **NOT available via OAuth:** `gemini-3.5-pro`, `gemini-3.5-flash`,
  `gemini-flash-latest` (these need a `GEMINI_API_KEY` from AI Studio + `selectedType: gemini-api-key`).
  Stronger-reasoning option on the same login: `gemini-2.5-pro`.
- To unlock the real `gemini-3.5-flash`/`-pro`: set `GEMINI_API_KEY`, flip `~/.gemini/settings.json`
  `security.auth.selectedType` to `gemini-api-key`. (settings.json must be UTF-8 **without BOM** — use
  the Write tool, NOT PowerShell `Out-File -Encoding utf8`, which adds a BOM and breaks the JSON parse.)

## How to run it

### 1. Write the prompt file
Assemble ONE self-contained prompt (the models have no repo/memory context). Put it in
`scratch/consult/problem.txt` (scratch/ is gitignored). It MUST include:
- The problem + the concrete ask.
- The relevant device-measured facts and the **proven walls** (so they don't re-derive dead ends).
  For Thor speed problems, prepend `docs/research/20260616-thor-speed-problem-briefing.md`.
- The default "hard rethink" instruction block (below).

Default instruction block to append to the problem:
```
You are a world-class systems/emulator/GPU engineer giving an INDEPENDENT second opinion.
Do three things, clearly separated:
1. RED-TEAM: attack the current plan/conclusion below — find the flawed assumption, the missed
   case, the reason it is wrong or won't deliver. Be specific and technical.
2. ALTERNATIVES: ignore our framing and propose 2-3 fundamentally different approaches from first
   principles, with the concrete mechanism and the expected payoff/risk of each.
3. VERDICT: the single highest-value thing to try next and why, plus what would falsify it.
Do not flatter. If our analysis is right, say so and sharpen it. Cite specifics over generalities.
```

### 2. Run BOTH in parallel (long-running — use a 600000 ms timeout or run_in_background)
Preferred (helper does the parallel fan-out + capture):
```
.\.agents\skills\consult-hard\consult.ps1 -PromptFile scratch\consult\problem.txt -OutDir scratch\consult
```
Outputs: `scratch/consult/codex.md` and `scratch/consult/gemini.md`.

Raw equivalents (if you'd rather run them as two separate background calls):
```powershell
Get-Content scratch\consult\problem.txt -Raw | codex exec -m gpt-5.5 -c model_reasoning_effort="high" --skip-git-repo-check - *>&1 | Out-File scratch\consult\codex.md -Encoding utf8
Get-Content scratch\consult\problem.txt -Raw | gemini -m gemini-3-flash-preview 2>$null | Out-File scratch\consult\gemini.md -Encoding utf8
```
(Gemini prints a harmless "256-color" warning to stderr — `2>$null` keeps the captured answer clean.)

### 3. Reconcile (this is the point — don't just relay)
Read both files. Then to the user, produce:
- **Where they agree** (highest-confidence signal).
- **Where they disagree / disagree with us** (the interesting part — adjudicate it with our
  device-measured facts; the external models do NOT have our measurements, so reject any claim
  that contradicts a proven wall and SAY why).
- **The one or two concrete next actions** you'll take, and what each would prove.
Treat their output as hypotheses to verify against the device/code, never as ground truth.

## Gotchas
- **Long prompts go in a FILE piped via stdin**, never as a quoted arg (PowerShell quoting + size).
- **Max reasoning is slow** (minutes). Always long-timeout or background; never a 120 s default.
- `*>&1` captures the streamed reasoning + the answer; for Codex you can use `-o file` to get only
  the final message. Write outputs under `scratch/` (gitignored) — they can be large.
- The models are stateless across calls — every consult re-sends full context.
- These are **advisors**. The project's rules still bind: device-measured facts win, no fabrication,
  validate IN-GAME, forward-only. An external suggestion is a lead to test, not a result.
- Free Gemini tier is rate-limited (~60/min, 1000/day); Codex uses the user's ChatGPT plan quota.
