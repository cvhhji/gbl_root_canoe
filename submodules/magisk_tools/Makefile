clean:
	rm -rf build || true

build_android: build/patch_tools

build/patch_tools: build_patch_tools.sh patch.cpp NOTICE ../../LICENSE
	sh build_patch_tools.sh

.PHONY: clean build_android
