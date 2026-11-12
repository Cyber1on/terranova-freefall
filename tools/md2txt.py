#!/usr/bin/env python3
# md2txt.py -- turns one of the docs (docs/BUGS.md) into plain text for the
# release zip, readable in Notepad: headings underlined, bold/code marks and links removed, lines
# wrapped at 78 columns, and each table row written as a block of "Column: value" lines.
#   tools/md2txt.py <in.md> <out.txt>
import re
import sys
import textwrap

WIDTH = 78


def inline(s):
    s = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'\1 (\2)', s)     # [text](url) -> text (url)
    s = re.sub(r'\*\*(.+?)\*\*', r'\1', s)
    s = re.sub(r'(?<!\w)\*(.+?)\*(?!\w)', r'\1', s)
    s = s.replace('`', '')
    return s.strip()


def wrap(s, first='', rest=''):
    return textwrap.fill(s, WIDTH, initial_indent=first, subsequent_indent=rest,
                         break_long_words=False, break_on_hyphens=False)


def cells(line):
    return [inline(c) for c in line.strip().strip('|').split('|')]


def convert(lines):
    out = []
    i = 0
    while i < len(lines):
        line = lines[i].rstrip()
        m = re.match(r'(#+)\s+(.*)', line)
        if m:
            title = inline(m.group(2))
            if out and out[-1] != '':
                out.append('')
            out.append(title.upper() if len(m.group(1)) == 1 else title)
            out.append(('=' if len(m.group(1)) <= 2 else '-') * len(title))
            out.append('')
            i += 1
        elif line.startswith('|'):
            head = cells(line)
            i += 2                                  # the header and the |---| line
            while i < len(lines) and lines[i].startswith('|'):
                row = cells(lines[i])
                out.append(wrap('%s  %s' % (row[0], row[1]) if len(row) > 1 else row[0]))
                for name, value in zip(head[2:], row[2:]):
                    out.append(wrap('%s: %s' % (name, value), '    ', '        '))
                out.append('')
                i += 1
        elif re.match(r'\s*[-*] ', line):
            depth = (len(line) - len(line.lstrip())) // 2
            out.append(wrap(inline(re.sub(r'^\s*[-*] ', '', line)),
                            '  ' * depth + '- ', '  ' * depth + '  '))
            i += 1
        elif line == '':
            if out and out[-1] != '':
                out.append('')
            i += 1
        else:
            para = [line]                           # a paragraph: lines up to a blank one
            i += 1
            while i < len(lines) and lines[i].strip() and not re.match(r'(#|\||\s*[-*] )', lines[i]):
                para.append(lines[i].rstrip())
                i += 1
            out.append(wrap(inline(' '.join(para))))
    while out and out[-1] == '':
        out.pop()
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    with open(sys.argv[1], encoding='utf-8') as f:
        text = convert(f.read().splitlines())
    with open(sys.argv[2], 'w', encoding='utf-8', newline='\r\n') as f:
        f.write(text)
