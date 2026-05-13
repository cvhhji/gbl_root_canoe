clean:
	rm -rf edk2/Build || true
	rm -rf edk2/Conf || true
	rm edk2/QcomModulePkg/Include/Library/ABL.h || true
	rm tools/patch_abl || true
	rm -rf dist || true
	rm -rf ./build || true
	mkdir dist
patch: clean
	gcc -O2 -o ./tools/extractfv ./tools/extractfv.c -llzma
	./tools/extractfv ./images/abl.img -o ./dist
	rm ./tools/extractfv
	mv ./dist/LinuxLoader.efi ./dist/ABL_original.efi
	gcc -o tools/patch_abl tools/patch_abl.c
	./tools/patch_abl ./dist/ABL_original.efi ./dist/ABL.efi > ./dist/patch_log.txt
	rm tools/patch_abl
	cat ./dist/patch_log.txt
build_loader: clean
	cp -r ./Conf ./edk2/
	bash -c 'cd edk2 && . ./edksetup.sh && make BOARD_BOOTLOADER_PRODUCT_NAME=canoe TARGET_ARCHITECTURE=AARCH64 TARGET=RELEASE \
  		CLANG_BIN=/usr/bin/ CLANG_PREFIX=aarch64-linux-gnu- VERIFIED_BOOT_ENABLED=1 \
  		VERIFIED_BOOT_LE=0 AB_RETRYCOUNT_DISABLE=0 TARGET_BOARD_TYPE_AUTO=0 \
  		BUILD_USES_RECOVERY_AS_BOOT=0 DISABLE_PARALLEL_DOWNLOAD_FLASH=0 PVMFW_BCC_ENABLED=-DPVMFW_BCC\
  		REMOVE_CARVEOUT_REGION=1 QSPA_BOOTCONFIG_ENABLE=1 USER_BUILD_VARIANT=0 \
  		PREBUILT_HOST_TOOLS="BUILD_CC=clang BUILD_CXX=clang++ LDPATH=-fuse-ld=lld BUILD_AR=llvm-ar"' || true
	# test if the build is successful by checking the output file
	if [ ! -f edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ]; then \
		echo "Build failed"; \
		exit 1; \
	fi
	mkdir -p dist
	cp edk2/Build/RELEASE_CLANG35/AARCH64/QcomModulePkg/Application/LinuxLoader/LinuxLoader/DEBUG/LinuxLoader.dll ./dist/loader.elf
	@echo "Loader built successfully: dist/loader.elf"

build: patch build_loader
	gcc -O2 -o tools/elf_inject tools/elf_inject.c
	./tools/elf_inject ./dist/loader.elf ./dist/ABL.efi ./dist/ABL_with_superfastboot.dll
	rm tools/elf_inject
	edk2/Build/Source/C/bin/GenFw -e UEFI_APPLICATION -o ./dist/ABL_with_superfastboot.efi ./dist/ABL_with_superfastboot.dll
	rm ./dist/ABL_with_superfastboot.dll
	ls -l ./dist

dist_loader: build_loader
	mkdir ./dist/images
	touch ./dist/images/PUT_ABL_IMAGE_HERE
	mkdir ./dist/bin
	mkdir release || true
	gcc -O2 -o ./dist/bin/elf_inject ./tools/elf_inject.c
	cp ./edk2/Build/Source/C/bin/GenFw ./dist/bin
	gcc -o ./dist/bin/extractfv ./tools/extractfv.c -llzma
	gcc -o ./dist/bin/patch_abl ./tools/patch_abl.c
	cp ./tools/build.sh ./dist
	cp ./tools/Makefile_dist ./dist/Makefile
	zip -r release/$(DIST_NAME)_linux.zip dist

dist_loader_windows: build_loader
	#build with mingw-w64
	mkdir -p ./dist/images
	touch ./dist/images/PUT_ABL_IMAGE_HERE
	mkdir -p ./dist/bin
	x86_64-w64-mingw32-gcc -O2 -o ./dist/bin/elf_inject.exe ./tools/elf_inject.c
	x86_64-w64-mingw32-gcc -O2 -fshort-wchar -fno-strict-aliasing -fwrapv \
		-Iedk2/BaseTools/Source/C -Iedk2/BaseTools/Source/C/Include -Iedk2/BaseTools/Source/C/Include/Common -Iedk2/BaseTools/Source/C/Include/IndustryStandard -Iedk2/BaseTools/Source/C/Include/AArch64 -Iedk2/BaseTools/Source/C/Common -Iedk2/BaseTools/Source/C/GenFw \
		-o ./dist/bin/GenFw.exe \
		edk2/BaseTools/Source/C/GenFw/GenFw.c edk2/BaseTools/Source/C/GenFw/ElfConvert.c edk2/BaseTools/Source/C/GenFw/Elf32Convert.c edk2/BaseTools/Source/C/GenFw/Elf64Convert.c \
		edk2/BaseTools/Source/C/Common/BasePeCoff.c edk2/BaseTools/Source/C/Common/BinderFuncs.c edk2/BaseTools/Source/C/Common/CommonLib.c edk2/BaseTools/Source/C/Common/Crc32.c edk2/BaseTools/Source/C/Common/Decompress.c edk2/BaseTools/Source/C/Common/EfiCompress.c edk2/BaseTools/Source/C/Common/EfiUtilityMsgs.c edk2/BaseTools/Source/C/Common/FirmwareVolumeBuffer.c edk2/BaseTools/Source/C/Common/FvLib.c edk2/BaseTools/Source/C/Common/MemoryFile.c edk2/BaseTools/Source/C/Common/MyAlloc.c edk2/BaseTools/Source/C/Common/OsPath.c edk2/BaseTools/Source/C/Common/ParseGuidedSectionTools.c edk2/BaseTools/Source/C/Common/ParseInf.c edk2/BaseTools/Source/C/Common/PeCoffLoaderEx.c edk2/BaseTools/Source/C/Common/SimpleFileParsing.c edk2/BaseTools/Source/C/Common/StringFuncs.c edk2/BaseTools/Source/C/Common/TianoCompress.c \
		-Wl,-Bstatic -luuid -Wl,-Bdynamic
	bash ./tools/build_extractfv_windows.sh
	x86_64-w64-mingw32-gcc -o ./dist/bin/patch_abl.exe ./tools/patch_abl.c
	cp ./tools/build.bat ./dist
	zip -r release/$(DIST_NAME)_windows.zip dist

dist_loader_android: build_patcher_android build_loader
	mkdir -p ./dist/images
	touch ./dist/images/PUT_ABL_IMAGE_HERE
	mkdir -p ./dist/bin
	mv ./dist/patch_abl_android ./dist/bin/patch_abl
	mv ./dist/extractfv_android ./dist/bin/extractfv
	mv ./dist/GenFw_android ./dist/bin/GenFw
	mv ./dist/elf_inject_android ./dist/bin/elf_inject
	cp ./tools/build.sh ./dist
	zip -r release/$(DIST_NAME)_android.zip dist

dist: build
	mkdir release
	zip -r release/$(DIST_NAME).zip dist

build_superfbonly: clean
	cp -r ./Conf ./edk2/
	bash -c 'cd edk2 && . ./edksetup.sh && make BOARD_BOOTLOADER_PRODUCT_NAME=canoe TARGET_ARCHITECTURE=AARCH64 TARGET=RELEASE \
  		CLANG_BIN=/usr/bin/ CLANG_PREFIX=aarch64-linux-gnu- VERIFIED_BOOT_ENABLED=1 \
  		VERIFIED_BOOT_LE=0 AB_RETRYCOUNT_DISABLE=0 TARGET_BOARD_TYPE_AUTO=0 \
  		BUILD_USES_RECOVERY_AS_BOOT=0 DISABLE_PARALLEL_DOWNLOAD_FLASH=0 PVMFW_BCC_ENABLED=-DPVMFW_BCC\
  		REMOVE_CARVEOUT_REGION=1 QSPA_BOOTCONFIG_ENABLE=1 USER_BUILD_VARIANT=0 TEST_ADAPTER=1 \
  		PREBUILT_HOST_TOOLS="BUILD_CC=clang BUILD_CXX=clang++ LDPATH=-fuse-ld=lld BUILD_AR=llvm-ar"' || true
	# test if the build is successful by checking the output file
	if [ ! -f edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ]; then \
		echo "Build failed"; \
		exit 1; \
	fi
	cp edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ./dist/superfastboot.efi
	ls -l ./dist

build_generic: clean
	cp -r ./Conf ./edk2/
	bash -c 'cd edk2 && . ./edksetup.sh && make BOARD_BOOTLOADER_PRODUCT_NAME=canoe TARGET_ARCHITECTURE=AARCH64 TARGET=RELEASE \
  		CLANG_BIN=/usr/bin/ CLANG_PREFIX=aarch64-linux-gnu- VERIFIED_BOOT_ENABLED=1 \
  		VERIFIED_BOOT_LE=0 AB_RETRYCOUNT_DISABLE=0 TARGET_BOARD_TYPE_AUTO=0 \
  		BUILD_USES_RECOVERY_AS_BOOT=0 DISABLE_PARALLEL_DOWNLOAD_FLASH=0 PVMFW_BCC_ENABLED=-DPVMFW_BCC\
  		REMOVE_CARVEOUT_REGION=1 QSPA_BOOTCONFIG_ENABLE=1 USER_BUILD_VARIANT=0 AUTO_PATCH_ABL=1 DISABLE_PRINT=1\
  		PREBUILT_HOST_TOOLS="BUILD_CC=clang BUILD_CXX=clang++ LDPATH=-fuse-ld=lld BUILD_AR=llvm-ar"' || true
	# test if the build is successful by checking the output file
	if [ ! -f edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ]; then \
		echo "Build failed"; \
		exit 1; \
	fi
	cp edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ./dist/generic_superfastboot.efi
	ls -l ./dist

build_patcher_android: clean
	$(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang tools/patch_abl.c -o dist/patch_abl_android
	bash ./tools/build_extractfv_android.sh
	bash ./tools/build_genfw_android.sh
build_module: build_patcher_android build_loader
	cp dist/loader.elf magisk_module/loader.elf
	mv dist/patch_abl_android magisk_module/bin/patch_abl
	mv dist/extractfv_android magisk_module/bin/extractfv
	mv dist/GenFw_android magisk_module/bin/GenFw
	mv dist/elf_inject_android magisk_module/bin/elf_inject
	mkdir release || true
	cd magisk_module && zip -r ../release/$(DIST_NAME).zip ./
	rm magisk_module/bin/patch_abl
	rm magisk_module/bin/extractfv
	rm magisk_module/bin/GenFw
	rm magisk_module/bin/elf_inject
	rm magisk_module/loader.elf

test_exploit:
	@echo "This script is used to test the ABL exploit. Please make sure you tested before ota."
	@echo Please enter the Builtin Fastboot in the project. And put abl.img in the images folder. Press Enter to continue.
	@bash -c read -n 1 -s
	@python tools/extractfv.py ./images/abl.img ./ABL_original.efi
	@fastboot boot ./ABL_original.efi
	@echo 'If the exploit existed in the new abl image, the device will show two lines of "Press Volume Down key to enter Fastboot mode, waiting for 5 seconds into Normal mode..."'
	@echo 'If the exploit does not exist in the new abl image, the device will show red state screen'
	@rm ./ABL_original.efi
test_boot: build
	fastboot boot ./dist/ABL_with_superfastboot.efi

test:
	bash ./tests/runall.sh
