"""Extract only frame diagnostics from a runtime log, omitting console identity.

Usage: python summarize-frame-log.py input.log output.log
Each row retains its offset from the first diagnostic row. Console clock dates
are not treated as the test date. No original game content is copied.
"""
import argparse
from datetime import datetime
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path)
parser.add_argument('destination', type=Path)
args = parser.parse_args()
start = None
rows = []
for line in args.source.read_text(errors='replace').splitlines():
    match = re.search(r'WOS (?:perf:|phases ms/frame:|guest perf:|frame times:|scheduling:|coarse perf:|coarse nested ms/frame:|texture descriptors/frame:|command waits ms/frame:|extra nested ms/frame:|guest waits ms/frame:|guest wait calls/frame:|wait detail:|wait detail coverage:|signal detail:|signal detail coverage:|draw sample coverage:|sampled draw us/sample:).*', line)
    if not match:
        continue
    stamp = re.match(r'\[([^\]]+)\]', line)
    offset = ''
    if stamp:
        try:
            now = datetime.fromisoformat(stamp[1])
            if start is None:
                start = now
            offset = f'+{(now - start).total_seconds():.3f}s '
        except ValueError:
            pass
    rows.append(offset + match[0])
if not rows:
    raise SystemExit('No frame diagnostics found')
args.destination.parent.mkdir(parents=True, exist_ok=True)
args.destination.write_text(
    'Web of Shadows PS5 frame diagnostics\n'
    'Host frame intervals, CPU times and backend wall times, including waits.\n'
    'Not GPU hardware timestamps. Nested timings overlap their calling phases.\n'
    'Guest swaps are frame updates, not unique displayed images.\n\n'
    + '\n'.join(rows) + '\n')
print(f'Saved {len(rows)} diagnostic rows to {args.destination}')
