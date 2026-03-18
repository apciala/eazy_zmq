#!/bin/bash

set -e

SYSROOT="${SYSROOT:-/home/hh/sysroots/wanglian}"
BUILD_DIR="${BUILD_DIR:-build_cross}"
TOOLCHAIN_TRIPLE="${TOOLCHAIN_TRIPLE:-aarch64-linux-gnu}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-/home/hh/gcc8-prebuilt/gcc-arm-8.3-2019.03-x86_64-aarch64-linux-gnu}"
CC="${CC:-${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_TRIPLE}-gcc}"
CXX="${CXX:-${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_TRIPLE}-g++}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"

if [ ! -x "${CC}" ] || [ ! -x "${CXX}" ]; then
    echo "ERROR: cross compiler not found: ${CC} / ${CXX}" >&2
    exit 1
fi

if [ ! -d "${SYSROOT}" ]; then
    echo "ERROR: SYSROOT does not exist: ${SYSROOT}" >&2
    exit 1
fi

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

export PKG_CONFIG_SYSROOT_DIR="${SYSROOT}"
export PKG_CONFIG_LIBDIR="${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${SYSROOT}/usr/local/lib/pkgconfig:${SYSROOT}/lib/pkgconfig:${SYSROOT}/lib/aarch64-linux-gnu/pkgconfig"
unset PKG_CONFIG_PATH

SYSROOT_B_FLAGS="-B${SYSROOT}/usr/lib/aarch64-linux-gnu/ -B${SYSROOT}/lib/aarch64-linux-gnu/"
RPATH_LINK="${SYSROOT}/lib:${SYSROOT}/lib/aarch64-linux-gnu:${SYSROOT}/usr/lib:${SYSROOT}/usr/lib/aarch64-linux-gnu:${SYSROOT}/usr/local/lib"

rm -rf "${ROOT_DIR}/${BUILD_DIR}"
mkdir -p "${ROOT_DIR}/${BUILD_DIR}"

cmake -S "${ROOT_DIR}" -B "${ROOT_DIR}/${BUILD_DIR}" \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER="${CC}" \
    -DCMAKE_CXX_COMPILER="${CXX}" \
    -DCMAKE_SYSROOT="${SYSROOT}" \
    -DCMAKE_C_FLAGS="--sysroot=${SYSROOT} ${SYSROOT_B_FLAGS}" \
    -DCMAKE_CXX_FLAGS="--sysroot=${SYSROOT} ${SYSROOT_B_FLAGS}" \
    -DCMAKE_FIND_ROOT_PATH="${SYSROOT}" \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
    -DCMAKE_PREFIX_PATH="${SYSROOT};${SYSROOT}/usr;${SYSROOT}/usr/local" \
    -DCMAKE_SKIP_RPATH=ON \
    -DCMAKE_EXE_LINKER_FLAGS="--sysroot=${SYSROOT} -Wl,-rpath-link,${RPATH_LINK}" \
    -DCMAKE_SHARED_LINKER_FLAGS="--sysroot=${SYSROOT} -Wl,-rpath-link,${RPATH_LINK}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

cmake --build "${ROOT_DIR}/${BUILD_DIR}" -j"$(nproc)"

echo ""
echo "Build complete: ${ROOT_DIR}/${BUILD_DIR}/bin/zmq_ipc_tester"
file "${ROOT_DIR}/${BUILD_DIR}/bin/zmq_ipc_tester"
ls -lh "${ROOT_DIR}/${BUILD_DIR}/bin/zmq_ipc_tester"
