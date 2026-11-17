#!/usr/bin/env python3
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

"""Balance tasks subject to a migration budget using the native Python package.

Run each measurement in a fresh process to isolate peak RSS, for example:
    python tools/benchmarks/group_move_limit.py --mode dynamic
    python tools/benchmarks/group_move_limit.py --mode sparse
    python tools/benchmarks/group_move_limit.py --mode dense --objects 10000

Input construction, binding calls, materialization, and search are timed.
Result validation is excluded from timing. The dense case rotates equal-sized sets of
destination overrides between task classes; different rows have the same
cost multiset. Repeat with identical arguments and builds for comparisons.
"""

import argparse
import hashlib
import json
import resource
import sys
import time

from rebalancer import ProblemSolver


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--objects", type=int, default=100_000)
    parser.add_argument("--hosts", type=int, default=1_000)
    parser.add_argument("--moves", type=int, default=1_000)
    parser.add_argument(
        "--mode", choices=("static", "dynamic", "sparse", "dense"), default="dynamic"
    )
    parser.add_argument(
        "--density",
        type=int,
        default=50,
        help="Percent of hosts with overrides per task in dense mode",
    )
    args = parser.parse_args()
    n, hosts, moves = args.objects, args.hosts, args.moves
    if n <= 0 or hosts < 2 or n % hosts or not 0 <= args.density <= 100:
        parser.error("Use positive objects divisible by hosts >= 2 and density 0..100")
    if not 0 <= moves <= (n // hosts) * (hosts - 1):
        parser.error("Moves must fit on the hosts other than host 0")

    start = time.perf_counter()
    solver = ProblemSolver(service_name="benchmark", service_scope="local")
    solver.set_log_level("ERR")
    solver.set_run_id("group-move-limit-benchmark")
    solver.set_object_name("task").set_container_name("host")
    assignment = {f"h{i}": [] for i in range(hosts)}
    objects = [f"t{i}" for i in range(n)]
    for i, obj in enumerate(objects):
        assignment[f"h{i % hosts}"].append(obj)
    for i in range(moves):
        assignment["h0"].append(assignment[f"h{1 + i % (hosts - 1)}"].pop())
    solver.set_assignment(assignment)
    solver.add_partition("service", {"all": objects})
    solver.add_object_dimension("cpu", {}, 1.0)
    override_hosts = hosts * args.density // 100

    def cost(obj, host):
        if args.mode == "sparse":
            return 2.0 if obj < min(n, 100) and host < min(hosts, 100) else 1.0
        if args.mode == "dense":
            return 2.0 if (host - obj // hosts) % hosts < override_hosts else 1.0
        return 1.0

    if args.mode == "static":
        solver.add_object_dimension("move_cost", {}, 1.0)
    else:
        values = {}
        if args.mode == "sparse":
            values = {
                f"h{i}": {objects[j]: 2.0 for j in range(min(n, 100))}
                for i in range(min(hosts, 100))
            }
        elif args.mode == "dense":
            values = {
                f"h{i}": {
                    obj: 2.0 for j, obj in enumerate(objects) if cost(j, i) == 2.0
                }
                for i in range(hosts)
            }
        solver.add_dynamic_object_dimension("move_cost", "host", values, 1.0)
        del values
    budget = float(moves * 2)
    solver.add_constraint(
        {
            "groupMoveLimitSpec": {
                "name": "migration_budget",
                "partitionName": "service",
                "dimension": "move_cost",
                "limit": {"type": 1, "globalLimit": budget},
            }
        }
    )
    solver.add_goal(
        {"balanceSpec": {"name": "cpu_balance", "scope": "host", "dimension": "cpu"}}
    )
    solver.add_solver(
        {
            "localSearchSolverSpec": {
                "randomSeed": 1,
                "solveTime": 30,
                "moveTypeList": [{"singleMoveTypeSpec": {}}],
            }
        }
    )
    built = time.perf_counter()
    result = solver.solve()
    finished = time.perf_counter()

    final = result["assignment"]
    original = {obj: host for host, group in assignment.items() for obj in group}
    counts = dict.fromkeys(assignment, 0)
    weighted_moves = 0.0
    actual_moves = 0
    assert set(final) == set(objects), "Missing or unexpected tasks"
    for obj, host in final.items():
        counts[host] += 1
        if original[obj] != host:
            actual_moves += 1
            weighted_moves += max(
                cost(int(obj[1:]), int(original[obj][1:])),
                cost(int(obj[1:]), int(host[1:])),
            )
    assert min(counts.values()) == max(counts.values()), "Uneven final placement"
    assert weighted_moves <= budget, "Migration budget exceeded"
    assert result["finalConstraint"]["brokenCount"] == 0, result["finalConstraint"]
    assert result["finalObjective"]["value"] == 0, result["finalObjective"]
    rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    print(
        json.dumps(
            {
                **vars(args),
                "setup_s": built - start,
                "solve_s": finished - built,
                "total_s": finished - start,
                "materialization_s": result["problemProfile"]["materializationSec"],
                "peak_rss_mib": rss
                / (1024 * 1024 if sys.platform == "darwin" else 1024),
                "actual_moves": actual_moves,
                "weighted_moves": weighted_moves,
                "tasks_per_host": min(counts.values()),
                "assignment_sha256": hashlib.sha256(
                    json.dumps(final, sort_keys=True).encode()
                ).hexdigest(),
                "final_objective": result["finalObjective"],
                "final_constraint": result["finalConstraint"],
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
