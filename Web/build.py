#!/usr/bin/env python3
"""Assembles Web/dist/mandelbulb.html from the sources in Web/src (one self-contained page)."""
import pathlib
root = pathlib.Path(__file__).parent
src = root / 'src'
page = (src / 'page.html').read_text()
lib = (src / 'lib.js').read_text()
lib = lib.replace("if (typeof module !== 'undefined') module.exports = { makeLib };\n", '')
app = (src / 'app.js').read_text()
for name in ('shader.wgsl', 'present.wgsl'):
    text = (src / name).read_text()
    assert '`' not in text and '${' not in text, name
page = page.replace('/*__LIB__*/', lib)
page = page.replace('/*__SHADER__*/', (src / 'shader.wgsl').read_text())
page = page.replace('/*__PRESENT__*/', (src / 'present.wgsl').read_text())
page = page.replace('/*__APP__*/', app)
out = root / 'dist'
out.mkdir(exist_ok=True)
(out / 'mandelbulb.html').write_text(page)
print('wrote', out / 'mandelbulb.html', len(page), 'bytes')
