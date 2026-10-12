import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    repo = root.parents[1]
    args.output.mkdir(parents=True, exist_ok=True)
    flags = ["--target=aarch64-none-elf", "-Oz", "-ffreestanding", "-fpie",
             "-fno-stack-protector", "-fno-builtin", "-fno-jump-tables",
             "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
             "-mstrict-align", "-mgeneral-regs-only", "-Wall", "-Wextra", "-Werror",
             "-I" + str(repo / "submodules/uefi/edk2/MdePkg/Include"),
             "-I" + str(repo / "submodules/uefi/edk2/MdePkg/Include/AArch64")]
    for name in ("entry.S", "loader.c"):
        subprocess.run(["clang", *flags, "-c", str(root / name), "-o",
                        str(args.output / (name + ".o"))], check=True)
    elf = args.output / "payload.elf"
    subprocess.run(["ld.lld", "-T", str(root / "payload.ld"),
                    str(args.output / "entry.S.o"), str(args.output / "loader.c.o"),
                    "-o", str(elf)], check=True)
    relocations = subprocess.check_output(["llvm-readelf", "-r", str(elf)], text=True)
    if "There are no relocations" not in relocations:
        raise RuntimeError(relocations)
    symbols = {}
    for line in subprocess.check_output(["llvm-nm", "--defined-only", str(elf)], text=True).splitlines():
        value, kind, name = line.split()
        if name in {"efisp_entry", "efisp_resume", "efisp_config", "efisp_load"}:
            symbols[name] = int(value, 16)
    if len(symbols) != 4 or symbols["efisp_entry"] != 0:
        raise RuntimeError("Unexpected payload symbols")
    binary = args.output / "payload.bin"
    subprocess.run(["llvm-objcopy", "-O", "binary", "--only-section=.text", str(elf), str(binary)], check=True)
    symbols["sha256"] = hashlib.sha256(binary.read_bytes()).hexdigest()
    (args.output / "payload.json").write_text(json.dumps(symbols, indent=2) + "\n", encoding="utf-8")
    shutil.copyfile(root / "restore_efisp.py", args.output / "restore_efisp.py")
    print(f"Built {len(binary.read_bytes())} bytes of position-independent EFISP loading code")


if __name__ == "__main__":
    main()
