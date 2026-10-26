#!/system/bin/sh

umask 077
STATE=/data/adb/canoe-efi-test
DEST=/mnt/vendor/persist/efisp
DRIVER=/dev/block/by-name/DRIVER
die_efi() {
  echo "! $1"
  exit 1
}
hash_efi() { sha256sum "$1" | cut -d' ' -f1; }
[ "$(id -u)" = 0 ] || die_efi "root required"
[ "$(getprop ro.product.model)" = PYB110 ] || die_efi "Wrong device"
[ -d "$STATE" ] && [ ! -L "$STATE" ] || die_efi "Backup directory missing"
[ -x "$STATE/canoe_bootctl" ] || die_efi "Recovery helper missing"
if [ -f "$STATE/option.bin" ]; then
  "$STATE/canoe_bootctl" verify-option "$STATE/option.bin" || die_efi "Device GPT no longer matches this backup"
fi
case "${1:-}" in
arm)
  [ -f "$STATE/ready" ] || die_efi "Installation incomplete"
  [ "$(getprop ro.boot.slot_suffix)" = "$(cat "$STATE/slot")" ] || die_efi "Slot changed; restore and reinstall"
  [ "$(hash_efi "$DRIVER")" = "$(cat "$STATE/driver.test.sha256")" ] || die_efi "DRIVER changed"
  cmp -s "$DEST/boot.efi" "$STATE/boot.efi" || die_efi "Android EFI changed"
  "$STATE/canoe_bootctl" arm "$STATE/option.bin" || die_efi "Cannot set BootNext"
  echo "BootNext set for one boot. Reboot manually."
  ;;
rollback | restore)
  [ ! -f "$STATE/restored" ] || {
    echo "Already restored"
    exit 0
  }
  if [ -f "$STATE/option.bin" ]; then
    "$STATE/canoe_bootctl" disarm "$STATE/option.bin" || die_efi "Cannot remove test boot variables; do not reboot"
  fi
  if [ -f "$STATE/driver-changing" ]; then
    [ "$(hash_efi "$STATE/DRIVER.original.img")" = "$(cat "$STATE/driver.original.sha256")" ] || die_efi "Backup hash mismatch"
    [ "$(blockdev --getsize64 "$DRIVER")" = 33554432 ] || die_efi "Wrong DRIVER size"
    blockdev --setrw "$DRIVER" || die_efi "DRIVER is not writable"
    dd if="$STATE/DRIVER.original.img" of="$DRIVER" bs=4M conv=fsync || die_efi "DRIVER restore failed"
    sync
    [ "$(hash_efi "$DRIVER")" = "$(cat "$STATE/driver.original.sha256")" ] || die_efi "DRIVER restore readback failed"
    if [ "$(cat "$STATE/driver.ro")" = 1 ]; then blockdev --setro "$DRIVER" || die_efi "Cannot restore DRIVER flags"; fi
  fi
  if [ -f "$STATE/persist-changing" ]; then
    grep -q ' /mnt/vendor/persist ext4 .*rw' /proc/mounts || die_efi "persist not mounted"
    [ ! -L "$DEST" ] || die_efi "Unexpected persist/efisp symlink"
    if [ -f "$STATE/persist-owned" ] || [ -d /mnt/vendor/persist/.canoe-efi-original ]; then
      if [ -e "$DEST" ]; then
        [ ! -e /mnt/vendor/persist/.canoe-efi-tested ] || die_efi "Preserved test files already exist"
        mv "$DEST" /mnt/vendor/persist/.canoe-efi-tested || die_efi "Cannot preserve test files"
      fi
      if [ -f "$STATE/had-efisp" ]; then
        [ -d /mnt/vendor/persist/.canoe-efi-original ] && [ ! -L /mnt/vendor/persist/.canoe-efi-original ] || die_efi "Original persist directory missing"
        mv /mnt/vendor/persist/.canoe-efi-original "$DEST" || die_efi "persist restore failed"
      fi
    fi
  fi
  sync
  touch "$STATE/restored"
  echo "Restored. Backups and logs remain in $STATE. No reboot was requested."
  ;;
*)
  echo "Usage: sh $STATE/efi_test.sh arm|restore"
  exit 2
  ;;
esac
