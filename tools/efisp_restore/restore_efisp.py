import argparse
import hashlib
import json
import lzma
from pathlib import Path
import struct


PROFILES = {
    "8ed9bcd440a89b51ac76beb9fe24eaabe8689fe9924de487d627c87abf47d2ea": {
        "name": "new_abl", "efi_sha256": "663e89b7c3f5e4335a610090013c4a729ab89317bd0f9452cf5ac0bb5a3d7401",
        "hook": 0x556c, "boot_services": 0xbaa08, "image_handle": 0xba9e8,
        "blocked": "Boot failure reported after flashing abl_a. Generation is disabled pending startup analysis.",
    },
    "cd4fe25e80e7af320ad49d66e83056776fc012c8de1e3fbb2489fe9766d337d7": {
        "name": "PYB110_17.0.0.152", "efi_sha256": "8e4060ce5b3df61991ec0361ffa3abc0379e3fd3f037dff0b3a5221116ae9812",
        "hook": 0x53d4, "boot_services": 0xc1460, "image_handle": 0xc1440,
    },
}


def read24(data, offset):
    return int.from_bytes(data[offset:offset + 3], "little")


def put24(data, offset, value):
    data[offset:offset + 3] = value.to_bytes(3, "little")


def ffs_checksum(data, offset):
    data[offset + 16] = 0
    header = bytearray(data[offset:offset + 24])
    header[17] = header[23] = 0
    data[offset + 16] = -sum(header) & 255


def fv_checksum(data, offset):
    length = struct.unpack_from("<H", data, offset + 48)[0]
    struct.pack_into("<H", data, offset + 50, 0)
    words = struct.unpack_from("<" + "H" * (length // 2), data, offset)
    struct.pack_into("<H", data, offset + 50, -sum(words) & 65535)


def branch(source, target):
    delta = target - source
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError("Branch target is out of range")
    return 0x14000000 | ((delta // 4) & 0x3ffffff)


def move_adrp(word, source, target):
    if word & 0x9f000000 != 0x90000000:
        raise ValueError("The insertion point is not the expected ADRP instruction")
    immediate = ((word >> 5) & 0x7ffff) << 2 | ((word >> 29) & 3)
    if immediate & (1 << 20):
        immediate -= 1 << 21
    page = (source & ~4095) + (immediate << 12)
    delta = (page - (target & ~4095)) // 4096
    if not -(1 << 20) <= delta < (1 << 20):
        raise ValueError("Relocated ADRP is out of range")
    delta &= 0x1fffff
    return 0x90000000 | ((delta & 3) << 29) | ((delta >> 2) << 5) | (word & 31)


def add_loader(efi, profile, payload, symbols):
    pe = struct.unpack_from("<I", efi, 60)[0]
    opt = pe + 24
    count = struct.unpack_from("<H", efi, pe + 6)[0]
    optional = struct.unpack_from("<H", efi, pe + 20)[0]
    table = opt + optional
    headers = struct.unpack_from("<I", efi, opt + 60)[0]
    section_align, file_align = struct.unpack_from("<II", efi, opt + 32)
    image_size = struct.unpack_from("<I", efi, opt + 56)[0]
    if count != 3 or section_align != 4096 or file_align != 4096 or len(efi) != image_size:
        raise ValueError("Unsupported PE section layout")
    if table + (count + 1) * 40 > headers or any(efi[table + count * 40:table + (count + 1) * 40]):
        raise ValueError("There is no unused section header")
    for i in range(count):
        virtual, raw = struct.unpack_from("<I", efi, table + i * 40 + 12)[0], struct.unpack_from("<I", efi, table + i * 40 + 20)[0]
        if virtual != raw:
            raise ValueError("Unsupported PE address mapping")
    data = bytearray(efi)
    code = bytearray(payload)
    resume = image_size + symbols["efisp_resume"]
    config = image_size + symbols["efisp_config"]
    hook = profile["hook"]
    instruction = struct.unpack_from("<I", data, hook)[0]
    struct.pack_into("<II", code, symbols["efisp_resume"],
                     move_adrp(instruction, hook, resume), branch(resume + 4, hook + 4))
    struct.pack_into("<ii", code, symbols["efisp_config"],
                     profile["boot_services"] - config, profile["image_handle"] - config)
    size = (len(code) + 4095) & ~4095
    data.extend(code + bytes(size - len(code)))
    struct.pack_into("<I", data, hook, branch(hook, image_size))
    struct.pack_into("<H", data, pe + 6, count + 1)
    struct.pack_into("<I", data, opt + 4, struct.unpack_from("<I", data, opt + 4)[0] + size)
    struct.pack_into("<I", data, opt + 56, image_size + size)
    struct.pack_into("<I", data, opt + 64, 0)
    struct.pack_into("<8sIIIIIIHHI", data, table + count * 40,
                     b".efisp\0\0", len(code), image_size, size, image_size, 0, 0, 0, 0, 0x60000020)
    return bytes(data), {"hook": hook, "continuation": hook + 4, "payload_rva": image_size,
                         "payload_size": len(code), "resume_rva": resume, "config_rva": config}


def unpack(image):
    if image[:6] != b"\x7fELF\x01\x01" or image[0x1028:0x102c] != b"_FVH":
        raise ValueError("Unsupported firmware container")
    length = struct.unpack_from("<Q", image, 0x1020)[0]
    section_size = read24(image, 0x1060)
    if image[0x1063] != 2 or image[0x1064:0x1074] != bytes.fromhex("98584eee143959429d6edc7bd79403cf"):
        raise ValueError("Unsupported compressed section")
    body = lzma.decompress(image[0x1078:0x1060 + section_size], format=lzma.FORMAT_ALONE)
    if body[:8] != b"\x04\x00\x00\x19" + (len(body) - 4).to_bytes(3, "little") + b"\x17":
        raise ValueError("Unsupported inner FV section")
    if body[0x30:0x34] != b"_FVH" or body[0xb7] != 0x10 or body[0xb8:0xba] != b"MZ":
        raise ValueError("LinuxLoader section was not found")
    pe_length = read24(body, 0xb4) - 4
    if body[0xb8 + pe_length:] != b"\xff" * 16:
        raise ValueError("Unexpected trailing FV data")
    return length, body, body[0xb8:0xb8 + pe_length]


def repack(image, body, efi, length):
    old_size = read24(body, 0xb4) - 4
    delta = len(efi) - old_size
    new_body = bytearray(body[:0xb8] + efi + body[0xb8 + old_size:])
    put24(new_body, 4, len(new_body) - 4)
    struct.pack_into("<Q", new_body, 0x28, struct.unpack_from("<Q", body, 0x28)[0] + delta)
    struct.pack_into("<I", new_body, 0x40, struct.unpack_from("<I", body, 0x40)[0] + delta // 64)
    fv_checksum(new_body, 8)
    put24(new_body, 0x94, read24(body, 0x94) + delta)
    ffs_checksum(new_body, 0x80)
    put24(new_body, 0xb4, len(efi) + 4)
    stream = bytearray(lzma.compress(bytes(new_body), format=lzma.FORMAT_ALONE,
                                   filters=[{"id": lzma.FILTER_LZMA1, "dict_size": 1 << 24,
                                             "lc": 3, "lp": 0, "pb": 2, "mode": lzma.MODE_NORMAL,
                                             "mf": lzma.MF_BT4, "nice_len": 64}]))
    stream[5:13] = len(new_body).to_bytes(8, "little")
    section = bytearray(image[0x1060:0x1078]) + stream
    put24(section, 0, len(section))
    file = bytearray(image[0x1048:0x1060]) + section
    put24(file, 20, len(file))
    ffs_checksum(file, 0)
    if 0x48 + len(file) > length:
        raise ValueError("Compressed loader does not fit in the original FV")
    result = bytearray(image)
    result[0x1048:0x1000 + length] = file + b"\xff" * (length - 0x48 - len(file))
    original_digest = hashlib.sha384(image[0x1000:0x1000 + length]).digest()
    digest = result.find(original_digest, 0x1000 + length)
    if digest < 0 or result.find(original_digest, digest + 1) >= 0:
        raise ValueError("The firmware hash entry is missing or ambiguous")
    result[digest:digest + 48] = hashlib.sha384(result[0x1000:0x1000 + length]).digest()
    if len(result) != len(image) or unpack(result)[2] != efi:
        raise ValueError("Firmware round-trip verification failed")
    return bytes(result), digest


def main():
    parser = argparse.ArgumentParser(description="Restore raw EFISP loading in the two reviewed firmware images")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--payload-dir", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    if args.output.suffix.lower() != ".img":
        raise ValueError("The output must be an .img file")
    if args.output.resolve() == args.input.resolve() or any(
        path.exists() for path in (args.output, args.output.with_suffix(".efi"), args.output.with_suffix(".json"))
    ):
        raise ValueError("Choose a new output file; input and existing outputs are never overwritten")
    image = args.input.read_bytes()
    profile = PROFILES.get(hashlib.sha256(image).hexdigest())
    if not profile:
        raise ValueError("This exact firmware image has not been reviewed")
    if profile.get("blocked"):
        raise ValueError(profile["blocked"])
    symbols = json.loads((args.payload_dir / "payload.json").read_text(encoding="utf-8"))
    payload = (args.payload_dir / "payload.bin").read_bytes()
    if hashlib.sha256(payload).hexdigest() != symbols["sha256"]:
        raise ValueError("Payload checksum mismatch")
    length, body, original = unpack(image)
    if hashlib.sha256(original).hexdigest() != profile["efi_sha256"]:
        raise ValueError("Unexpected LinuxLoader image")
    efi, report = add_loader(original, profile, payload, symbols)
    restored, hash_offset = repack(image, body, efi, length)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(restored)
    args.output.with_suffix(".efi").write_bytes(efi)
    report.update(profile=profile["name"], original_sha256=hashlib.sha256(image).hexdigest(),
                  output_sha256=hashlib.sha256(restored).hexdigest(), hash_offset=hash_offset,
                  original_signature_valid=False, device_tested=False)
    args.output.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Saved {args.output} ({len(restored)} bytes)")
    print("EFISP loading added; missing, invalid or returning EFI continues at the original boot path.")
    print("The original firmware signature is no longer valid. This is an untested image for offline validation.")


if __name__ == "__main__":
    main()
