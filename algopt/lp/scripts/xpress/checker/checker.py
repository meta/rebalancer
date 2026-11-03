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

# pyre-strict
import os
import platform
import re
import subprocess
import sys
from collections import Counter, defaultdict
from datetime import date, datetime
from glob import glob
from os import listdir
from os.path import basename, isdir, join

import numpy as np
import xpress as xp
import xxhash

month = {
    "jan": 1,
    "feb": 2,
    "mar": 3,
    "apr": 4,
    "may": 5,
    "jun": 6,
    "jul": 7,
    "aug": 8,
    "sep": 9,
    "oct": 10,
    "nov": 11,
    "dec": 12,
}

home_dir = str(os.environ.get("HOME"))
fbsource_dir = "fbsource"
tp2_dir: str = join(fbsource_dir, "third-party", "tp2")
code_dir: str = join(fbsource_dir, "fbcode")
base_dir: str = join(home_dir, tp2_dir)
platform_file: str = join(home_dir, code_dir, "third-party2", "platform010.py")
library_base = "libxprs"


class Package:
    def __init__(self, package_name: str) -> None:
        self.name = package_name
        self.licenses: dict[str, list[dict[str, str]]] = {}
        self.shared_libs: dict[str, list[str]] = {}
        self.static_libs: dict[str, list[str]] = {}

    def read_license_data(self, license_files: list[str]) -> dict[str, str]:
        licenses = {}
        for file in license_files:
            with open(file, "rb") as f:
                content = f.read()
                my_hash = xxhash.xxh3_64(content).hexdigest()
                content = content.replace(b"\r", b"")
                content = content.replace(b"\n", b"")
                content = content.replace(b"\\", b"")
                content = content.replace(b"\t", b" ")
                content = re.sub(" +", " ", str(content))
                licenses[my_hash] = content
        return licenses

    def parse_license_lines(self, lines: list[str]) -> dict[str, str]:
        parts = {}
        for line in lines:
            line = line.strip().replace('"', "")
            kv = line.split("=", maxsplit=1)
            k = kv[0].strip().replace("license ", "")
            v = kv[1].strip()
            if (
                k == "terminal-services sig"
                or k == "hostid"
                or k == "platform"
                or k == "static"
            ):
                continue
            if k == "b'expiry":
                k = "expiry"
            if k == "b'comment":
                k = "comment"
            if k == "expiry":
                f = v.split("-")
                d = date(int(f[2]), month[f[1]], int(f[0]))
                n = date.today()
                parts["expiry_days"] = (d - n).days
                k = "expiry_date"
                v = d.strftime("%Y/%m/%d")
            if k == "features":
                v = v.split(" ")
                if "Community" in v:
                    parts["commercial"] = False
                continue
            if k == "comment":
                commercial = " Facebook " in v
                parts["commercial"] = commercial
                continue
            if k == "release":
                k = "year"
            # pyrefly: ignore [unsupported-operation]
            parts[k] = v
        # pyrefly: ignore [bad-return]
        return parts

    def check_package_version(self, version: str) -> None:
        package_dir = join(base_dir, self.name)
        print(f"  checking version {version}")
        self.licenses[version] = []
        self.shared_libs[version] = []
        self.static_libs[version] = []
        version_dir = join(package_dir, version)
        license_files = glob(join(version_dir, "**", "*.xpr"), recursive=True)
        licenses = self.read_license_data(license_files)

        print(f"    found {len(licenses)} distinct licenses")
        for pos, data in enumerate(licenses.items()):
            license_hash, license = data
            lines = re.findall(r"[^=]+=\"[^\"]+\" *", license)
            parts = self.parse_license_lines(lines)
            days = int(parts["expiry_days"])
            if days < 0:
                print(
                    f"      #{pos + 1}: year: {parts['year']}, release: {parts['fico_xpress_release']} -- EXPIRED {-days} days ago"
                )
            else:
                print(
                    f"      #{pos + 1}: year: {parts['year']}, commercial: {'yes' if parts['commercial'] else 'no'}, release: {parts['fico_xpress_release']}, validity: {days} days"
                )
            self.licenses[version].append(parts)

        shared_libraries = glob(
            join(version_dir, "**", f"{library_base}.so.[0-9]*.[0-9]*.[0-9]*"),
            recursive=True,
        )
        print(f"    found {len(shared_libraries)} distinct shared library versions")
        for pos, library in enumerate(shared_libraries):
            name = basename(library)
            size = os.path.getsize(library) / 1024 / 1024
            print(f"      #{pos + 1}: {name} -- {size:.2f} MB")
            self.shared_libs[version].append(name)

        static_libraries = glob(
            join(version_dir, "**", f"{library_base}-static.a"), recursive=True
        )
        print(f"    found {len(static_libraries)} distinct static libraries")
        for pos, library in enumerate(static_libraries):
            name = basename(library)
            size = os.path.getsize(library) / 1024 / 1024
            print(f"      #{pos + 1}: {name} -- {size:.2f} MB")
            self.static_libs[version].append(name)

    def check_package_versions(self) -> None:
        package_dir = join(base_dir, self.name)
        versions = [d for d in listdir(package_dir) if isdir(join(package_dir, d))]
        print(
            f"Package {self.name} has {len(versions)} distinct versions available: {versions}"
        )
        for version in versions:
            self.check_package_version(version)

    def check_package(self) -> None:
        print("\n")
        print(f"Starting check for package {self.name} on {datetime.now()}")
        self.check_package_versions()
        print(f"Finishing check for package {self.name} on {datetime.now()}")


class Checker:
    def __init__(self) -> None:
        self.packages: list[Package] = []
        self.xp_version = ""
        self.xp_days_left = 0

    def check_package(self, package_name: str) -> None:
        package = Package(package_name)
        self.packages.append(package)
        package.check_package()

    def check_environment(self) -> None:
        print("\n")
        print(f"Starting environment check on {datetime.now()}")

        print(f"python version tuple: [{platform.python_version_tuple()[:2]}]")
        print(f"numpy version: [{np.version.version}]")
        self.xp_version = xp.getVersion()
        print(f"xpress version: [{self.xp_version}]")

        try:
            ndays = xp.getDaysLeft()
        except RuntimeError:
            print("This is not an evaluation license")
            ndays = 0
        else:
            print(f"This is an evaluation license which expires in {ndays} days")
        self.xp_days_left = ndays

        print("xpress banner:")
        print("--------------------------------------------------------------")
        print(xp.getBanner())
        print("--------------------------------------------------------------")

        print("\n")
        print(f"Local changes to [{platform_file}]:")
        print("--------------------------------------------------------------")
        subprocess.run(["hg", "diff", platform_file, "--pager", "never"])
        print("--------------------------------------------------------------")

        print(f"Ending environment check on {datetime.now()}")

    def guess_current_version(self) -> None:
        candidate_versions = Counter()
        candidate_licenses = {}
        for package in self.packages:
            for version in package.licenses:
                candidate_licenses[version] = False
                for license in package.licenses[version]:
                    if license["expiry_days"] == self.xp_days_left:
                        candidate_versions[version] += 1
                        candidate_licenses[version] = (
                            # pyrefly: ignore [unsupported-operation]
                            license[
                                "commercial"
                            ]  # pyrefly: ignore [unsupported-operation]
                            if "commercial" in license
                            else False  # pyrefly: ignore [unsupported-operation]
                        )
            for version in package.shared_libs:
                for name in package.shared_libs[version]:
                    if name == f"{library_base}.so.{self.xp_version}":
                        candidate_versions[version] += 1

        top_count = 0
        versions = defaultdict(list)
        for version, count in candidate_versions.items():
            versions[count].append(version)
            if top_count < count:
                top_count = count

        print("\n")
        print("==============================================================")
        if top_count <= 0:
            print("Could not guess a consistent version")
        elif len(versions[top_count]) == 1:
            version = versions[top_count][0]
            kind = "commercial" if candidate_licenses[version] else "community"
            print(
                f"You are definitely running guessed version [{version}] with a {kind} license which expires in {self.xp_days_left} days ({top_count} points)"
            )
        else:
            print(
                f"You are running one of these guessed versions: [{versions[top_count]}] ({top_count} points)"
            )
        print("==============================================================")


def main() -> None:
    checker = Checker()
    packages = sys.argv[1:]
    for package in packages:
        checker.check_package(package)

    checker.check_environment()
    checker.guess_current_version()


if __name__ == "__main__":
    main()
