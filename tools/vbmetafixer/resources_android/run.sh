#!/system/bin/sh
# vbmetafixer (Android, on-device) — graft an OFFICIAL vbmeta footer onto a
# third-party image (e.g. a custom recovery) so it passes fake-lock AVB
# verification, then flash it. Run as root.
#
# Usage:
#   sh run.sh backup                     Back up the official vbmeta chain from
#                                        the active slot into ./vbmetas/
#   sh run.sh flash <partition> <image>  Graft the official footer of
#                                        <partition>'s base onto <image> and
#                                        flash the result to <partition>.
#
# Examples:
#   sh run.sh backup
#   sh run.sh flash recovery_a twrp.img
set -e

SCRIPTDIR=$(dirname "$0")
cd "$SCRIPTDIR"

VBMETA_DIR=./vbmetas
BY_NAME=/dev/block/by-name

usage() {
  cat <<'USAGE'
vbmetafixer (Android, on-device) — graft an OFFICIAL vbmeta footer onto a
third-party image (e.g. a custom recovery) so it passes fake-lock AVB
verification, then flash it. Run as root.

Usage:
  sh run.sh backup                     Back up the official vbmeta chain from
                                       the active slot into ./vbmetas/
  sh run.sh flash <partition> <image>  Graft the official footer of
                                       <partition>'s base onto <image> and
                                       flash the result to <partition>.

Examples:
  sh run.sh backup
  sh run.sh flash recovery_a twrp.img
USAGE
}

# Drop an _a / _b / _ab slot suffix to get the base partition name.
strip_suffix() {
  case "$1" in
    *_ab) echo "${1%_ab}" ;;
    *_a|*_b) echo "${1%_?}" ;;
    *) echo "$1" ;;
  esac
}

do_backup() {
  echo "[*] Backing up official vbmeta chain from the active slot..."
  ./vbmetabackup -o "$VBMETA_DIR"
}

case "$1" in
  backup)
    do_backup
    ;;
  flash)
    part="$2"
    img="$3"
    [ -n "$part" ] && [ -n "$img" ] || { usage; exit 1; }
    [ -f "$img" ] || { echo "[-] Image not found: $img"; exit 1; }
    [ -e "$BY_NAME/$part" ] || { echo "[-] Partition not found: $BY_NAME/$part"; exit 1; }

    base=$(strip_suffix "$part")
    if [ ! -f "$VBMETA_DIR/$base.vbmeta" ]; then
      echo "[*] No backup for '$base' yet, running backup first..."
      do_backup
    fi
    [ -f "$VBMETA_DIR/$base.vbmeta" ] || {
      echo "[-] No $base.vbmeta available after backup, abort"; exit 1;
    }

    out="./grafted_${base}.img"
    echo "[*] Grafting official '$base' vbmeta footer onto $img ..."
    ./vbmetaport "$VBMETA_DIR/$base.vbmeta" "$img" "$out"

    echo "[*] Flashing $out -> $part ..."
    blockdev --setrw "$BY_NAME/$part"
    dd if="$out" of="$BY_NAME/$part" bs=4M conv=fsync
    sync
    rm -f "$out"
    echo "[+] Done. '$part' now carries the official '$base' vbmeta footer."
    ;;
  *)
    usage
    exit 1
    ;;
esac
