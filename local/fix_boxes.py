"""
Fix box-diagram right-border alignment for Maple Mono CN font.
Rule: CJK char = 2x ASCII width.
Strategy: For simple single-box diagrams, use top border as canonical width.
           Skip complex tree/multi-box diagrams.
"""

import re
import os
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

RIGHT_BORDERS = set('│┤┐┘║╣╗╝')
LEFT_BORDERS  = set('│├┌└║╠╚╔')

COMPLEX_CHARS = set('├┤┬┴┼▼▲▶◀→←')  # chars that indicate tree/multi-box layouts


def is_cjk(c):
    cp = ord(c)
    if 0x2500 <= cp <= 0x257F: return False  # box-drawing = 1-width
    if 0x4E00 <= cp <= 0x9FFF: return True
    if 0x3400 <= cp <= 0x4DBF: return True
    if 0xF900 <= cp <= 0xFAFF: return True
    if 0x3000 <= cp <= 0x303F: return True
    if 0xFF00 <= cp <= 0xFFEF: return True
    if 0xFE30 <= cp <= 0xFE4F: return True
    if 0x3040 <= cp <= 0x309F: return True
    if 0x30A0 <= cp <= 0x30FF: return True
    return False


def visual_width(s):
    return sum(2 if is_cjk(c) else 1 for c in s)


def find_box_blocks(lines):
    """Find consecutive line ranges that form box-drawing diagrams."""
    blocks = []
    in_block = False
    start = 0
    for i, line in enumerate(lines):
        has_box = any(c in line for c in '┌┐└┘├┤┬┴┼│─')
        if has_box and not in_block:
            in_block = True
            start = i
        elif not has_box and in_block:
            if i - start >= 3:
                blocks.append((start, i - 1))
            in_block = False
    if in_block and len(lines) - start >= 3:
        blocks.append((start, len(lines) - 1))
    return blocks


def is_simple_box(lines_block):
    """Check if this is a simple single-box diagram (not a tree or multi-box)."""
    joined = '\n'.join(lines_block)
    # Count corners — simple box has exactly 1 of each corner
    nw = joined.count('┌')
    ne = joined.count('┐')
    sw = joined.count('└')
    se = joined.count('┘')
    if nw > 2 or ne > 2 or sw > 2 or se > 2:
        return False  # multiple boxes
    if nw == 0 or ne == 0:
        return False  # no complete box
    # No tree/multi-box chars
    for c in COMPLEX_CHARS:
        if c in joined:
            return False
    return True


def get_border_width(top_line):
    """Get canonical box width from top border: count ─ chars + 2 corners.
    Ignores any stray spaces (e.g. from previous buggy fixes)."""
    # Find the run of ─ chars between corners
    s = top_line.rstrip('\n')
    count = 0
    for c in s:
        if c == '─' or c == '═':
            count += 1
    return count + 2  # +2 for the corner chars


def find_right_border_pos(line):
    """Find position of rightmost border char in line."""
    for i in range(len(line) - 1, -1, -1):
        if line[i] in RIGHT_BORDERS:
            return i
    return -1


def find_left_border_pos(line):
    """Find position of leftmost border char in line."""
    for i, c in enumerate(line):
        if c in LEFT_BORDERS:
            return i
    return -1


def fix_simple_box(lines_block):
    """Fix a simple single-box diagram. Returns (fixed_lines, changed_count)."""
    # Step 0: strip trailing garbage after right borders
    cleaned = []
    for line in lines_block:
        rpos = find_right_border_pos(line)
        if rpos >= 0:
            cleaned.append(line[:rpos + 1])
        else:
            cleaned.append(line.rstrip('\n'))

    # Find canonical width from top border
    # Find the top border line (has ┌ and ┐)
    canon_width = None
    for line in cleaned:
        if '┌' in line and '┐' in line:
            canon_width = visual_width(line)
            break

    if canon_width is None:
        return lines_block, 0

    changed = 0
    fixed = []
    for line in cleaned:
        lpos = find_left_border_pos(line)
        rpos = find_right_border_pos(line)

        if lpos < 0 or rpos < 0 or lpos >= rpos:
            fixed.append(line)
            continue

        current_w = visual_width(line)
        if current_w == canon_width:
            fixed.append(line)
            continue

        # Interior = between left border and right border
        interior = line[lpos + 1:rpos]
        interior_w = visual_width(interior)
        target_interior_w = canon_width - 2  # minus the two border chars

        if interior_w < target_interior_w:
            # Need to pad interior
            needed = target_interior_w - interior_w
            new_line = line[:rpos] + ' ' * needed + line[rpos:]
        else:
            # Interior is too wide — trim trailing spaces from interior
            trimmed = interior.rstrip()
            trimmed_w = visual_width(trimmed)
            if trimmed_w <= target_interior_w:
                needed = target_interior_w - trimmed_w
                new_line = line[:lpos + 1] + trimmed + ' ' * needed + line[rpos:]
            else:
                # Interior content is genuinely too wide for the box
                # Don't modify — this would require changing the box size
                new_line = line

        if new_line != line:
            changed += 1
        fixed.append(new_line)

    return fixed, changed


def fix_file(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        text = f.read()

    lines = text.split('\n')
    blocks = find_box_blocks(lines)

    if not blocks:
        return 0, 0, 0

    total_changed = 0
    simple_count = 0
    for start, end in reversed(blocks):
        block_lines = lines[start:end + 1]
        if is_simple_box(block_lines):
            simple_count += 1
            fixed_lines, changed = fix_simple_box(block_lines)
            if changed:
                lines[start:end + 1] = fixed_lines
                total_changed += changed

    if total_changed:
        new_text = '\n'.join(lines)
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(new_text)

    return len(blocks), simple_count, total_changed


def main():
    vault = r'D:\Program Files\Obsidian\vault\thevault'
    if len(sys.argv) > 1:
        files = sys.argv[1:]
    else:
        files = []
        for root, dirs, fnames in os.walk(vault):
            dirs[:] = [d for d in dirs if d != '.trash']
            for fname in fnames:
                if fname.endswith('.md'):
                    files.append(os.path.join(root, fname))

    total_boxes = 0
    total_simple = 0
    total_changed = 0
    changed_files = []

    for filepath in files:
        boxes, simple, changed = fix_file(filepath)
        if boxes > 0:
            total_boxes += boxes
            total_simple += simple
        if changed > 0:
            total_changed += changed
            changed_files.append((filepath, boxes, changed))

    print(f"Scanned {len(files)} files")
    print(f"Found {total_boxes} box diagrams ({total_simple} simple, {total_boxes - total_simple} complex skipped)")
    print(f"Fixed {total_changed} lines in {len(changed_files)} files")
    for fp, boxes, changed in changed_files:
        rel = os.path.relpath(fp, vault)
        print(f"  {rel}: {boxes} boxes, {changed} lines fixed")


if __name__ == '__main__':
    main()
