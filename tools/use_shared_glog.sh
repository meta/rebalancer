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
# getdeps builds glog as a static library unless a consumer asks for its
# shared_libs feature. Static, it is linked into both libfolly.so and
# librebalancer.so (and the test binary, examples or Python extension), and the
# copies register glog's flags twice, which aborts at startup with "something
# wrong with flag". Ask for a shared glog in this checkout's copies of the
# rebalancer and rebalancer-explorer manifests before getdeps runs. The
# manifests themselves live in the shared fbcode_builder directory, which
# every getdeps project exports, so they are patched here rather than edited
# upstream.
set -euo pipefail

manifests="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/build/fbcode_builder/manifests"

for name in rebalancer rebalancer-explorer; do
  manifest="$manifests/$name"
  if grep -q '^glog = shared_libs$' "$manifest"; then
    continue
  fi
  if grep -q '^glog$' "$manifest"; then
    sed -i 's/^glog$/glog = shared_libs/' "$manifest"
  else
    sed -i 's/^folly = shared_libs$/folly = shared_libs\nglog = shared_libs/' "$manifest"
  fi
  grep -q '^glog = shared_libs$' "$manifest" || {
    echo "Failed to request a shared glog in $manifest" >&2
    exit 1
  }
done
