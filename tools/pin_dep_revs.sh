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
# Temporary: pin folly and the projects built with it to the last revisions
# rebalancer's CI built cleanly (2026-11-18). folly 143a712c6a ("Make Boost
# optional in CMake and don't fetch it by default") compiles ProgramOptions,
# JSONSchema, RegexMatchCache and TestUtilPCRE without Boost's include
# directory, which fails every getdeps build of folly, folly's own CI included.
# getdeps reads these files instead of the ones the daily hash update writes.
# Delete this script and its callers once folly builds again.
set -euo pipefail

hashes="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/build/deps/github_hashes"

pin() {
  mkdir -p "$hashes/$(dirname "$1")"
  echo "Subproject commit $2" > "$hashes/$1-rev.txt"
}

pin facebook/folly f416c5d1cac01f8f3a59bce209a9ebb877da7307
pin facebook/fbthrift c643ad9ab33d0d4c0cd5c920beac92f7c9e53691
pin facebook/mvfst 1177c1a6579f1b215613792a382aa36647e3c60a
pin facebook/wangle 5b0cffa62175ae6149587071b38173060718dd06
pin facebookincubator/fizz 9c53f26bb419ddcc987ea3686fa8dc2d63238087
