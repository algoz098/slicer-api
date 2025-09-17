#!/usr/bin/env python3
import sys
from itertools import islice

path = sys.argv[1]
start_tag = '; CONFIG_BLOCK_END\n'
with open(path, 'r', errors='ignore') as f:
    lines = f.readlines()
try:
    idx = next(i for i,l in enumerate(lines) if l.strip() == '; CONFIG_BLOCK_END')
except StopIteration:
    idx = -1
for i, l in enumerate(islice(lines, idx+1, idx+1+200)):
    print(f"{i+1:04d}: {l.rstrip()}\n", end='')

