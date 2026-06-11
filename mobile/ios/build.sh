#!/bin/bash
# Build script for iOS QminiVoice
# Run on macOS with Xcode installed

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SHARED_DIR="$SCRIPT_DIR/../shared"
OPUS_DIR="$SCRIPT_DIR/../../src/opus"
BUILD_DIR="$SCRIPT_DIR/build"

echo "=== QminiVoice iOS Build ==="
echo "Shared sources: $SHARED_DIR"
echo "Opus sources:   $OPUS_DIR"

# Check for Xcode
if ! command -v xcodebuild &>/dev/null; then
    echo "Error: Xcode not found. Install Xcode from the App Store."
    exit 1
fi

mkdir -p "$BUILD_DIR"

# ---- Compile Opus ----
echo ""
echo "--- Building Opus ---"
OPUS_C_SOURCES=(
    "$OPUS_DIR/src/opus.c"
    "$OPUS_DIR/src/opus_decoder.c"
    "$OPUS_DIR/src/opus_encoder.c"
    "$OPUS_DIR/src/opus_multistream.c"
    "$OPUS_DIR/src/opus_multistream_encoder.c"
    "$OPUS_DIR/src/opus_multistream_decoder.c"
    "$OPUS_DIR/src/repacketizer.c"
    "$OPUS_DIR/src/analysis.c"
    "$OPUS_DIR/src/mlp.c"
    "$OPUS_DIR/src/mlp_data.c"
    "$OPUS_DIR/src/opus_projection_encoder.c"
    "$OPUS_DIR/src/opus_projection_decoder.c"
    "$OPUS_DIR/src/mapping_matrix.c"
)
# Add CELT sources
for f in "$OPUS_DIR"/celt/*.c; do
    OPUS_C_SOURCES+=("$f")
done
# Add SILK sources
for f in "$OPUS_DIR"/silk/*.c; do
    OPUS_C_SOURCES+=("$f")
done

OPUS_INCLUDES=(
    "-I$OPUS_DIR/include"
    "-I$OPUS_DIR/src"
    "-I$OPUS_DIR/celt"
    "-I$OPUS_DIR/silk"
    "-I$OPUS_DIR/silk/fixed"
)

OPUS_DEFINES="-DOPUS_BUILD -DFIXED_POINT -DUSE_ALLOCA -DHAVE_LRINT -DHAVE_LRINTF"

echo "Compiling Opus for arm64..."
for src in "${OPUS_C_SOURCES[@]}"; do
    obj="$BUILD_DIR/opus_$(basename "$src" .c).o"
    xcrun -sdk iphoneos clang \
        -arch arm64 \
        -isysroot "$(xcrun --sdk iphoneos --show-sdk-path)" \
        -miphoneos-version-min=14.0 \
        -O2 -std=c11 -w \
        $OPUS_DEFINES \
        "${OPUS_INCLUDES[@]}" \
        -c "$src" -o "$obj" 2>/dev/null
done
echo "Opus compiled."

# ---- Compile shared C core ----
echo ""
echo "--- Building Qmini Core ---"
CORE_SOURCES=(
    "$SHARED_DIR/protocol/qmini_protocol.c"
    "$SHARED_DIR/crypto/crypto.c"
    "$SHARED_DIR/crypto/sha256.c"
    "$SHARED_DIR/crypto/tiny_aes.c"
    "$SHARED_DIR/jitter/jitter_buffer.c"
    "$SHARED_DIR/codec/codec.c"
    "$SCRIPT_DIR/QminiVoice/QminiVoice/platform_ios.c"
)

CORE_INCLUDES=(
    "-I$SHARED_DIR"
    "-I$SHARED_DIR/protocol"
    "-I$SHARED_DIR/crypto"
    "-I$SHARED_DIR/jitter"
    "-I$SHARED_DIR/audio"
    "-I$SHARED_DIR/codec"
    "-I$OPUS_DIR/include"
)

echo "Compiling core for arm64..."
for src in "${CORE_SOURCES[@]}"; do
    obj="$BUILD_DIR/core_$(basename "$src" .c).o"
    echo "  CC $(basename "$src")"
    xcrun -sdk iphoneos clang \
        -arch arm64 \
        -isysroot "$(xcrun --sdk iphoneos --show-sdk-path)" \
        -miphoneos-version-min=14.0 \
        -O2 -std=c11 \
        "${CORE_INCLUDES[@]}" \
        -c "$src" -o "$obj"
done

# ---- Create static library ----
echo ""
echo "Creating libqmini_core.a"
xcrun -sdk iphoneos ar rcs "$BUILD_DIR/libqmini_core.a" "$BUILD_DIR"/*.o

echo ""
echo "=== Build complete ==="
echo "Output: $BUILD_DIR/libqmini_core.a"
echo ""
echo "Next steps:"
echo "  1. Open QminiVoice project in Xcode"
echo "  2. Add libqmini_core.a to 'Link Binary With Libraries'"
echo "  3. Set Bridging Header to QminiVoice-Bridging-Header.h"
echo "  4. Add libopus.tbd or use the static lib"
echo "  5. Build and run on device"
