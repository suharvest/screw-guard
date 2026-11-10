#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_DIR="${SCRIPT_DIR}/packages/screw-guard"
PKG_VERSION="${1:-1.0.0}"
OUTPUT_DEB="${SCRIPT_DIR}/packages/screw-guard_${PKG_VERSION}_riscv64.deb"
SOURCE_BIN="/home/harvest/project/microduck/screw-guard/device/build/screw-guard"
SDK_LIB_DIR="/home/harvest/recamera-dev/sg2002_recamera_emmc"

echo "[BUILD] Preparing packaging staging directory for ScrewGuard ${PKG_VERSION}..."
mkdir -p "${PACKAGE_DIR}/DEBIAN"
mkdir -p "${PACKAGE_DIR}/etc/init.d"
mkdir -p "${PACKAGE_DIR}/usr/local/bin"
mkdir -p "${PACKAGE_DIR}/usr/share/supervisor/apps"
mkdir -p "${PACKAGE_DIR}/usr/lib/screw-guard"

# 1. Copy and strip binary
echo "[BUILD] Stripping and copying binary..."
USER_ID="$(id -u)"
GROUP_ID="$(id -g)"
docker run --rm --platform linux/amd64 \
  -v /home/harvest/recamera-dev:/recamera-dev \
  -v "${SCRIPT_DIR}:/workspace" \
  -v /home/harvest/project:/project \
  recamera-build-base:amd64 \
  bash -c "
    export PATH=/recamera-dev/host-tools/gcc/riscv64-linux-musl-x86_64/bin:\$PATH
    riscv64-unknown-linux-musl-strip -o /workspace/packages/screw-guard/usr/local/bin/screw-guard /project/microduck/screw-guard/device/build/screw-guard
    chown ${USER_ID}:${GROUP_ID} /workspace/packages/screw-guard/usr/local/bin/screw-guard
    chmod 755 /workspace/packages/screw-guard/usr/local/bin/screw-guard
  "

# 2. Copy init script and supervisor json
cp -f /home/harvest/project/microduck/screw-guard/device/rootfs/etc/init.d/K95screw-guard "${PACKAGE_DIR}/etc/init.d/K95screw-guard"
cp -f /home/harvest/project/microduck/screw-guard/screw-guard.json "${PACKAGE_DIR}/usr/share/supervisor/apps/screw-guard.json"

# 3. Copy private runtime libraries
OPENCV_DIR="${SDK_LIB_DIR}/cvi_mpi/modules/isp/cv181x/isp-tool-daemon/isp_daemon_tool/install/lib"
cp -f "${OPENCV_DIR}/libopencv_core.so.4.5" "${PACKAGE_DIR}/usr/lib/screw-guard/"
cp -f "${OPENCV_DIR}/libopencv_imgproc.so.4.5" "${PACKAGE_DIR}/usr/lib/screw-guard/"
cp -f "${OPENCV_DIR}/libopencv_imgcodecs.so.4.5" "${PACKAGE_DIR}/usr/lib/screw-guard/"
cp -f "${SDK_LIB_DIR}/cvi_mpi/lib/libsns_gc2053.so" "${PACKAGE_DIR}/usr/lib/screw-guard/"

# 4. Set permissions
chmod 755 "${PACKAGE_DIR}/DEBIAN/postinst" "${PACKAGE_DIR}/DEBIAN/prerm" "${PACKAGE_DIR}/DEBIAN/postrm"
chmod 644 "${PACKAGE_DIR}/DEBIAN/control"
chmod 755 "${PACKAGE_DIR}/etc/init.d/K95screw-guard"
chmod 755 "${PACKAGE_DIR}/usr/local/bin/screw-guard"
chmod 644 "${PACKAGE_DIR}/usr/share/supervisor/apps/screw-guard.json"
chmod 755 "${PACKAGE_DIR}/usr/lib/screw-guard/"*.so*

# 5. Build debian package
echo "[PACKAGE] Packaging Debian package: ${OUTPUT_DEB}..."
dpkg-deb -Zgzip --root-owner-group -b "${PACKAGE_DIR}" "${OUTPUT_DEB}"

# 6. Verify debian package
echo "[VERIFY] Verifying package contents:"
dpkg-deb -I "${OUTPUT_DEB}"
dpkg-deb -c "${OUTPUT_DEB}"
md5sum "${OUTPUT_DEB}"
sha256sum "${OUTPUT_DEB}"
echo "[SUCCESS] Package build complete: ${OUTPUT_DEB}"
