#!/usr/bin/env python3
"""Compare every SVT output sample to the unmodified ISO decoder driver."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
p=argparse.ArgumentParser()
p.add_argument('decoder',type=Path)
p.add_argument('streams',type=Path)
p.add_argument('output',type=Path)
p.add_argument('--expected',type=int,default=476)
a=p.parse_args()
a.output.mkdir(exist_ok=False)
passed=[]; failures=[]
paths=sorted(a.streams.glob('*-auto.jxs'))
assert len(paths)==a.expected,len(paths)
for path in paths:
    target=a.output/(path.stem+'.bin')
    result=subprocess.run([str(a.decoder.resolve()),str(path.resolve()),str(target.resolve())],capture_output=True)
    (a.output/(path.stem+'.log')).write_bytes(result.stdout+result.stderr)
    expected=path.with_name(path.name.replace('-auto.jxs','-pixels.bin')).read_bytes()
    if result.returncode:
        failures.append({'case':path.stem,'returncode':result.returncode})
    elif target.read_bytes()!=expected:
        actual=target.read_bytes()
        first=next((i for i,(x,y) in enumerate(zip(expected,actual)) if x!=y),min(len(expected),len(actual)))
        failures.append({'case':path.stem,'first_difference':first,'expected_bytes':len(expected),'actual_bytes':len(actual)})
    else:
        passed.append(path.stem)
receipt={'completed':len(paths),'passed':len(passed),'failures':failures,
         'driver_sha256':hashlib.sha256(a.decoder.read_bytes()).hexdigest(),
         'comparison':'every active decoded sample, native planes, LE for depth >8'}
(a.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2),flush=True)
if failures:raise SystemExit(1)
