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
      ui_print "- 已恢复本次改动 / Changes restored"
    else
      ui_print "! 自动恢复未完成，不要重启。保留日志与备份：$STATE"
    fi
  fi
  abort "PYB110 EFI test: $1"
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
if [ -f "$STATE/restored" ]; then
  mv "$STATE" "$STATE.restored-$(date +%Y%m%d-%H%M%S)-$$" || abort "Cannot preserve the earlier backup"
fi
[ ! -e "$STATE" ] || abort "Existing test backup: run $STATE/efi_test.sh restore before reinstalling"
[ ! -L "$DEST" ] || abort "persist/efisp is a symlink"
[ ! -e "$PERSIST/.canoe-efi-original" ] && [ ! -L "$PERSIST/.canoe-efi-original" ] || abort "Earlier persist backup exists"
[ ! -e "$PERSIST/.canoe-efi-tested" ] && [ ! -L "$PERSIST/.canoe-efi-tested" ] || abort "Preserved test files exist in persist"
[ -b "$DRIVER" ] || abort "DRIVER not found"
[ "$(blockdev --getsize64 "$DRIVER")" = 33554432 ] || abort "Unexpected DRIVER size"
DRIVER_NODE=$(readlink -f "$DRIVER")
[ "$DRIVER_NODE" = /dev/block/sdf11 ] || abort "Unexpected DRIVER device"
[ "$(cat /sys/class/block/sdf/queue/logical_block_size)" = 4096 ] || abort "Unexpected sector size"
[ "$(hash_efi "$DRIVER")" = "$EXPECTED_DRIVER" ] || abort "DRIVER differs from the checked .155 image"
[ "$(hash_efi "/dev/block/by-name/uefi$SLOT")" = 261f9bb02d1dcac0341c28dfc890519222aa5c2406b461d86526efcfb59c1d43 ] || abort "UEFI differs from .155"
[ "$(hash_efi "/dev/block/by-name/android_esp$SLOT")" = e94ca7ce00dc5c9a9d0dcd4a8dfa240546777b6929bac5f6882965cb02edf103 ] || abort "android_esp differs from .155"
[ "$(hash_efi "/dev/block/by-name/uefisecapp$SLOT")" = 2b4549470adc52806fbb3fcf2958ba9e908833c63181a47ce7455bc9bd6260bd ] || abort "uefisecapp differs from .155"
mkdir "$STATE" || abort "Cannot create backup directory"
cp "$MODPATH/bin/canoe_bootctl" "$MODPATH/bin/efi_test.sh" "$STATE/" || abort "Cannot save recovery tools"
chmod 0700 "$STATE/canoe_bootctl" "$STATE/efi_test.sh"
echo "$SLOT" >"$STATE/slot"
blockdev --getro "$DRIVER" >"$STATE/driver.ro" || fail_efi "Cannot read DRIVER flags"
ui_print "- 一加 16 EFI 实验安装 / OnePlus 16 EFI boot test"
ui_print "- BootNext -> DRIVER/BDS -> persist/efisp/boot.efi -> Android"
ui_print "- 本轮只安排下一次启动，不更改 BootOrder，不自动重启"
ui_print "- 检查变量服务 / Checking UEFI variable access"
"$STATE/canoe_bootctl" preflight >"$STATE/variables.log" 2>&1 || fail_efi "UEFI variable access failed; no partition changes. See $STATE/variables.log"
mkdir "$STATE/variables" || fail_efi "Cannot create variable backup"
"$STATE/canoe_bootctl" snapshot "$STATE/variables" >>"$STATE/variables.log" 2>&1 || fail_efi "Cannot back up boot variables"
"$STATE/canoe_bootctl" option /dev/block/sdf "$STATE/option.bin" >>"$STATE/variables.log" 2>&1 || fail_efi "GPT identity check failed"
dd if="$DRIVER" of="$STATE/DRIVER.original.img" bs=4M >"$STATE/backup.log" 2>&1 || fail_efi "DRIVER backup failed"
[ "$(hash_efi "$STATE/DRIVER.original.img")" = "$EXPECTED_DRIVER" ] || fail_efi "DRIVER backup hash mismatch"
echo "$EXPECTED_DRIVER" >"$STATE/driver.original.sha256"
if [ -d "$DEST" ]; then
  cp -a "$DEST" "$STATE/efisp.original" || fail_efi "persist backup failed"
  touch "$STATE/had-efisp"
fi
mkdir "$STATE/loader" || fail_efi "Cannot create loader directory"
"$MODPATH/bin/extractfv" -o "$STATE/loader" "/dev/block/by-name/android_esp$SLOT" >"$STATE/extract.log" 2>&1 || fail_efi "Cannot extract LinuxLoader"
"$MODPATH/bin/patch_abl" "$STATE/loader/LinuxLoader.efi" "$STATE/boot.efi" >"$STATE/patch.log" 2>&1 || fail_efi "Android EFI patch failed"
grep -q 'Android ESP layout:' "$STATE/patch.log" || fail_efi "Unexpected loader layout"
"$STATE/canoe_bootctl" image "$STATE/DRIVER.original.img" "$MODPATH/BDS.efi" "$STATE/DRIVER.test.img" >"$STATE/layout.log" 2>&1 || fail_efi "DRIVER layout failed"
hash_efi "$STATE/DRIVER.test.img" >"$STATE/driver.test.sha256"
TRANSACTION=yes
"$STATE/canoe_bootctl" reserve "$STATE/option.bin" >>"$STATE/variables.log" 2>&1 || fail_efi "Cannot create Boot6006"
touch "$STATE/persist-changing"
if [ -f "$STATE/had-efisp" ]; then
  mv "$DEST" "$PERSIST/.canoe-efi-original" || fail_efi "Cannot preserve original persist/efisp"
fi
touch "$STATE/persist-owned"
mkdir -p "$DEST" || fail_efi "Cannot create persist/efisp"
cp -r "$MODPATH/efisp/." "$DEST/" || fail_efi "Cannot deploy boot entries"
cp "$STATE/loader/LinuxLoader.efi" "$DEST/boot_backup.efi" || fail_efi "Cannot deploy stock Android EFI"
cp "$STATE/boot.efi" "$DEST/boot.efi" || fail_efi "Cannot deploy patched Android EFI"
cmp -s "$STATE/boot.efi" "$DEST/boot.efi" && cmp -s "$STATE/loader/LinuxLoader.efi" "$DEST/boot_backup.efi" || fail_efi "persist file verification failed"
if [ ! -s "$DEST/MENU.ID" ]; then cat /proc/sys/kernel/random/uuid >"$DEST/MENU.ID" || fail_efi "Cannot create menu identity"; fi
sync
touch "$STATE/driver-changing"
blockdev --setrw "$DRIVER" >>"$STATE/flash.log" 2>&1 || fail_efi "Cannot make DRIVER writable"
dd if="$STATE/DRIVER.test.img" of="$DRIVER" bs=4M conv=fsync >>"$STATE/flash.log" 2>&1 || fail_efi "DRIVER write failed"
sync
[ "$(hash_efi "$DRIVER")" = "$(cat "$STATE/driver.test.sha256")" ] || fail_efi "DRIVER readback mismatch"
if [ "$(cat "$STATE/driver.ro")" = 1 ]; then blockdev --setro "$DRIVER" || fail_efi "Cannot restore DRIVER flags"; fi
"$STATE/canoe_bootctl" arm "$STATE/option.bin" >>"$STATE/variables.log" 2>&1 || fail_efi "BootNext write/readback failed"
touch "$STATE/ready"
TRANSACTION=no
touch "$MODPATH/skip_mount"
sed -i 's/^description=.*/description=PYB110 .155 EFI boot test; one-shot BootNext; backup in \/data\/adb\/canoe-efi-test/' "$MODPATH/module.prop"
ui_print "- 已部署，下一次手动重启验证入口；首次进入应显示启动菜单"
ui_print "- 看见菜单后选择 Android；本轮不修改原厂启动分区、不清数据"
ui_print "- 再次测试：模块操作按钮；恢复：sh $STATE/efi_test.sh restore"
ui_print "- 尚未验证该机的外部 EFI 执行策略；安装失败请保留 $STATE 中的日志"
