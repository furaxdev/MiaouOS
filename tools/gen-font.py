#!/usr/bin/env python3
"""Generate src/desk/font8x16.h from the Linux kernel VGA 8x16 font (lib/fonts/font_8x16.c, GPL-2.0).
Usage: gen-font.py font_8x16.c > src/desk/font8x16.h"""
import re, sys
text = open(sys.argv[1]).read()
body = text.split('{ 0, 0, FONTDATAMAX, 0 }, {', 1)[1].split('}\n};', 1)[0]
body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
vals = [int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', body)]
assert len(vals) == 4096, len(vals)
print('/* VGA 8x16 bitmap font (CP437 order), derived from the Linux kernel lib/fonts/font_8x16.c, GPL-2.0. */')
print('#ifndef MIAOU_FONT8X16_H\n#define MIAOU_FONT8X16_H\nstatic const unsigned char font8x16[256*16] = {')
for i in range(0, 4096, 16):
    print('  ' + ','.join('0x%02x' % v for v in vals[i:i+16]) + ',')
print('};\n#endif')
