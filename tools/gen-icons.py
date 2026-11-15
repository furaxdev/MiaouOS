#!/usr/bin/env python3
"""Fetch Lucide icons (ISC licence, https://github.com/lucide-icons/lucide) and render them as white PNG masks
(assets/icons/mono/NAME.png). MiaouDesk tints them with the theme text colour at draw time, so every icon
shares one size, one stroke (1.5) and the colour of its text role.   Usage: gen-icons.py OUTDIR (needs cairosvg, network)"""
import os, sys, urllib.request
import cairosvg
BASE = 'https://raw.githubusercontent.com/lucide-icons/lucide/main/icons/'
OUT = sys.argv[1] + '/mono'
os.makedirs(OUT, exist_ok=True)
ICONS = {   # MiaouDesk name -> Lucide name
 'terminal': 'terminal', 'files': 'folders', 'web': 'globe', 'store': 'shopping-bag', 'monitor': 'activity', 'network': 'network',
 'settings': 'settings', 'folder': 'folder', 'home': 'house', 'file': 'file-text', 'image': 'image', 'package': 'package', 'drive': 'hard-drive',
 'info': 'info', 'folder-system': 'folder-cog', 'keyboard': 'keyboard', 'display': 'monitor', 'palette': 'palette', 'moon': 'moon', 'sun': 'sun',
 'back': 'arrow-left', 'forward': 'arrow-right', 'reload': 'rotate-cw', 'home-m': 'house', 'stop': 'x', 'folder-new': 'folder-plus', 'note': 'file-plus',
 'rename': 'pencil', 'trash': 'trash', 'search': 'search', 'power': 'power', 'reboot': 'rotate-ccw', 'console': 'square-terminal', 'up': 'arrow-up',
 'plug': 'wifi-off', 'signal': 'wifi', 'keyboard-m': 'keyboard', 'ok': 'check', 'install': 'download', 'run': 'play', 'kill': 'x', 'list': 'list',
 'down': 'arrow-down', 'info-m': 'info',
}
missing = []
for name, lucide in ICONS.items():
    try: svg = urllib.request.urlopen(BASE + lucide + '.svg', timeout=30).read().decode()
    except Exception: missing.append(name); continue
    svg = svg.replace('currentColor', '#ffffff').replace('stroke-width="2"', 'stroke-width="1.5"')
    open(f'{OUT}/{name}.png', 'wb').write(cairosvg.svg2png(bytestring=svg.encode(), output_width=96, output_height=96))
print('missing:', missing)
