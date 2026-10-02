#pragma once
#include <cassert>
#define assert_not_null(x) assert((x) != nullptr)
#define assert_true(x) assert(x)
#define assert_always() assert(false)
