#!/system/bin/sh

umask 077
STATE=/data/adb/canoe-efi-test
PERSIST=/mnt/vendor/persist
DEST=$PERSIST/efisp
DRIVER=/dev/block/by-name/DRIVER
EXPECTED_DRIVER=a8a168177a6d5968b0c4914c1d33eb980a74f5363e553cce8bb7dc84b4ac09a9

fail_efi() {
  ui_print "! $1"
  if [ "$TRANSACTION" = yes ]; then
    if sh "$STATE/efi_test.sh" rollback; then
      ui_print "- 已恢复本次改动"
    else
      ui_print "! 自动恢复未完成，不要重启。保留日志与备份：$STATE"
    fi
  fi
  abort "$1"
}

hash_efi() {
  sha256sum "$1" | cut -d' ' -f1
}

[ "$(id -u)" = 0 ] || abort "root required"
[ "${BOOTMODE:-false}" = true ] || abort "Install from the root manager in Android"
[ "$(getprop ro.build.display.id)" = 'PYB110_17.0.0.155(CN01)' ] || abort "This EFI test supports PYB110 .155 only"
[ "$(getprop ro.boot.vbmeta.digest)" = 0d2126af42f975eb68a41fe3b349c9c7f71269d18a5c5e038b6a07da0a5af78c ] || abort "AVB chain differs from the checked .155 firmware"
case "$(getprop ro.boot.slot_suffix)" in _a | _b) SLOT=$(getprop ro.boot.slot_suffix) ;; *) abort "Unknown active slot" ;; esac
grep -q " $PERSIST ext4 .*rw" /proc/mounts || abort "persist must be mounted as writable ext4"
for tool in dd sha256sum blockdev readlink cp cmp sync; do
  command -v "$tool" >/dev/null 2>&1 || abort "Missing $tool"
done
set_perm_recursive "$MODPATH/bin" 0 0 0755 0755
[ ! -L "$STATE" ] || abort "Backup path is a symlink"
if [ -e "$STATE" ]; then
  [ -d "$STATE" ] || abort "Backup path is not a directory"
  if [ ! -f "$STATE/restored" ]; then
    for marker in option.bin variables-changing persist-changing persist-owned driver-changing ready; do
      [ ! -e "$STATE/$marker" ] && [ ! -L "$STATE/$marker" ] || abort "上次安装需要先恢复：sh $STATE/efi_test.sh restore"
    done
  fi
fi
[ ! -L "$DEST" ] || abort "persist/efisp is a symlink"
[ ! -e "$PERSIST/.canoe-efi-original" ] && [ ! -L "$PERSIST/.canoe-efi-original" ] || abort "Earlier persist backup exists"
if [ -e "$PERSIST/.canoe-efi-tested" ] || [ -L "$PERSIST/.canoe-efi-tested" ]; then
  [ -f "$STATE/restored" ] && [ -d "$PERSIST/.canoe-efi-tested" ] && [ ! -L "$PERSIST/.canoe-efi-tested" ] || abort "Preserved files need to be restored first"
  [ ! -e "$STATE/efisp.tested" ] && [ ! -L "$STATE/efisp.tested" ] || abort "Earlier persist backup already exists"
fi
[ -b "$DRIVER" ] || abort "DRIVER not found"
[ "$(blockdev --getsize64 "$DRIVER")" = 33554432 ] || abort "Unexpected DRIVER size"
DRIVER_NODE=$(readlink -f "$DRIVER")
[ "$DRIVER_NODE" = /dev/block/sdf11 ] || abort "Unexpected DRIVER device"
[ "$(cat /sys/class/block/sdf/queue/logical_block_size)" = 4096 ] || abort "Unexpected sector size"
[ "$(hash_efi "$DRIVER")" = "$EXPECTED_DRIVER" ] || abort "DRIVER differs from the checked .155 image"
[ "$(hash_efi "/dev/block/by-name/uefi$SLOT")" = 261f9bb02d1dcac0341c28dfc890519222aa5c2406b461d86526efcfb59c1d43 ] || abort "UEFI differs from .155"
[ "$(hash_efi "/dev/block/by-name/android_esp$SLOT")" = e94ca7ce00dc5c9a9d0dcd4a8dfa240546777b6929bac5f6882965cb02edf103 ] || abort "android_esp differs from .155"
[ "$(hash_efi "/dev/block/by-name/uefisecapp$SLOT")" = 2b4549470adc52806fbb3fcf2958ba9e908833c63181a47ce7455bc9bd6260bd ] || abort "uefisecapp differs from .155"
if [ -d "$STATE" ]; then
  ARCHIVE="$STATE.previous-$(date +%Y%m%d-%H%M%S)-$$"
  [ ! -e "$ARCHIVE" ] && [ ! -L "$ARCHIVE" ] || abort "Backup archive already exists"
  if [ -d "$PERSIST/.canoe-efi-tested" ]; then
    mv "$PERSIST/.canoe-efi-tested" "$STATE/efisp.tested" || abort "Cannot preserve earlier boot files"
  fi
  mv "$STATE" "$ARCHIVE" || abort "Cannot preserve the earlier backup"
fi
mkdir "$STATE" || abort "Cannot create backup directory"
cp "$MODPATH/bin/canoe_bootctl" "$MODPATH/bin/efi_test.sh" "$STATE/" || abort "Cannot save recovery tools"
chmod 0700 "$STATE/canoe_bootctl" "$STATE/efi_test.sh"
echo "$SLOT" >"$STATE/slot"
blockdev --getro "$DRIVER" >"$STATE/driver.ro" || fail_efi "Cannot read DRIVER flags"
ui_print "- 检查安装条件"
"$STATE/canoe_bootctl" preflight >"$STATE/variables.log" 2>&1 || fail_efi "变量服务不可用，请查看 $STATE/variables.log"
mkdir "$STATE/variables" || fail_efi "Cannot create variable backup"
"$STATE/canoe_bootctl" snapshot "$STATE/variables" >>"$STATE/variables.log" 2>&1 || fail_efi "Cannot back up boot variables"
"$STATE/canoe_bootctl" option /dev/block/sdf "$STATE/option.bin" >>"$STATE/variables.log" 2>&1 || fail_efi "GPT identity check failed"
ui_print "- 备份启动文件"
dd if="$DRIVER" of="$STATE/DRIVER.original.img" bs=4M >"$STATE/backup.log" 2>&1 || fail_efi "DRIVER backup failed"
[ "$(hash_efi "$STATE/DRIVER.original.img")" = "$EXPECTED_DRIVER" ] || fail_efi "DRIVER backup hash mismatch"
echo "$EXPECTED_DRIVER" >"$STATE/driver.original.sha256"
if [ -d "$DEST" ]; then
  cp -a "$DEST" "$STATE/efisp.original" || fail_efi "persist backup failed"
  touch "$STATE/had-efisp" || fail_efi "Cannot save persist backup state"
fi
mkdir "$STATE/loader" || fail_efi "Cannot create loader directory"
"$MODPATH/bin/extractfv" -o "$STATE/loader" "/dev/block/by-name/android_esp$SLOT" >"$STATE/extract.log" 2>&1 || fail_efi "Cannot extract LinuxLoader"
"$MODPATH/bin/patch_abl" "$STATE/loader/LinuxLoader.efi" "$STATE/boot.efi" >"$STATE/patch.log" 2>&1 || fail_efi "Android EFI patch failed"
grep -q 'Android ESP layout:' "$STATE/patch.log" || fail_efi "Unexpected loader layout"
"$STATE/canoe_bootctl" image "$STATE/DRIVER.original.img" "$MODPATH/BDS.efi" "$STATE/DRIVER.test.img" >"$STATE/layout.log" 2>&1 || fail_efi "DRIVER layout failed"
hash_efi "$STATE/DRIVER.test.img" >"$STATE/driver.test.sha256"
touch "$STATE/variables-changing" || fail_efi "Cannot save variable backup state"
TRANSACTION=yes
"$STATE/canoe_bootctl" reserve "$STATE/option.bin" >>"$STATE/variables.log" 2>&1 || fail_efi "Cannot create Boot6006"
ui_print "- 安装启动文件"
touch "$STATE/persist-changing" || fail_efi "Cannot save persist restore state"
if [ -f "$STATE/had-efisp" ]; then
  mv "$DEST" "$PERSIST/.canoe-efi-original" || fail_efi "Cannot preserve original persist/efisp"
fi
touch "$STATE/persist-owned" || fail_efi "Cannot save persist install state"
mkdir -p "$DEST" || fail_efi "Cannot create persist/efisp"
cp -r "$MODPATH/efisp/." "$DEST/" || fail_efi "Cannot deploy boot entries"
cp "$STATE/loader/LinuxLoader.efi" "$DEST/boot_backup.efi" || fail_efi "Cannot deploy stock Android EFI"
cp "$STATE/boot.efi" "$DEST/boot.efi" || fail_efi "Cannot deploy patched Android EFI"
cmp -s "$STATE/boot.efi" "$DEST/boot.efi" && cmp -s "$STATE/loader/LinuxLoader.efi" "$DEST/boot_backup.efi" || fail_efi "persist file verification failed"
if [ ! -s "$DEST/MENU.ID" ]; then cat /proc/sys/kernel/random/uuid >"$DEST/MENU.ID" || fail_efi "Cannot create menu identity"; fi
sync
touch "$STATE/driver-changing" || fail_efi "Cannot save DRIVER restore state"
blockdev --setrw "$DRIVER" >>"$STATE/flash.log" 2>&1 || fail_efi "Cannot make DRIVER writable"
dd if="$STATE/DRIVER.test.img" of="$DRIVER" bs=4M conv=fsync >>"$STATE/flash.log" 2>&1 || fail_efi "DRIVER write failed"
sync
[ "$(hash_efi "$DRIVER")" = "$(cat "$STATE/driver.test.sha256")" ] || fail_efi "DRIVER readback mismatch"
if [ "$(cat "$STATE/driver.ro")" = 1 ]; then blockdev --setro "$DRIVER" || fail_efi "Cannot restore DRIVER flags"; fi
"$STATE/canoe_bootctl" arm "$STATE/option.bin" >>"$STATE/variables.log" 2>&1 || fail_efi "BootNext write/readback failed"
touch "$STATE/ready" || fail_efi "Cannot save installation state"
touch "$MODPATH/skip_mount" || fail_efi "Cannot save module state"
sed -i 's/^description=.*/description=启动菜单与假回锁/' "$MODPATH/module.prop" || fail_efi "Cannot update module information"
TRANSACTION=no
ui_print "- 安装完成"
