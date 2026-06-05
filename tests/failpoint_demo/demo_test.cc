#include "demo.h"
#include <gtest/gtest.h>
#include "failpoint.h"
#include "failpoint_scope.h"

TEST(DBTest, OpenFailOnce) {
    failpoint::ScopedFailPoint fp("wal::write::fail", failpoint::Mode::ONCE);

    EXPECT_EQ(WriteWAL(), 1);
}