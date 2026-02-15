#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-mac"
APP="$BUILD_DIR/RecklessDrivin.app"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
SDL2_DIR="$SCRIPT_DIR/SDL2"
SDL2_VERSION="2.30.11"

# Support macOS 11.0+ (Big Sur)
export MACOSX_DEPLOYMENT_TARGET="11.0"

# Download SDL2 source if not present
if [ ! -f "$SDL2_DIR/CMakeLists.txt" ]; then
    echo "=== Downloading SDL2 $SDL2_VERSION source ==="
    curl -L "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-$SDL2_VERSION.tar.gz" -o /tmp/SDL2.tar.gz
    tar xzf /tmp/SDL2.tar.gz -C "$SCRIPT_DIR"
    mv "$SCRIPT_DIR/SDL2-$SDL2_VERSION" "$SDL2_DIR"
    rm /tmp/SDL2.tar.gz
fi

echo "=== Configuring ==="
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET"

echo "=== Building ==="
cmake --build "$BUILD_DIR" --config Release -j"$JOBS"

echo "=== Extracting resources ==="
cmake --build "$BUILD_DIR" --target extract_resources

echo "=== Bundling app ==="
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/assets"

cp "$BUILD_DIR/RecklessDrivin" "$APP/Contents/MacOS/RecklessDrivin"
cp "$SCRIPT_DIR/RecklessDrivin.icns" "$APP/Contents/Resources/AppIcon.icns"
cp "$SCRIPT_DIR/assets/"* "$APP/Contents/Resources/assets/"
cp "$SCRIPT_DIR/Data" "$APP/Contents/Resources/Data"

cat > "$APP/Contents/Info.plist" << 'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key>
	<string>Reckless Drivin</string>
	<key>CFBundleDisplayName</key>
	<string>Reckless Drivin'</string>
	<key>CFBundleIdentifier</key>
	<string>com.recklessdrivin.game</string>
	<key>CFBundleVersion</key>
	<string>1.0</string>
	<key>CFBundleShortVersionString</key>
	<string>1.0</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleExecutable</key>
	<string>RecklessDrivin</string>
	<key>CFBundleIconFile</key>
	<string>AppIcon</string>
	<key>NSHighResolutionCapable</key>
	<true/>
	<key>LSMinimumSystemVersion</key>
	<string>11.0</string>
</dict>
</plist>
PLIST

echo "=== Signing ==="
codesign --force --deep --sign - "$APP"

echo "=== Done ==="
echo "App bundle: $APP"
