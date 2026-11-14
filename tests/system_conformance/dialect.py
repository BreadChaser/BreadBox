#!/usr/bin/env python3
"""Match the target C++ dialect without weakening undefined-behavior checks."""
from pathlib import Path
import subprocess
import sys

out = Path(sys.argv[1])
source = out / 'dialect.cpp'
source.write_text(r'''#include <climits>
int main(int argc, char **) {
 volatile int value = -10505, count = 16, high = INT_MAX, one = 1;
 if (argc == 2) return high + one;
 if (argc == 3) { count = 32; return one << count; }
 return (value << count) == -688455680 ? 0 : 1;
}
''')
exe = out / 'dialect'
for standard in ('gnu++17', 'gnu++20'):
 subprocess.run(['c++', '-std='+standard, '-fsanitize=undefined', '-fno-sanitize-recover=all',
                 str(source), '-o', str(exe)], check=True)
 result = subprocess.run([str(exe)], capture_output=True, text=True)
 if standard == 'gnu++17':
  assert result.returncode and 'left shift of negative value' in result.stderr, result.stderr
 else:
  assert result.returncode == 0, result.stderr
  for args, message in [(['overflow'], 'signed integer overflow'), (['shift', 'count'], 'shift exponent 32')]:
   result = subprocess.run([str(exe), *args], capture_output=True, text=True)
   assert result.returncode and message in result.stderr, result.stderr
print('NES C++20 dialect: defined signed shift passes; C++17 negative control, overflow and shift-count controls reject PASS')
