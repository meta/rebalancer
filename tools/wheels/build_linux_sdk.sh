#!/usr/bin/env bash
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Runs inside quay.io/pypa/manylinux_2_28_x86_64 (via Docker in the linux-sdk
# GHA job). Builds librebalancer and all deps as shared libs via getdeps, then
# copies the install tree to /project/_artifacts/linux/ for upload.
#
# Mirrors before_all_linux.sh but targets the full SDK install (lib + headers
# + binaries) rather than the wheel-only path.
set -euo pipefail

# /usr/bin/python3 in manylinux_2_28 is 3.6 which lacks PEP 585 generics
# used by getdeps. Use a bundled cpython instead.
export PY=/opt/python/cp310-cp310/bin/python

# AlmaLinux 8 ships clang 20 via llvm-toolset. No prebuilt clang19 for
# rhel8 x86_64 exists; clang 20 is forward-compatible with our "clang19+"
# requirement.
dnf install -y clang lld gcc-c++
clang_dir=$(dirname "$(command -v clang)")
ln -sf "$clang_dir/clang"   "$clang_dir/clang-19"
ln -sf "$clang_dir/clang++" "$clang_dir/clang++-19"
clang --version | head -1
ld.lld --version

bash tools/pin_dep_revs.sh
bash tools/use_shared_glog.sh

$PY build/fbcode_builder/getdeps.py --allow-system-packages \
    install-system-deps --recursive rebalancer

# getdeps satisfies boost on EL 8 with EPEL's boost169 packages, which ship no
# CMake package config. folly finds Boost only through that config, so it
# fetches its own Boost instead and turns its install rules off, failing the
# folly install. Remove boost169 so getdeps builds Boost from source, as it
# does on Ubuntu.
dnf remove -y 'boost169*'
# The container's CMake 4 rejects Boost 1.83's libs/predef, which still asks
# for cmake_minimum_required below 3.5. folly sets the same floor when it
# builds Boost itself.
export CMAKE_POLICY_VERSION_MINIMUM=3.10

# Build rebalancer (non-SKBUILD path). All deps (folly, fbthrift, fmt,
# glog, gflags) are now shared libs (BUILD_SHARED_LIBS=ON in their manifests).
# CMakeLists.txt installs:
#   lib/librebalancer.so
#   lib/libfolly.so libfmt.so libglog.so libgflags.so libfbthrift*.so ...
#   include/algopt/**/*.h  (source + thrift-generated headers)
# PACKAGING_TEST=ON is now in the getdeps manifest [cmake.defines] so it is
# part of the cache key; --extra-cmake-defines was excluded from the key and
# caused getdeps to reuse a stale cached build that omitted test_solve.
$PY build/fbcode_builder/getdeps.py --allow-system-packages \
    build --build-type RelWithDebInfo --src-dir=. --no-tests rebalancer \
    --project-install-prefix rebalancer:/usr/local

# Copy the installed tree (lib/, bin/) to the output directory.
# fixup-dyn-deps copies all shared libs, strips debug symbols, and rewrites
# absolute-path DT_NEEDED entries to point at the final install prefix.
#
# Stash test_solve before fixup-dyn-deps: fixup-dyn-deps runs patchelf on
# every binary it finds, calling it once per DT_NEEDED entry. Multiple
# patchelf invocations + strip on the same executable corrupts the ELF
# program header table offset, making the binary unparseable. We restore
# the pristine getdeps-built binary afterwards and patch it ourselves.
REBALANCER_PREFIX=$(ls -d /tmp/fbcode_builder_getdeps-*/installed/rebalancer 2>/dev/null | head -1)
TEST_SOLVE_SRC="$REBALANCER_PREFIX/usr/local/bin/test_solve"

# PACKAGING_TEST=ON in the manifest causes cmake to build and install
# test_solve. If getdeps hits a stale cache that predates PACKAGING_TEST,
# test_solve will be absent. In that case, force cmake to re-run configure
# by patching the cmake cache and deleting build.ninja (which forces cmake
# to regenerate), then build just the test_solve target.
if [[ ! -f "$TEST_SOLVE_SRC" ]]; then
    echo "test_solve absent from getdeps install; forcing cmake rebuild"
    CMAKE_BUILD_DIR=$(ls -d /tmp/fbcode_builder_getdeps-*/build/rebalancer 2>/dev/null | head -1)
    if [[ -z "$CMAKE_BUILD_DIR" || ! -d "$CMAKE_BUILD_DIR" ]]; then
        echo "ERROR: cmake build dir not found under /tmp/fbcode_builder_getdeps-*/build/rebalancer"
        exit 1
    fi
    # Ensure PACKAGING_TEST=ON in the cmake cache
    if grep -q "^PACKAGING_TEST" "$CMAKE_BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
        sed -i 's/^PACKAGING_TEST:.*/PACKAGING_TEST:BOOL=ON/' "$CMAKE_BUILD_DIR/CMakeCache.txt"
    else
        echo "PACKAGING_TEST:BOOL=ON" >> "$CMAKE_BUILD_DIR/CMakeCache.txt"
    fi
    # Delete build.ninja then re-run cmake configure (which re-reads
    # CMakeLists.txt with PACKAGING_TEST=ON from the cache and adds
    # test_solve), then build the target.
    rm -f "$CMAKE_BUILD_DIR/build.ninja"
    cmake -B "$CMAKE_BUILD_DIR"
    cmake --build "$CMAKE_BUILD_DIR" --target test_solve --parallel "$(nproc)"
    mkdir -p "$(dirname "$TEST_SOLVE_SRC")"
    BUILT_BIN=""
    if [[ -f "$CMAKE_BUILD_DIR/test_solve" ]]; then
        BUILT_BIN="$CMAKE_BUILD_DIR/test_solve"
    elif [[ -f "$CMAKE_BUILD_DIR/bin/test_solve" ]]; then
        BUILT_BIN="$CMAKE_BUILD_DIR/bin/test_solve"
    fi
    if [[ -n "$BUILT_BIN" ]]; then
        cp "$BUILT_BIN" "$TEST_SOLVE_SRC"
        chmod +x "$TEST_SOLVE_SRC"
        echo "Fallback: built test_solve via cmake → $TEST_SOLVE_SRC"
    else
        echo "ERROR: cmake built but test_solve binary not found in $CMAKE_BUILD_DIR"
        exit 1
    fi
fi
echo "test_solve present at $TEST_SOLVE_SRC"

cp "$TEST_SOLVE_SRC" /tmp/test_solve_pristine
echo "Stashed pristine test_solve"

$PY build/fbcode_builder/getdeps.py --allow-system-packages \
    fixup-dyn-deps --strip --src-dir=. rebalancer /project/_artifacts/linux \
    --project-install-prefix rebalancer:/usr/local \
    --final-install-prefix /usr/local

# Restore pristine test_solve (fixup-dyn-deps corrupts it via repeated patchelf)
if [[ -f /tmp/test_solve_pristine ]]; then
    cp /tmp/test_solve_pristine /project/_artifacts/linux/bin/test_solve
    chmod +x /project/_artifacts/linux/bin/test_solve
    echo "Restored pristine test_solve"
fi

# Copy headers from the cmake install prefix into the artifact.
# --project-install-prefix rebalancer:/usr/local causes getdeps to nest the
# cmake install under $PREFIX/usr/local/, so headers land at:
#   $PREFIX/usr/local/include/algopt/**/*.h
# fixup-dyn-deps maps $PREFIX/usr/local/ → _artifacts/linux/ for lib/ and
# bin/ but skips include/ entirely. Copy it explicitly.
INCLUDE_SRC="$REBALANCER_PREFIX/usr/local/include"
if [[ -d "$INCLUDE_SRC" ]]; then
    cp -r "$INCLUDE_SRC" /project/_artifacts/linux/
    echo "Copied headers from $INCLUDE_SRC"
else
    echo "WARNING: no usr/local/include/ found in $REBALANCER_PREFIX"
fi

# fixup-dyn-deps rewrites DT_NEEDED entries to absolute paths like
# /usr/local/lib/libfolly.so. This works after a deb install (the file IS
# there), but fails in the rpm extracted-tree test (the prefix is a tmpdir).
# Fix: for every ELF in lib/, replace absolute DT_NEEDED paths with just
# the SONAME, then set a $ORIGIN-relative rpath so each lib finds its
# siblings regardless of installation prefix.
#
# Ordering: patchelf BEFORE bundle_system_deps.py so that ldd (run by the
# bundler) resolves shared deps via $ORIGIN and reports them as "found" —
# preventing the bundler from duplicating them in lib/rebalancer/.
echo "=== Patching absolute DT_NEEDED → SONAME and setting \$ORIGIN rpaths ==="
for f in /project/_artifacts/linux/lib/*.so*; do
    [[ -f "$f" && ! -L "$f" ]] || continue
    # Replace any absolute-path DT_NEEDED entries with just the filename.
    while IFS= read -r needed; do
        if [[ "$needed" == */* ]]; then
            patchelf --replace-needed "$needed" "$(basename "$needed")" "$f"
            echo "  $f: $needed → $(basename "$needed")"
        fi
    done < <(patchelf --print-needed "$f" 2>/dev/null || true)
    # $ORIGIN   = this lib's own directory (/usr/local/lib or extracted tmpdir)
    # $ORIGIN/rebalancer = bundled system deps subdir
    patchelf --set-rpath '$ORIGIN:$ORIGIN/rebalancer' "$f"
done

# Fix test_solve: same absolute DT_NEEDED issue, needs rpath pointing up to lib/.
# Use fix_elf.py (in-place .dynstr patching) instead of patchelf --replace-needed
# and --set-rpath: patchelf calls rewriteSectionsExecutable which asserts on
# aarch64's 64KB pages. fix_elf.py overwrites strings in-place — no section
# movement, no page-alignment math.
TEST_SOLVE=/project/_artifacts/linux/bin/test_solve
if [[ -f "$TEST_SOLVE" ]]; then
    while IFS= read -r needed; do
        if [[ "$needed" == */* ]]; then
            $PY /project/tools/wheels/fix_elf.py replace-needed "$needed" "$(basename "$needed")" "$TEST_SOLVE"
        fi
    done < <($PY /project/tools/wheels/fix_elf.py print-needed "$TEST_SOLVE" 2>/dev/null || true)
    $PY /project/tools/wheels/fix_elf.py set-rpath '$ORIGIN/../lib:$ORIGIN/../lib/rebalancer' "$TEST_SOLVE"
    echo "test_solve rpath: $($PY /project/tools/wheels/fix_elf.py print-rpath "$TEST_SOLVE")"
    echo "test_solve needed: $($PY /project/tools/wheels/fix_elf.py print-needed "$TEST_SOLVE")"
else
    echo "WARNING: test_solve not found at $TEST_SOLVE"
fi

# bundle_system_deps.py walks the artifact's shared libs and bundles any
# transitive deps that aren't already in the artifact lib dir.
#
# Two classes of missing dep:
#   1. True system libs (libstdc++, libssl, etc.) — ldd finds them in system
#      paths; bundler copies them to lib/rebalancer/.
#   2. Getdeps-built libs that fixup-dyn-deps didn't copy (libglog, libgflags,
#      libfmt, etc.) — these are transitive deps of libfolly.so but not direct
#      DT_NEEDED of librebalancer.so, so fixup-dyn-deps never walked them.
#      After the $ORIGIN rpath pass above, ldd reports them as "not found"
#      (they're not in lib/ or lib/rebalancer/). Adding the getdeps install
#      prefix to LD_LIBRARY_PATH lets ldd resolve them; the bundler then
#      copies them to lib/rebalancer/ where ldconfig will register them.
GETDEPS_LIBDIRS=""
for d in /tmp/fbcode_builder_getdeps-*/installed/*/lib \
          /tmp/fbcode_builder_getdeps-*/installed/*/lib64; do
    [ -d "$d" ] && GETDEPS_LIBDIRS="${GETDEPS_LIBDIRS}:${d}"
done
cd /project
LD_LIBRARY_PATH="${GETDEPS_LIBDIRS#:}" $PY /project/tools/packages/bundle_system_deps.py
