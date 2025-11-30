#!/usr/bin/env python3

import os
import re
import sys
import subprocess

DEFAULT_FONTS = ['B612_Mono', 'BrassMono']

def generate_font(header_file, path):
    symbol_name=os.path.splitext(os.path.basename(path))[0]
    symbol_name=symbol_name.replace('-', '_')
    output = subprocess.check_output(
            f'binary_to_compressed_c "{path}" {symbol_name}',
            shell=True)
    header_file.write('\n\n')
    header_file.write(output.decode('utf-8'))

def generate_header(header, guard, files):
    try:
        os.remove(header)
    except FileNotFoundError:
        pass
    except:
        raise
    with open(header, 'wt') as header_file:
        header_file.write(f"""#ifndef {guard}
#define {guard}

""")
        for file in files:
            generate_font(header_file, file)

        header_file.write('\n\n#endif\n')

def generate_dir(path):
    filelist = [os.path.join(path, file) for file in os.listdir(path)
                if file.endswith('ttf')]
    guard = f'KGE_FONTS_{path.upper()}_H'

    header = f"{path.lower().replace('-', '_')}.h"
    generate_header(header, guard, filelist)

def main(fonts=None):
    if fonts is None:
        fonts = DEFAULT_FONTS

    for font in fonts:
        generate_dir(font)

if __name__ == '__main__':
    fonts = None
    if len(sys.argv) > 1:
        fonts = sys.argv[1:]
    main(fonts)
