#!/bin/bash
# Copyright 2020 Andreas Atteneder
# SPDX-License-Identifier: Apache-2.0

set -e

# Fallback to arm64-v8a
ANDROID_ABI=${ANDROID_ABI:-'arm64-v8a'}
ASTC_ISA=${ASTC_ISA:-'ISA_NONE=ON'}
CONFIGURATION=${CONFIGURATION:-Release}

BUILD_DIR="build-android-$ANDROID_ABI"

# You need to set the following environment variables first
# ANDROID_NDK= <Path to Android NDK>

cmake_args=("-G" "Ninja" \
  "-B" "$BUILD_DIR" \
  "-D" "ANDROID_PLATFORM=android-24" \
  "-D" "ANDROID_ABI=$ANDROID_ABI" \
  "-D" "ANDROID_NDK=$ANDROID_NDK" \
  "-D" "CMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  "-D" "CMAKE_BUILD_TYPE=$CONFIGURATION" \
  "-D" "BASISU_SUPPORT_SSE=OFF" \
  "-D" "${ASTC_ISA}"
)

config_display="Configure KTX-Software (Android $ANDROID_ABI $CONFIGURATION): "
for arg in "${cmake_args[@]}"; do
  echo $arg
  case $arg in
    "-G") config_display+="Generator=" ;;
    "-B") config_display+="Build Dir=" ;;
    "-D") ;;
    *) config_display+="$arg, " ;;
  esac
done

echo ${config_display%??}
cmake . "${cmake_args[@]}"

pushd "$BUILD_DIR"

echo "Build KTX-Software (Android $ANDROID_ABI $CONFIGURATION)"
cmake --build . --config $CONFIGURATION -j
# echo "Test KTX-Software (Android $ANDROID_ABI Release)"
# ctest -C Release # --verbose
if [ "$CONFIGURATION" = "Release" ]; then
  echo "Install KTX-Software (Android $ANDROID_ABI $CONFIGURATION)"
  cmake --install . --config Release --prefix ../install-android/$ANDROID_ABI
fi

popd
