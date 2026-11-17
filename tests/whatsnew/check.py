#!/usr/bin/env python3
"""Exercise extraction, its compiled C output and the production startup code."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('changelog', root / 'tools/build/changelog.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
extract = module.release_text
assert extract('## v1.2.3\n\n- One.\n- Two.\n\n## v1.2.30\n- Other.', '1.2.3') == 'One.\nTwo.'
assert extract('## v1.2.30\n- Other.', '1.2.3') == ''
assert extract('## v1.2.3\n\n## v1.2.2\n- Old.', '1.2.3') == ''
assert extract('## 1.2.3 (beta)\r\n- "Quoted" \\ path.\r\n', '1.2.3') == '"Quoted" \\ path.'
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    header = tmp / 'whatsnew_text.h'
    subprocess.run(['python3', str(root/'tools/build/changelog.py'), str(root/'CHANGELOG.md'),
                    str(root/'apps/breadbox/version.h'), str(header)], check=True)
    probe = tmp / 'probe.c'
    probe.write_text('#include <stdio.h>\n#include <string.h>\n#include "apps/breadbox/version.h"\n#include "whatsnew_text.h"\nint main(void){if(strcmp(BB_VERSION,BB_CHANGELOG_VERSION))return 1;puts(BB_CHANGELOG_VERSION);fputs(BB_CHANGELOG_TEXT,stdout);}\n')
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-I'+str(root), str(probe), '-o', str(tmp/'probe')], check=True)
    version, text = subprocess.check_output([str(tmp/'probe')], text=True).split('\n', 1)
    entry = (root/'CHANGELOG.md').read_text().split('## v'+version+' ', 1)[1].split('\n',1)[1].lstrip()
    expected = '\n'.join(line[2:] for line in entry.split('\n\n',1)[0].splitlines())
    assert text == expected
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I'+str(tmp), '-I'+str(root/'tests/app/host'), '-I'+str(root/'app/ui'),
                    '-I'+str(root/'include/breadbox'), '-I'+str(root/'platform/3ds'),
                    str(root/'tests/whatsnew/check.c'), str(root/'app/ui/whatsnew.c'),
                    str(root/'app/ui/whatsnew_model.c'), '-o', str(tmp/'check')], check=True)
    subprocess.run([str(tmp/'check')], cwd=tmp, check=True)
    fonts = json.loads((root/'tests/ui_fit/card_font.json').read_text())['fonts']
    (tmp/'card_advance.h').write_text('static const float advance[][128]={' +
        ','.join('{' + ','.join(f'[{ord(c)}]={value}' for c, value in advances.items()) + '}'
                 for advances in fonts.values()) + '};\n')
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-Wno-misleading-indentation', '-fsanitize=address,undefined', '-ffunction-sections',
                    '-fdata-sections', '-Wl,--gc-sections', '-I'+str(tmp), '-I'+str(root/'tests/app/host'),
                    '-I'+str(root/'app/ui'), '-I'+str(root/'include/breadbox'), '-I'+str(root/'platform/3ds'),
                    '-I'+str(root/'core/deps/libretro-common/include'), str(root/'tests/whatsnew/ui.c'),
                    str(root/'app/ui/card_scroll.c'), '-lm', '-o', str(tmp/'ui')], check=True)
    subprocess.run([str(tmp/'ui')], cwd=tmp, check=True)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-include', 'stddef.h',
                    '-I'+str(root/'tests/app/host'), '-I'+str(root/'include/breadbox'),
                    str(root/'tests/whatsnew/notice.c'), str(root/'app/ui/notice_bar.c'), '-lm',
                    '-o', str(tmp/'notice')], check=True)
    subprocess.run([str(tmp/'notice')], check=True)
assert extract('## v1.2.3\n\n- Short.\n\n### Details\n- Long prose.', '1.2.3') == 'Short.'
for body in ('\n'.join('- Short.' for _ in range(7)), '- '+ 'W'*50):
    try:
        extract('## v1.2.3\n'+body, '1.2.3')
    except ValueError as error:
        assert '1.2.3' in str(error)
    else:
        raise AssertionError('Invalid highlights were accepted')
print('Compiled changelog highlights and build limits: PASS')
