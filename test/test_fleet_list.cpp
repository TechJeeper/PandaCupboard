#include "ui/FleetList.h"

#include <cstdio>

static int gFails = 0;

static void expect(bool cond, const char *msg) {
    if (cond) return;
    std::printf("FAIL: %s\n", msg);
    gFails += 1;
}

int main() {
    int a[] = {0, 2, 1, 3};
    int b[] = {0, 2, 1, 3};
    int c[] = {2, 0, 1, 3};

    expect(!fleetOrderChanged(a, b, 4), "identical order is unchanged");
    expect(fleetOrderChanged(a, c, 4), "swapped indexes count as changed");
    expect(!fleetOrderChanged(a, b, 0), "empty order is unchanged");
    expect(fleetOrderChanged(nullptr, b, 3), "missing prev order is changed");

    expect(fleetClampScrollY(-40, 800, 396) == 0, "scroll does not go above top");
    expect(fleetClampScrollY(0, 396, 396) == 0, "exactly one screen stays at 0");
    expect(fleetClampScrollY(50, 300, 396) == 0, "short content cannot scroll");
    expect(fleetClampScrollY(500, 8 * 48, 396) == 0, "8 rows of 48px fit in 396px view");
    expect(fleetClampScrollY(500, 12 * 48, 396) == 12 * 48 - 396, "scroll clamps to last page");
    expect(fleetClampScrollY(80, 12 * 48, 396) == 80, "in-range scroll is kept");

    expect(fleetListPlan(-1, 8, false, false, 0, false) == FleetListPlan::Rebuild,
           "first paint rebuilds");
    expect(fleetListPlan(8, 9, false, false, 120, false) == FleetListPlan::Rebuild,
           "count change rebuilds even while scrolled");
    expect(fleetListPlan(8, 8, false, false, 180, false) == FleetListPlan::Patch,
           "same order patches in place");
    expect(fleetListPlan(8, 8, true, false, 0, false) == FleetListPlan::Reorder,
           "idle at top can reorder");
    expect(fleetListPlan(8, 8, true, true, 0, false) == FleetListPlan::Patch,
           "do not reorder under an active gesture");
    expect(fleetListPlan(8, 8, true, false, 160, false) == FleetListPlan::Patch,
           "do not reorder while scrolled down");
    expect(fleetListPlan(8, 8, true, false, 160, true) == FleetListPlan::Rebuild,
           "forced rebuild still wins");
    expect(fleetListPlan(0, 0, false, false, 0, false) == FleetListPlan::Patch,
           "empty list stays put");

    expect(fleetPaintOrder(FleetListPlan::Patch, true, a, c) == a,
           "deferred reorder paints the on-screen order");
    expect(fleetPaintOrder(FleetListPlan::Patch, false, a, b) == b,
           "unchanged order paints the latest snapshot");
    expect(fleetPaintOrder(FleetListPlan::Reorder, true, a, c) == c,
           "reorder paints the sorted order");
    expect(fleetPaintOrder(FleetListPlan::Rebuild, true, a, c) == c,
           "rebuild paints the sorted order");
    expect(!fleetCommitOrder(FleetListPlan::Patch, true),
           "deferred reorder does not commit sorted indexes");
    expect(fleetCommitOrder(FleetListPlan::Patch, false),
           "patch with stable order commits");
    expect(fleetCommitOrder(FleetListPlan::Reorder, true),
           "successful reorder commits");

    if (gFails) {
        std::printf("%d test(s) failed\n", gFails);
        return 1;
    }
    std::printf("test_fleet_list: 25 passed\n");
    return 0;
}
