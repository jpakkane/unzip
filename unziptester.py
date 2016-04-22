#!/usr/bin/env python3

import os, sys, subprocess, zipfile, tempfile

def check_zip(unzip_exe, zip_file):
    with tempfile.TemporaryDirectory() as tmpdir:
        with zipfile.ZipFile(zip_file, 'r') as zf:
            data = zf.read('notes')
            subprocess.check_call([unzip_exe, zip_file], cwd=tmpdir)
            data2 = open(os.path.join(tmpdir, 'notes'), 'rb').read()
            if data != data2:
                print('Uncompression failed.')
                sys.exit(1)
    print('All ok.')

if __name__ == '__main__':
    unzip_exe = sys.argv[1]
    zip_file = sys.argv[2]
    if not os.path.isabs(unzip_exe):
        unzip_exe = os.path.join(os.getcwd(), unzip_exe)
    if not os.path.isabs(zip_file):
        zip_file = os.path.join(os.getcwd(), zip_file)
    check_zip(unzip_exe, zip_file)