#include <gtest/gtest.h>
#include "xlog.h"

TEST(MathUtilsTest, AddTwoNumbers)
{
    EXPECT_EQ(add(2, 3), 5);
    EXPECT_EQ(add(-1, 1), 0);
    EXPECT_EQ(add(0, 0), 0);
}

TEST(MathUtilsTest, AddNegativeNumbers)
{
    EXPECT_EQ(add(-5, -3), -8);
}