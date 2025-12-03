#!/usr/bin/env python3

import os
import pdb
import re
import subprocess
import sys

DEFAULT_FONTS = [
    'B612Mono',
    'BrassMono',
    'BrassMonoCode',
    'FiraCode',
    'Go',
    'IBMPlexMono',
    'Inconsolata',
    'InconsolataExpanded',
    'ShareTech',
    'Syne',
    'Triplicate',
]


def generate_font(header_file, path):
    symbol_name = os.path.splitext(os.path.basename(path))[0]
    symbol_name = symbol_name.replace('-', '_')
    symbol_name = symbol_name.split('_', 2)[1]

    output = subprocess.check_output(
        f'binary_to_compressed_c "{path}" DefaultFont{symbol_name}',
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
    print(f'Header: {header}')
    with open(header, 'wt') as header_file:
        header_file.write(f"""#pragma once
namespace kte::Fonts::{header.removesuffix('.h')} {{
""")
        for file in files:
            print(f'\t{file}')
            generate_font(header_file, file)

        header_file.write('}');

    subprocess.call("sed -i '' -e 's/_compressed_size/CompressedSize/' " +
                    f"{header_file.name}", shell=True)
    subprocess.call("sed -i '' -e 's/_compressed_data/CompressedData/' " +
                    f"{header_file.name}", shell=True)


def generate_dir(path):
    filelist = [os.path.join(path, file) for file in os.listdir(path)
                if file.endswith('ttf')]
    namespace = f'kte::Fonts::{path}'

    header = f"{path.replace('-', '_')}.h"
    generate_header(header, namespace, filelist)


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
