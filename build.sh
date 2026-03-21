bash clean.sh
python tools/extractfv.py ./images/abl.img ./dist/ABL_original.efi
g++ -o tools/patch_abl tools/patch_abl.cpp
./tools/patch_abl ./dist/ABL_original.efi ./dist/ABL_patched.efi > patch_log.txt
mv ./dist/ABL_patched.efi ./dist/ABL.efi  
xxd -i dist/ABL.efi > edk2/QcomModulePkg/Include/Library/ABL.h
cp -r ./Conf ./edk2/
cd edk2
source edksetup.sh

make BOARD_BOOTLOADER_PRODUCT_NAME=canoe TARGET_ARCHITECTURE=AARCH64 TARGET=RELEASE \
  CLANG_BIN=/usr/bin/ CLANG_PREFIX=aarch64-linux-gnu- VERIFIED_BOOT_ENABLED=1 \
  VERIFIED_BOOT_LE=0 AB_RETRYCOUNT_DISABLE=0 TARGET_BOARD_TYPE_AUTO=0 \
  BUILD_USES_RECOVERY_AS_BOOT=0 DISABLE_PARALLEL_DOWNLOAD_FLASH=0 PVMFW_BCC_ENABLED=-DPVMFW_BCC\
  REMOVE_CARVEOUT_REGION=1 QSPA_BOOTCONFIG_ENABLE=1 USER_BUILD_VARIANT=0 \
  PREBUILT_HOST_TOOLS="BUILD_CC=clang BUILD_CXX=clang++ LDPATH=-fuse-ld=lld BUILD_AR=llvm-ar"
cd ../
cp edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ./dist/ABL_with_superfastboot.efi
rm ./dist/ABL_original.efi
cat patch_log.txt
ls -l ./dist
