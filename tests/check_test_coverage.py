# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""A new test source or unregistered executable must fail configuration."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as temporary:
    source = Path(temporary) / 'source'
    shutil.copytree(sys.argv[1], source, ignore=shutil.ignore_patterns('.git', 'build*', 'run_inner.sh', '__pycache__'))
    for case in ('source', 'executable'):
        orphan = source / 'tests' / 'test_deliberately_omitted.cpp'
        orphan.write_text('int main() { return 0; }\n')
        if case == 'executable':
            with (source / 'tests' / 'CMakeLists.txt').open('a') as stream:
                stream.write('\nadd_executable(test_deliberately_omitted test_deliberately_omitted.cpp)\n')
        result = subprocess.run(['cmake', '-S', str(source), '-B', str(Path(temporary) / case), '-G', 'Ninja', '-DBUILD_TESTING=ON'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        expected = 'Test source omitted' if case == 'source' else 'Test executable omitted from CTest'
        if result.returncode == 0 or expected not in result.stdout:
            print(result.stdout)
            raise SystemExit('Coverage guard did not detect ' + case)
print('Unregistered test source and CTest executable both detected')
