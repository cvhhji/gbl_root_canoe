import argparse
import hashlib
import os
from pathlib import Path
import platform
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    ndk = Path(os.environ['NDK_PATH'])
    host = 'windows-x86_64' if platform.system() == 'Windows' else 'linux-x86_64'
    suffix = '.exe' if platform.system() == 'Windows' else ''
    compiler = ndk / 'toolchains/llvm/prebuilt' / host / 'bin' / ('clang' + suffix)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(compiler), '--target=aarch64-linux-android31', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror', '-mbranch-protection=standard', '-fPIE', '-pie', '-Wl,-z,relro,-z,now,-z,max-page-size=16384', str(Path(__file__).with_name('bootctl.c')), '-ldl', '-o', str(args.output)], check=True)
    data = args.output.read_bytes()
    if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', data, 18)[0] != 183:
        raise ValueError('Expected an AArch64 ELF executable')
    print('canoe_bootctl sha256=' + hashlib.sha256(data).hexdigest())


if __name__ == '__main__':
    main()
