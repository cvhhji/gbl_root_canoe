#!/system/bin/sh

if [ "$(getprop ro.product.model)" = PYB110 ] && [ -f /data/adb/canoe-efi-test/ready ]; then
  exec sh /data/adb/canoe-efi-test/efi_test.sh arm
fi
echo "Open the module WebUI to manage this installation."
