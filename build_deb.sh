#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_VERSION="${1:-1.0.0}"
RECAMERA_ROOT="${RECAMERA_ROOT:-/home/harvest/project/recamera/sscma-example-sg200x}"
SG200X_SDK_PATH="${SG200X_SDK_PATH:-/home/harvest/recamera-dev/sg2002_recamera_emmc}"
HOST_TOOLS_PATH="${HOST_TOOLS_PATH:-/home/harvest/project/recamera/host-tools}"

OUTPUT_DIR="${SCRIPT_DIR}/packages"
OUTPUT_DEB="${OUTPUT_DIR}/screw-guard_${PKG_VERSION}_riscv64.deb"

echo "=================================================="
echo " Building ScrewGuard Debian Package (${PKG_VERSION})"
echo " Standalone Out-of-Tree build"
echo " RECAMERA_ROOT: ${RECAMERA_ROOT}"
echo "=================================================="

mkdir -p "${OUTPUT_DIR}"

USER_ID="$(id -u)"
GROUP_ID="$(id -g)"

docker run --rm --platform linux/amd64 \
  -v "${RECAMERA_ROOT}/..:/recamera_ws" \
  -v "${SG200X_SDK_PATH}:/recamera_ws/sg2002_recamera_emmc" \
  -v "${SCRIPT_DIR}:/workspace" \
  recamera-build-base:amd64 bash -c "
    set -euo pipefail
    export PATH=/recamera_ws/host-tools/gcc/riscv64-linux-musl-x86_64/bin:\$PATH
    export SG200X_SDK_PATH=/recamera_ws/sg2002_recamera_emmc
    export RECAMERA_ROOT=/recamera_ws/sscma-example-sg200x

    cd /workspace
    if [ -f build/CMakeCache.txt ]; then
      if grep -q 'screw-guard' build/CMakeCache.txt; then
        rm -rf build
      fi
    fi
    mkdir -p build && cd build
    cmake .. -DCMAKE_TOOLCHAIN_FILE=\${RECAMERA_ROOT}/cmake/toolchain-riscv64-linux-musl-x86_64.cmake
    make -j\$(nproc)

    # Prepare staging directory
    STAGING_DIR='/workspace/build/pkg_staging'
    rm -rf \"\${STAGING_DIR}\"
    mkdir -p \"\${STAGING_DIR}/DEBIAN\"
    mkdir -p \"\${STAGING_DIR}/usr/local/bin\"
    mkdir -p \"\${STAGING_DIR}/usr/lib/screw-guard\"

    # Strip and install binary
    riscv64-unknown-linux-musl-strip -o \"\${STAGING_DIR}/usr/local/bin/screw-guard\" /workspace/build/screw-guard
    chmod 755 \"\${STAGING_DIR}/usr/local/bin/screw-guard\"

    # Control files
    cp -f /workspace/control/control \"\${STAGING_DIR}/DEBIAN/control\"
    cp -f /workspace/control/postinst \"\${STAGING_DIR}/DEBIAN/postinst\"
    cp -f /workspace/control/prerm \"\${STAGING_DIR}/DEBIAN/prerm\"
    cp -f /workspace/control/postrm \"\${STAGING_DIR}/DEBIAN/postrm\"
    chmod 755 \"\${STAGING_DIR}/DEBIAN/postinst\" \"\${STAGING_DIR}/DEBIAN/prerm\" \"\${STAGING_DIR}/DEBIAN/postrm\"
    chmod 644 \"\${STAGING_DIR}/DEBIAN/control\"

    # Copy rootfs overlay
    cp -rf /workspace/rootfs/* \"\${STAGING_DIR}/\"
    chmod 755 \"\${STAGING_DIR}/etc/init.d/K95screw-guard\"
    chmod 644 \"\${STAGING_DIR}/usr/share/supervisor/apps/screw-guard.json\"

    # Private runtime libraries
    OPENCV_DIR=\"\${SG200X_SDK_PATH}/cvi_mpi/modules/isp/cv181x/isp-tool-daemon/isp_daemon_tool/install/lib\"
    cp -f \"\${OPENCV_DIR}/libopencv_core.so.4.5\" \"\${STAGING_DIR}/usr/lib/screw-guard/\"
    cp -f \"\${OPENCV_DIR}/libopencv_imgproc.so.4.5\" \"\${STAGING_DIR}/usr/lib/screw-guard/\"
    cp -f \"\${OPENCV_DIR}/libopencv_imgcodecs.so.4.5\" \"\${STAGING_DIR}/usr/lib/screw-guard/\"
    cp -f \"\${SG200X_SDK_PATH}/cvi_mpi/lib/libsns_gc2053.so\" \"\${STAGING_DIR}/usr/lib/screw-guard/\"
    chmod 755 \"\${STAGING_DIR}/usr/lib/screw-guard/\"*.so*

    # Build debian package
    dpkg-deb -Zgzip --root-owner-group -b \"\${STAGING_DIR}\" \"/workspace/packages/screw-guard_${PKG_VERSION}_riscv64.deb\"

    # Fix host ownership
    chown -R ${USER_ID}:${GROUP_ID} /workspace/build /workspace/packages
  "

echo "=================================================="
echo " Package successfully built at:"
echo " ${OUTPUT_DEB}"
echo "=================================================="
dpkg-deb -I "${OUTPUT_DEB}"
dpkg-deb -c "${OUTPUT_DEB}"
md5sum "${OUTPUT_DEB}"
sha256sum "${OUTPUT_DEB}"
