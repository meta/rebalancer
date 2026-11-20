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
# CIBW_BEFORE_ALL_LINUX hook for the wheels workflow.
#
# Runs inside the manylinux_2_28_x86_64 container that cibuildwheel
# spawned for the matrix entry. Installs LLVM clang + lld, runs getdeps
# to produce librebalancer's transitive deps, and writes
# .cmake_prefix_path for the wheel-build cmake invocation to read via
# CMakeLists.txt (cibuildwheel's bashlex env-evaluator can't handle the
# redirects/||/&& we'd need to do this through CIBW_ENVIRONMENT).
set -euo pipefail

# Use one of cibuildwheel's bundled cpythons (>=3.9) explicitly --
# /usr/bin/python3 in manylinux_2_28 is 3.6, which lacks PEP 585
# subscripted generics that getdeps' envfuncs.py uses.
export PY=/opt/python/cp310-cp310/bin/python

# AlmaLinux 8's llvm-toolset:rhel8 module currently ships clang 20.
# There's no prebuilt clang19 on this distro and LLVM upstream stopped
# publishing rhel-8 x86_64 binaries for the 19.x series. clang 20 is
# forward-compatible with the project's "clang19+" requirement.
dnf install -y clang lld gdb chrpath
clang_dir=$(dirname "$(command -v clang)")
ln -sf "$clang_dir/clang"   "$clang_dir/clang-19"
ln -sf "$clang_dir/clang++" "$clang_dir/clang++-19"
clang --version | head -1
ld.lld --version

bash tools/use_shared_glog.sh

$PY build/fbcode_builder/getdeps.py --allow-system-packages \
    install-system-deps --recursive rebalancer
$PY build/fbcode_builder/getdeps.py --allow-system-packages \
    install-system-deps --recursive patchelf

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

$PY build/fbcode_builder/getdeps.py --allow-system-packages \
    build --build-type RelWithDebInfo --src-dir=. --no-tests rebalancer \
    --extra-cmake-defines \
    '{"CMAKE_POSITION_INDEPENDENT_CODE":"ON"}'

# Build a CMAKE_PREFIX_PATH for the wheel-build's cmake invocation so
# find_package(Boost), find_package(folly), etc. find the getdeps-
# installed deps. Use the actual install root contents (`installed/*`)
# rather than `getdeps query-paths`: the latter recomputes manifest
# hashes from scratch and can disagree with the cached install paths
# if env vars differ between phases.
ls -d /tmp/fbcode_builder_getdeps-*/installed/* 2>/dev/null \
    | tr '\n' ':' > .cmake_prefix_path
echo "wrote CMAKE_PREFIX_PATH:"
tr ':' '\n' < .cmake_prefix_path
