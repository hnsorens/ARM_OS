#ifndef TEST_H
#define TEST_H

typedef int (*kernel_test_func_t)(void);

struct kernel_test_entry {
    const char* test_name;          // Stores the readable name string
    kernel_test_func_t function;   // Stores the execution address
};

#define TEST(name) \
    int name(); \
    __attribute__((section(".kernel_test"), used, aligned(8))) \
    static struct kernel_test_entry __entry_##name = { \
        .test_name = #name, \
        .function = name, \
    }; \
    int name()

#define ANSI_RED    "\033[1;31m"
#define ANSI_RESET  "\033[0m"

// Core tracking macro: initialize this at the start of your test function
#define TEST_INIT() int __test_status = 0

// Core return macro: use this at the end of your test function
#define TEST_RESULT() return __test_status

// Internal failure handler to keep the macros clean
#define _TEST_FAIL_MSG(fmt, ...) do { \
    serial.printf("[" ANSI_RED "   ERROR   " ANSI_RESET "] " fmt "\n", ##__VA_ARGS__); \
    __test_status = 1; \
} while(0)

// --- Expectation Macros ---

// Expects A == B (integers, pointers, sizes)
#define EXPECT_EQ(actual, expected) do { \
    __typeof__(actual) _act = (actual); \
    __typeof__(expected) _exp = (expected); \
    if (_act != _exp) { \
        _TEST_FAIL_MSG("%s:%d: Expected %s (%ld) == %s (%ld)", \
                       __FILE__, __LINE__, #actual, (long)_act, #expected, (long)_exp); \
    } \
} while(0)

// Expects A != B
#define EXPECT_NE(actual, expected) do { \
    __typeof__(actual) _act = (actual); \
    __typeof__(expected) _exp = (expected); \
    if (_act == _exp) { \
        _TEST_FAIL_MSG("%s:%d: Expected %s (%ld) != %s (%ld)", \
                       __FILE__, __LINE__, #actual, (long)_act, #expected, (long)_exp); \
    } \
} while(0)

// Expects a condition to be true
#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { \
        _TEST_FAIL_MSG("%s:%d: Condition (%s) was expected to be TRUE", \
                       __FILE__, __LINE__, #cond); \
    } \
} while(0)

// Expects a condition to be false
#define EXPECT_FALSE(cond) do { \
    if (cond) { \
        _TEST_FAIL_MSG("%s:%d: Condition (%s) was expected to be FALSE", \
                       __FILE__, __LINE__, #cond); \
    } \
} while(0)

#endif
