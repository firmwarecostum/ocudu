#!/bin/bash

# SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
# SPDX-License-Identifier: BSD-3-Clause-Open-MPI


set -e # stop executing after error

# UHD headers use uint32_t / uint8_t / … without #include <cstdint>. GCC 15+ (e.g. Fedora
# with GCC 16) no longer exposes those typedefs via transitive includes.
# $1: extracted UHD tree root (e.g. /tmp/uhd-4.8.0.0)
uhd_apply_missing_cstdint_includes() {
    local uhd_root="${1:?}"
    local -a fixes=(
        'host/include/uhd/features/ref_clk_calibration_iface.hpp|^#include <memory>$'
        'host/lib/include/uhdlib/usrp/dboard/fbx/fbx_constants.hpp|^#include <vector>$'
    )
    local fix rel pattern path
    for fix in "${fixes[@]}"; do
        rel="${fix%%|*}"
        pattern="${fix#*|}"
        path="${uhd_root}/${rel}"
        [[ -f "${path}" ]] || continue
        grep -qE '\<(uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t)\>' "${path}" || continue
        grep -qF '#include <cstdint>' "${path}" && continue
        sed -i "/${pattern}/a#include <cstdint>" "${path}"
    done
}

main() {
    # Check number of args
    if [ $# -lt 1 ] || [ $# -gt 3 ]; then
        echo >&2 "Illegal number of parameters"
        echo >&2 "Run like this: \"./build_uhd.sh <uhd_version> [<arch> [<ncores>]]\" where arch is any gcc/clang compatible -march and ncores could be any number or empty for all"
        exit 1
    fi

    local uhd_version=$1
    local arch="${2:--march=native}"
    local ncores="${3:-$(nproc)}"

    cd /tmp
    curl -L "https://github.com/EttusResearch/uhd/archive/refs/tags/v${uhd_version}.tar.gz" | tar xzf -

    local uhd_root="/tmp/uhd-${uhd_version}"
    uhd_apply_missing_cstdint_includes "${uhd_root}"

    cd "${uhd_root}/host" && mkdir -p build && cd build
    # CMake 4.0+ rejects cmake_minimum_required(VERSION <3.5) in UHD's bundled lib/rc (CMakeRC.cmake).
    # Harmless on older CMake (unused cache entry).
    cmake \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_INSTALL_PREFIX=/opt/uhd/"${uhd_version}" \
        -DENABLE_LIBUHD=On \
        -DENABLE_PYTHON_API=Off \
        -DENABLE_EXAMPLES=Off \
        -DENABLE_TESTS=Off \
        -DCMAKE_CXX_FLAGS="${arch}" ..
    cmake --build . -- -j"${ncores}"
    cmake --install .

    rm -Rf /tmp/uhd*"${uhd_version}"

}

main "$@"
