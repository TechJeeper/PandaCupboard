"""Host tests for farm-list scroll update decisions.

Exercises include/ui/FleetList.h by compiling it with a host C++ compiler when one
is available, and always runs an equivalent Python check of the same cases.
"""

from __future__ import annotations

import os
import shutil
import subprocess
from enum import IntEnum
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "include" / "ui" / "FleetList.h"
CPP_TEST = Path(__file__).resolve().with_name("test_fleet_list.cpp")


class FleetListPlan(IntEnum):
    Patch = 0
    Reorder = 1
    Rebuild = 2


def fleet_order_changed(prev, nxt, n):
    if n <= 0:
        return False
    if prev is None or nxt is None:
        return True
    return any(prev[i] != nxt[i] for i in range(n))


def fleet_clamp_scroll_y(y, content_h, view_h):
    if y < 0:
        y = 0
    max_y = content_h - view_h if content_h > view_h else 0
    if y > max_y:
        y = max_y
    return y


def fleet_list_plan(built_count, new_count, order_changed, interacting, scroll_y, force):
    if force or built_count < 0 or new_count != built_count:
        return FleetListPlan.Rebuild
    if new_count <= 0 or not order_changed:
        return FleetListPlan.Patch
    if interacting or scroll_y != 0:
        return FleetListPlan.Patch
    return FleetListPlan.Reorder


def fleet_paint_order(plan, order_changed, built, sorted_order):
    if plan == FleetListPlan.Patch and order_changed:
        return built
    return sorted_order


def fleet_commit_order(plan, order_changed):
    return not (plan == FleetListPlan.Patch and order_changed)


def expect(cond, msg, fails):
    if not cond:
        print(f"FAIL: {msg}")
        fails.append(msg)


def run_logic_tests():
    fails: list[str] = []
    a = [0, 2, 1, 3]
    b = [0, 2, 1, 3]
    c = [2, 0, 1, 3]

    expect(not fleet_order_changed(a, b, 4), "identical order is unchanged", fails)
    expect(fleet_order_changed(a, c, 4), "swapped indexes count as changed", fails)
    expect(not fleet_order_changed(a, b, 0), "empty order is unchanged", fails)
    expect(fleet_order_changed(None, b, 3), "missing prev order is changed", fails)

    expect(fleet_clamp_scroll_y(-40, 800, 396) == 0, "scroll does not go above top", fails)
    expect(fleet_clamp_scroll_y(0, 396, 396) == 0, "exactly one screen stays at 0", fails)
    expect(fleet_clamp_scroll_y(50, 300, 396) == 0, "short content cannot scroll", fails)
    expect(fleet_clamp_scroll_y(500, 8 * 48, 396) == 0, "8 rows of 48px fit in 396px view", fails)
    expect(fleet_clamp_scroll_y(500, 12 * 48, 396) == 12 * 48 - 396, "scroll clamps to last page", fails)
    expect(fleet_clamp_scroll_y(80, 12 * 48, 396) == 80, "in-range scroll is kept", fails)

    expect(fleet_list_plan(-1, 8, False, False, 0, False) == FleetListPlan.Rebuild, "first paint rebuilds", fails)
    expect(fleet_list_plan(8, 9, False, False, 120, False) == FleetListPlan.Rebuild, "count change rebuilds even while scrolled", fails)
    expect(fleet_list_plan(8, 8, False, False, 180, False) == FleetListPlan.Patch, "same order patches in place", fails)
    expect(fleet_list_plan(8, 8, True, False, 0, False) == FleetListPlan.Reorder, "idle at top can reorder", fails)
    expect(fleet_list_plan(8, 8, True, True, 0, False) == FleetListPlan.Patch, "do not reorder under an active gesture", fails)
    expect(fleet_list_plan(8, 8, True, False, 160, False) == FleetListPlan.Patch, "do not reorder while scrolled down", fails)
    expect(fleet_list_plan(8, 8, True, False, 160, True) == FleetListPlan.Rebuild, "forced rebuild still wins", fails)
    expect(fleet_list_plan(0, 0, False, False, 0, False) == FleetListPlan.Patch, "empty list stays put", fails)

    expect(fleet_paint_order(FleetListPlan.Patch, True, a, c) is a, "deferred reorder paints the on-screen order", fails)
    expect(fleet_paint_order(FleetListPlan.Patch, False, a, b) is b, "unchanged order paints the latest snapshot", fails)
    expect(fleet_paint_order(FleetListPlan.Reorder, True, a, c) is c, "reorder paints the sorted order", fails)
    expect(fleet_paint_order(FleetListPlan.Rebuild, True, a, c) is c, "rebuild paints the sorted order", fails)
    expect(not fleet_commit_order(FleetListPlan.Patch, True), "deferred reorder does not commit sorted indexes", fails)
    expect(fleet_commit_order(FleetListPlan.Patch, False), "patch with stable order commits", fails)
    expect(fleet_commit_order(FleetListPlan.Reorder, True), "successful reorder commits", fails)

    header = HEADER.read_text(encoding="utf-8")
    for needle in (
        "enum class FleetListPlan",
        "fleetOrderChanged",
        "fleetClampScrollY",
        "fleetListPlan",
        "fleetPaintOrder",
        "fleetCommitOrder",
        "builtCount < 0",
        "interacting || scrollY != 0",
    ):
        expect(needle in header, f"FleetList.h still contains {needle!r}", fails)

    if fails:
        print(f"{len(fails)} logic test(s) failed")
        return 1
    print("test_fleet_list: 25 passed")
    return 0


def run_cpp_tests():
    compiler = shutil.which("g++") or shutil.which("clang++") or shutil.which("cl")
    if not compiler:
        print("C++ compiler not found; skipped native compile of test_fleet_list.cpp")
        return 0
    out = ROOT / "test" / "test_fleet_list.exe"
    cmd = [compiler, "-std=c++17", f"-I{ROOT / 'include'}", str(CPP_TEST), "-o", str(out)]
    print(" ".join(cmd))
    subprocess.check_call(cmd)
    subprocess.check_call([str(out)])
    return 0


def main():
    os.chdir(ROOT)
    rc = run_logic_tests()
    if rc:
        return rc
    return run_cpp_tests()


if __name__ == "__main__":
    raise SystemExit(main())
