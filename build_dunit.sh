#!/bin/bash
set -e

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DUNIT_DIR="/home/tuxpc/dunit-os"
SYSROOT="$DUNIT_DIR/build/dunit-sysroot"
CC="$SYSROOT/bin/dunit-cc"
CARGO_DUNIT="python3 $DUNIT_DIR/tools/cargo-dunit"

echo "=== Building Minecraft C Edition for Dunit OS ==="

if [ ! -f "$REPO_DIR/assets/generated/world_atlas.png" ]; then
    echo "Generating world_atlas.png with alpha transparency..."
    python3 -c "
import zlib, struct
with open('$REPO_DIR/assets/generated/world_atlas.rgba', 'rb') as f: raw = f.read()
width, height = 1024, 1024
line_len = width * 4
raw_lines = bytearray((line_len + 1) * height)
for y in range(height):
    raw_lines[y * (line_len + 1)] = 0
    raw_lines[y * (line_len + 1) + 1 : y * (line_len + 1) + 1 + line_len] = raw[y * line_len : (y + 1) * line_len]
compressed = zlib.compress(bytes(raw_lines), 6)
def chunk(tag, data):
    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) + chunk(b'IDAT', compressed) + chunk(b'IEND', b'')
with open('$REPO_DIR/assets/generated/world_atlas.png', 'wb') as f: f.write(png)
"
fi

mkdir -p "$REPO_DIR/dunit_build"
cd "$REPO_DIR/dunit_build"

CFLAGS="-O2 -DSTBI_NO_SIMD -I$REPO_DIR/src -I$REPO_DIR/third_party -I$REPO_DIR/third_party/tinygl/include -DMCED_USE_GL=0"

echo "[1/5] Compiling TinyGL software rasterizer..."
for f in "$REPO_DIR"/third_party/tinygl/src/*.c; do
    obj="tgl_$(basename "$f" .c).o"
    "$CC" -c $CFLAGS "$f" -o "$obj"
done

echo "[2/5] Compiling MCED core engine..."
for f in assets blocks noise world render player ui settings entity audio_stub mced_dunit thread_pool; do
    "$CC" -c $CFLAGS "$REPO_DIR/src/$f.c" -o "$f.o"
done

echo "[3/5] Creating static archive libmced_core.a..."
ar rcs libmced_core.a *.o

echo "[4/5] Building Dunit SDK application..."
cd "$REPO_DIR/dunit_app"
$CARGO_DUNIT build

echo "[5/5] Packaging bootable Dunit OS ISO with MCED..."
OVERLAY_DIR="$REPO_DIR/dunit_build/overlay"
mkdir -p "$OVERLAY_DIR/app" \
         "$OVERLAY_DIR/system/share/dunit/apps/mced" \
         "$OVERLAY_DIR/system/share/dwm"

cp "$REPO_DIR/dunit_app/target/x86_64-unknown-none/release/mced" "$OVERLAY_DIR/app/mced"
chmod +x "$OVERLAY_DIR/app/mced"

cat <<'EOF' > "$OVERLAY_DIR/system/share/dunit/apps/mced/dunit.toml"
schema_version = 1

[application]
id = "mced"
name = "Minecraft C Edition"
kind = "gui"
binary = "mced"
icon = "/assets/icons/aqua/gui_demo.svg"
capabilities = []
EOF

cp "$DUNIT_DIR/assets/dwm/default.toml" "$OVERLAY_DIR/system/share/dwm/default.toml"
python3 -c '
path = "'"$OVERLAY_DIR"'/system/share/dwm/default.toml"
text = open(path).read()
if "[application.mced]" not in text:
    text = text.replace("[application.gui_settings]", "[application.mced]\nname = \"Minecraft C\"\nlabel = \"MC\"\n\n[application.gui_settings]")
    text = text.replace("entries = [\"gui_demo\",", "entries = [\"mced\", \"gui_demo\",")
    text = text.replace("[startup]\nentries = []", "[startup]\nentries = [\"mced\"]")
    open(path, "w").write(text)
'

cat <<'EOF' > "$OVERLAY_DIR/system/share/dwm/session.toml"
[window.mced]
x = 0
y = 0
w = 1024
h = 768
maximized = true
EOF

ISO_PATH="$REPO_DIR/dunit_build/mced.iso"
make -C "$DUNIT_DIR" iso \
    IMAGE_PROFILE=dwm \
    LIMINE_CONFIG=limine_dwm.conf \
    INITRD_OVERLAY="$OVERLAY_DIR" \
    ISO_PATH="$ISO_PATH" > /dev/null

echo ""
echo "=== Build Successful! ==="
echo "Application binary: $REPO_DIR/dunit_app/target/x86_64-unknown-none/release/mced"
echo "Bootable ISO:       $ISO_PATH"
echo ""

if [ "$1" = "run" ]; then
    echo "=== Running MCED on Dunit OS in QEMU GUI ==="
    QEMU_DISPLAY="${QEMU_DISPLAY:-gtk}"
    qemu-system-x86_64 \
        -enable-kvm -cpu host -machine q35,accel=kvm \
        -no-reboot -boot d \
        -cdrom "$ISO_PATH" \
        -m 512M \
        -serial stdio \
        -display "$QEMU_DISPLAY" \
        -vga std -global VGA.vgamem_mb=32 \
        -boot menu=on "${@:2}"
elif [ "$1" = "test" ]; then
    echo "=== Running MCED SDK Unit Tests in QEMU ==="
    $CARGO_DUNIT test --timeout 60
fi
