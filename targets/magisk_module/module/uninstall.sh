#!/system/bin/sh

MODDIR=${0%/*}

if [ "$(getprop ro.product.model)" = PYB110 ] && [ -d /data/adb/canoe-efi-test ]; then
  sh /data/adb/canoe-efi-test/efi_test.sh restore
  exit $?
fi

rm -rf "$MODDIR/tmp"
ui_print "卸载完成,仅卸载OTA更新辅助，假回锁请自行卸载（因为需要清数据）"
