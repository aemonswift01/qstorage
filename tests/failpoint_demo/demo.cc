#include "demo.h"
#include "failpoint_macro.h"

int WriteWAL() {
    FAIL_POINT_RETURN("wal::write::fail", 1);

    FAIL_POINT_SLEEP("wal::write::delay", 50);

    return 0;
}