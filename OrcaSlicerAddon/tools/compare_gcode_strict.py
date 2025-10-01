#!/usr/bin/env python3
import argparse
import sys
from itertools import zip_longest


def read_lines(path, after_config=False):
    with open(path, 'r', errors='ignore') as f:
        lines = f.readlines()
    if not after_config:
        return lines, 0
    # find '; CONFIG_BLOCK_END' and start after it
    start = 0
    for i, line in enumerate(lines):
        if line.strip() == '; CONFIG_BLOCK_END':
            start = i + 1
            break
    return lines[start:], start


def compare_lines(ref_lines, out_lines, ref_start_idx=0, out_start_idx=0, out_stream=sys.stdout):
    diff_count = 0
    total = 0
    for i, (ra, rb) in enumerate(zip_longest(ref_lines, out_lines, fillvalue=None), start=1):
        total += 1
        a = ra if ra is not None else ''
        b = rb if rb is not None else ''
        if a != b:
            diff_count += 1
            # Print line numbers relative to original files
            la = (ref_start_idx + i) if ra is not None else '-'
            lb = (out_start_idx + i) if rb is not None else '-'
            # Make lines single-line (strip only trailing newlines)
            as_ = a.rstrip('\n')
            bs_ = b.rstrip('\n')
            print(f"REF[{la}]: {as_}", file=out_stream)
            print(f"OUT[{lb}]: {bs_}", file=out_stream)
            print(file=out_stream)
    return total, diff_count


def main():
    ap = argparse.ArgumentParser(description='Strict G-code line-by-line comparator (prints every differing line).')
    ap.add_argument('reference', help='Path to reference G-code file')
    ap.add_argument('generated', help='Path to generated G-code file')
    ap.add_argument('--after-config', action='store_true', help='Compare only lines after ; CONFIG_BLOCK_END')
    ap.add_argument('--out', default='-', help='Output path for differences; default stdout')
    args = ap.parse_args()

    ref_lines, ref_start = read_lines(args.reference, after_config=args.after_config)
    out_lines, out_start = read_lines(args.generated, after_config=args.after_config)

    if args.out == '-':
        out_stream = sys.stdout
        close_out = False
    else:
        out_stream = open(args.out, 'w', encoding='utf-8')
        close_out = True

    try:
        total, diff_count = compare_lines(ref_lines, out_lines, ref_start, out_start, out_stream)
    finally:
        if close_out:
            out_stream.close()

    print(f"Compared {total} lines; differences: {diff_count}", file=sys.stderr)
    return 0 if diff_count == 0 else 1


if __name__ == '__main__':
    sys.exit(main())

