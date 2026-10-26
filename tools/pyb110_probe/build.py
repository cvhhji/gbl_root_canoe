import argparse
import base64
import ctypes
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess


SOURCE = Path(__file__).resolve().parent
REPO = SOURCE.parents[1]


def run(arguments):
    return subprocess.check_output([str(value) for value in arguments], stderr=subprocess.STDOUT, text=True)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def host_tests(output):
    windows = platform.system() == 'Windows'
    compiler = shutil.which('clang' if windows else 'cc')
    if not compiler:
        raise RuntimeError('Host C compiler not found')
    library = output / ('ProbeMock.dll' if windows else 'ProbeMock.so')
    flags = ['-O2', '-std=c11', '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-nostdlib', '-shared']
    if windows:
        flags += ['--target=x86_64-pc-windows-msvc', '-fuse-ld=lld', '-Wl,/noentry,/nodefaultlib,/Brepro']
    else:
        flags += ['-fPIC', '-fvisibility=hidden', '-Wl,-Bsymbolic']
    run([compiler, *flags, SOURCE / 'service_probe_mock.c', '-o', library])
    mock = ctypes.CDLL(str(library))
    mock.RunEnvProbeTest.argtypes = [ctypes.c_uint32]
    mock.RunEnvProbeTest.restype = ctypes.c_uint32
    names = ['service_found', 'library_missing', 'environment_denied', 'controller_denied', 'application_missing', 'non_root', 'wrong_model', 'wrong_build', 'symbol_missing', 'null_environment', 'invalid_arguments']
    for index, name in enumerate(names):
        faults = mock.RunEnvProbeTest(index)
        if faults:
            raise RuntimeError(name + ' failed: ' + hex(faults))
    return names


def main():
    parser = argparse.ArgumentParser(description='Build the read-only PYB110 .155 service query test')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ndk', type=Path, default=os.environ.get('ANDROID_NDK_LATEST_HOME') or os.environ.get('ANDROID_NDK_HOME'))
    parser.add_argument('--host-only', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    result = {'host_mock_cases_passed': host_tests(output), 'source_sha256': digest(SOURCE / 'service_probe.c')}
    if args.host_only:
        (output / 'host-verification.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(result, indent=2))
        return
    if not args.ndk or not args.ndk.is_dir():
        raise RuntimeError('Pass an Android NDK with --ndk')
    host = 'windows-x86_64' if platform.system() == 'Windows' else 'linux-x86_64'
    suffix = '.exe' if platform.system() == 'Windows' else ''
    toolchain = args.ndk / 'toolchains/llvm/prebuilt' / host / 'bin'
    compiler = toolchain / ('clang' + suffix)
    binary = output / 'service-probe'
    run([compiler, '--target=aarch64-linux-android34', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-mbranch-protection=standard', '-fPIE', '-pie', '-Wl,-z,relro,-z,now,-z,max-page-size=16384', '-Wl,--build-id=sha1', SOURCE / 'service_probe.c', '-ldl', '-o', binary])
    data = binary.read_bytes()
    if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', data, 18)[0] != 183:
        raise RuntimeError('Probe is not an AArch64 ELF64 executable')
    imports = run([toolchain / ('llvm-nm' + suffix), '--dynamic', '--undefined-only', binary])
    names = {line.split()[-1].split('@')[0] for line in imports.splitlines() if line.split()}
    forbidden = {'ioctl', 'open', 'write', 'pwrite', 'reboot', 'system', 'execve', 'QSEECom_start_app', 'QSEECom_shutdown_app', 'QSEECom_send_cmd'}
    if names & forbidden:
        raise RuntimeError('Unexpected probe imports: ' + ', '.join(sorted(names & forbidden)))
    revision = run(['git', '-C', REPO, 'rev-parse', 'HEAD']).strip()
    script = (SOURCE / 'test.sh.in').read_text(encoding='utf-8')
    script = script.replace('@PROBE_BASE64@', base64.encodebytes(data).decode('ascii').rstrip()).replace('@PROBE_SHA256@', digest(binary)).replace('@SOURCE_COMMIT@', revision)
    if not script.startswith('#!/system/bin/sh\n') or any(line.lstrip().startswith('#') for line in script.splitlines()[1:]):
        raise RuntimeError('Unexpected script header/comment')
    packet = output / 'packet'
    packet.mkdir(exist_ok=True)
    path = packet / 'test.sh'
    path.write_text(script, encoding='utf-8', newline='\n')
    if {item.name for item in packet.iterdir()} != {'test.sh'}:
        raise RuntimeError('The phone package must contain only test.sh')
    if platform.system() != 'Windows':
        run(['bash', '-n', path])
    result.update({'source_commit': revision, 'android_compiler': run([compiler, '--version']).splitlines()[0], 'ndk_properties': (args.ndk / 'source.properties').read_text(encoding='utf-8'), 'binary_sha256': digest(binary), 'binary_bytes': len(data), 'script_sha256': digest(path), 'script_bytes': path.stat().st_size, 'packet_entries': ['test.sh'], 'contains_markdown': False, 'variable_commands_sent': 0, 'device_execution_tested': False, 'efi_execution_tested': False})
    (output / 'verification.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
