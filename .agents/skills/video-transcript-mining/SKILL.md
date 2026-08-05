---
name: video-transcript-mining
description: Pull a technical talk/video down to a searchable local transcript (yt-dlp auto-captions, whisper only as fallback) and mine it for concrete, portable optimizations. Use when the user links a video about emulator/ARM/GPU performance and asks what we can take from it - do NOT guess the contents from the title or from related repo commits.
---

# Mining a video for portable optimizations

Claude cannot watch video. Guessing the content from the title, or inferring it
from the author's recent commits, produces plausible-but-wrong answers. Get the
actual transcript - it is cheap, and it names things the commits do not (which
device, which core, how much faster, what did NOT work).

## 1. Get the transcript (auto-captions first)

Auto-captions are almost always present and need no audio download and no
whisper. This is seconds, not minutes:

```bash
python -m pip install --quiet yt-dlp        # if missing; ffmpeg is already installed
cd "$SCRATCH/vid"
python -m yt_dlp --skip-download --write-auto-subs --write-subs \
  --sub-langs "en.*" --sub-format vtt -o "vid.%(ext)s" "<URL>"
```

Title/author without downloading anything (WebFetch on a YouTube watch URL
returns only the footer - it does NOT work):

```bash
curl -s "https://www.youtube.com/oembed?url=<URL>&format=json"
```

**Fallback, only when no captions exist:** download audio
(`python -m yt_dlp -x --audio-format wav`) and transcribe with
`faster-whisper` (`pip install faster-whisper`, `small.en` is enough for
technical speech). Whisper is minutes of CPU; do not reach for it first.

## 2. Flatten the VTT

Auto-caption VTT repeats every line 2-3x with inline timing tags. Dedupe or the
transcript is unreadable and 3x too long:

```python
import re
lines, seen = [], set()
for raw in open('vid.en.vtt', encoding='utf-8', errors='replace'):
    if '-->' in raw or raw.startswith(('WEBVTT','Kind:','Language:')) or not raw.strip():
        continue
    t = re.sub(r'<[^>]+>', '', raw).strip()
    if t and t not in seen:
        seen.add(t); lines.append(t)
open('transcript.txt','w',encoding='utf-8').write(re.sub(r'\s+',' ',' '.join(lines)))
```

## 3. Mine it, don't read it

A 1-hour talk is ~12k words. Do not read it linearly. Frequency-scan for the
vocabulary of the domain, then pull context windows around the hits:

```python
terms = ['%','faster','speedup','instruction','codegen','llvm','page','atomic',
         'spin','yield','cache','shuffle','feature','port','latency']
# count occurrences, then print ~200 chars before / ~400 after the top hits
```

The frequency table alone tells you what the video is actually about, which is
often not what the title says.

## 4. What to extract (and what to write down)

Capture into `docs/research/<date>-<topic>.md`:
- **The hardware it was measured on.** Decisive for whether a number transfers.
  (A 2026-08 RPCS3 talk turned out to be measured on an AYN Odin 2 - the same
  Snapdragon 8 Gen 2 as our Thor - so its numbers transfer directly rather than
  by analogy. That single fact changed the whole assessment.)
- **Each optimization, mapped to OUR file/function**, with a verdict:
  applicable / already done / not applicable (and why).
- **Things the author found did NOT work** - as valuable as the wins, and they
  belong in `tools/exp_ledger.py` so we never re-run them.
- **Claimed numbers, attributed and marked unverified.** Never restate someone
  else's benchmark as ours. Measure on the Thor before claiming it here.

## 5. Then verify against the source

Talks compress and simplify. Before porting, find the actual commit/PR and read
the diff - the video says what and why, the diff says exactly how, and it is
the thing that has to survive our tree. For a GitHub project:

```bash
curl -s "https://api.github.com/search/issues?q=repo:<owner>/<repo>+author:<user>+is:pr+is:merged&sort=updated&order=desc"
curl -s "https://api.github.com/repos/<owner>/<repo>/pulls/<n>/files"
```

## Hygiene

Keep transcripts and media in the scratchpad, NOT in the repo - they are third
party content and large. Commit only our own analysis and the mapped findings.
