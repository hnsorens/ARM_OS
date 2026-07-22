# Operating System Kernel Development Conventions

This document defines the architectural guidelines, repository file layout, subsystem interface reflection patterns, coding standards, documentation style, and unit testing requirements. All kernel drivers, subsystems, and modules must strictly adhere to these conventions.

---

## 1. Directory & File Structure Conventions

### Module Repository Path Layout
Modules in the kernel repository must adhere strictly to the namespaced path format:
```text
module/<user>/<type>/<module_name>/<files>
```

For the standard kernel namespace under user **`hnsorens`**, module implementations, headers, and tests are placed in their corresponding subsystem directory:

```text
module/
└── hnsorens/
    ├── memory/
    │   └── heap/
    │       ├── heap.h
    │       ├── heap.c
    │       └── main.cpp
    ├── drivers/
    │   └── serial/
    │       ├── serial.h
    │       └── serial.c
    └── sys/
        └── vmm/
            ├── vmm.h
            └── vmm.c
```

### File Header Block
Every source (`.c`, `.cpp`) and header (`.h`) file must begin with a standardized Doxygen header block outlining its scope, role, and subsystem dependencies.

```cpp
/**
 * @file subsystem_name.c
 * @brief Short Subsystem Title / Primary Purpose
 * * Comprehensive multi-line description explaining architectural design, 
 * underlying data structures, operational mechanics, and integration details.
 */
```

### Header File Structure (`.h`)
Header files must use standard preprocessor include guards, organize symbols logically with explicit section markers, and expose clean interface structures.

```cpp
#ifndef SUBSYSTEM_NAME_H
#define SUBSYSTEM_NAME_H

#include <type.h>

/* --- Core Configuration Constraints --- */
#define SUBSYSTEM_MAGIC 0x414C4F43

/* --- Forward Declarations --- */
struct subsystem_context;

/* --- Data Structures --- */
struct subsystem_node {
    u32 magic; /**< Structural integrity token */
    u64 size;  /**< Payload footprint in bytes */
};

/* --- Public API Operations Kernel Interfaces --- */
int subsystem_create(u64 root, u64 size, struct subsystem_context **out_ctx);

#endif /* SUBSYSTEM_NAME_H */
```

---

## 2. Subsystem Reflection & Module Binding System

Subsystem communication uses the kernel's dynamic interface reflection framework defined in `<modules.h>`. Direct symbol linking across subsystem boundaries is forbidden; all cross-module interactions must be registered and resolved through linker section metadata macros.

### The Reflection Layer Interface Header (`<modules.h>`)
```cpp
#ifndef MODULES_H
#define MODULES_H

#define IMPORT_INTERFACE(Type, ModuleName, ApiName) \
__attribute__((section(".import." #Type "." #ModuleName), used, \
           aligned(8))) volatile const Type##_interface_t ApiName;

#define IMPORT_INTERFACE_ANY(Type, ApiName) \
__attribute__((section(".import." #Type), used, \
           aligned(8))) volatile const Type##_interface_t ApiName;

#define EXPORT_INTERFACE(Type, ModuleName, ...) \
__attribute__((section(".export." #Type "." #ModuleName), used, \
           aligned(8))) volatile const Type##_interface_t __export__ = __VA_ARGS__;

#define EXTERN_IMPORT_INTERFACE(Type, ApiName) \
extern volatile const Type##_interface_t ApiName;

#endif
```

### How to Import Modules
Depending on the file context (`.c` source, `.h` interface header, or `.cpp` test suite), use the appropriate import macro:

1. **In Subsystem Header / Interface Code (`.h` or `.cpp` test suites):**
   Use `IMPORT_INTERFACE_ANY(Type, ApiName)` or `IMPORT_INTERFACE(Type, ModuleName, ApiName)`. This injects metadata directly into the executable's `.import` section for runtime resolution.

   ```cpp
   /* --- Subsystem Module Imports --- */
   IMPORT_INTERFACE_ANY(vmm, vmm);
   IMPORT_INTERFACE_ANY(serial, serial);
   ```

2. **In C Source Files (`.c`):**
   Use `EXTERN_IMPORT_INTERFACE(Type, ApiName)` at top-level scope to declare external interface symbols initialized elsewhere.

   ```cpp
   /* --- Core Module Linkage Hooks --- */
   EXTERN_IMPORT_INTERFACE(vmm, vmm);
   EXTERN_IMPORT_INTERFACE(serial, serial);

   int example_function(void) {
       // Perform API call through imported interface node
       return vmm.allocate(...);
   }
   ```

### How to Export Modules
Every subsystem exposing dynamic entry points to the kernel must construct an interface struct and register its function dispatch matrix using `EXPORT_INTERFACE`:

```cpp
/* --- Subsystem API Interface Export Registration --- */
EXPORT_INTERFACE(heap, Heap,
         { .malloc    = heap_malloc,
           .free      = heap_free,
           .realloc   = heap_realloc,
           .memalign  = heap_memalign,
           .get_stats = heap_get_stats });
```

---

## 3. Documentation & Doxygen Rules

### Public Function Documentation
Every exported API function must be documented immediately above its implementation or header declaration using Doxygen notation.

* **`@brief`**: Concise statement of function behavior.
* **Detailed Body (`*`)**: Technical explanation of underlying strategies (e.g., First-Fit allocation, lockless sweeps, memory coalescing).
* **`@param[in]`**: Description of incoming read-only arguments.
* **`@param[out]`**: Description of output buffers or modified pointers.
* **`@return`**: Return status details (`0` on success, or positive `<errno.h>` error tokens like `EINVAL`, `ENOMEM`).

```cpp
/**
 * @brief Allocates an unaligned sequential chunk of memory space from a target heap.
 * * Uses a First-Fit parsing strategy across available free intrusive header list ranges.
 * * @param[in]  heap    Active storage pool allocator reference node.
 * @param[in]  size    Minimum capacity requirements to yield.
 * @param[out] out_ptr Destination reference storing the payload location address.
 * @return 0 on success, ENOMEM if space cannot be carved out, EINVAL for null handles.
 */
int heap_malloc(struct heap_context *heap, u64 size, void **out_ptr);
```

### Struct & Field Level Comments
All struct definitions must include inline field documentation formatted as `/**< comment */`.

```cpp
/**
 * @struct heap_block
 * @brief Intrusive boundary tag tracking individual chunk metadata.
 * * Every allocated or free memory chunk is prepended by this structural header.
 */
struct heap_block {
    u32               magic;    /**< Signature validation field checking for buffer overflows */
    bool              is_free;  /**< Flag indicating if chunk is available for scheduling */
    u64               size;     /**< Absolute payload block capacity in bytes (excludes header) */
    struct heap_block *next;    /**< Memory-contiguous subsequent sibling block pointer */
    struct heap_block *prev;    /**< Memory-contiguous antecedent sibling block pointer */
};
```

---

## 4. Coding Style & Implementation Guidelines

### Core Principles
1. **Standard Fixed-Width Types:** Use standard kernel sized types (`u8`, `u16`, `u32`, `u64`) and `uintptr_t` for raw address manipulations. Avoid raw standard C types (`int`, `long`) except as loop counters or exit status flags.
2. **Address Arithmetic:** Always cast pointers to `uintptr_t` prior to performing bitwise operations or pointer offset arithmetic.
3. **Alignment Alignment Macros:** Use preprocessor macros for standard boundary adjustments (e.g., `#define HEAP_ALIGN(x) (((x) + 15) & ~15)`).

### Single-Exit Cleanup Flow (`goto cleanup`)
Functions allocating resources, manipulating pointers, or modifying global system states must enforce a single-exit cleanup path to guarantee memory safety and prevent leaks.

```cpp
int heap_malloc(struct heap_context *heap, u64 size, void **out_ptr)
{
    int status = 0;

    /* 1. Parameter Validation */
    if (!heap || !out_ptr) {
        status = EINVAL;
        goto cleanup;
    }

    if (size == 0) {
        *out_ptr = NULL;
        goto cleanup;
    }

    /* 2. Operational Logic */
    void *ptr = locate_chunk(heap, size);
    if (!ptr) {
        *out_ptr = NULL;
        status = ENOMEM;
        goto cleanup;
    }

    *out_ptr = ptr;

cleanup:
    return status;
}
```

### Memory Corruption Integrity Standard
* **Validation Magic Headers:** Operational structures must include signature fields (e.g., `HEAP_MAGIC_ALLOCATED = 0x414C4F43`, `HEAP_MAGIC_FREE = 0x46524545`).
* **Runtime Verification:** Validate magic headers before processing blocks. Mismatches must immediately return `EINVAL` or trigger assertion faults.
* **Stale Token Zeroing:** Clear magic fields (`block->magic = 0`) on consumed, invalidated, or merged memory blocks to prevent double-free and use-after-free conditions.

---

## 5. Unit Testing Conventions & Execution

### Test Header & Serial Subsystem Dependency
To write unit tests, you **must include `<test.h>`** to access the test runner and assertion macros (`TEST()`, `TEST_INIT()`, `EXPECT_EQ()`, etc.).

In addition, unit test suites output diagnostic telemetry to the serial debug console. **Any file declaring unit tests (`#ifdef TESTING`) MUST explicitly include `<test.h>` and import the `serial` interface.**

```cpp
#ifdef TESTING

#include <test.h>

/* Serial module import is strictly mandatory for unit testing telemetry */
IMPORT_INTERFACE_ANY(serial, serial);
IMPORT_INTERFACE_ANY(mmu, mmu);

/**
 * @brief Test Case: Basic Subsystem Verification
 */
TEST(Subsystem_TestCaseName)
{
    TEST_INIT();

    // 1. Setup
    struct heap_context *heap = NULL;
    u64 vmm_root;
    mmu.get_kernel_ctx(&vmm_root);

    // 2. Execution & Assertions
    int status = heap_create(vmm_root, 64 * 1024, &heap);
    EXPECT_EQ(status, 0);
    EXPECT_NE(heap, NULL);

    // 3. Complete Teardown
    status = heap_destroy(heap);
    EXPECT_EQ(status, 0);

    TEST_RESULT();
}

#endif
```

### Available Assertion Reference

| Assertion | Behavioral Expectation |
| :--- | :--- |
| `EXPECT_EQ(a, b)` | Asserts `a == b` |
| `EXPECT_NE(a, b)` | Asserts `a != b` |
| `EXPECT_LT(a, b)` | Asserts `a < b` |
| `EXPECT_LE(a, b)` | Asserts `a <= b` |

### Required Test Coverage Categories
Every subsystem test suite must cover:
1. **Lifecycle Sanity:** Instance setup, allocations, memory frees, and teardown cycles.
2. **Fragmentation & Coalescing:** Validating bidirectional free block merging and memory re-use.
3. **Alignment Boundary Logic:** Verifying power-of-two constraints (e.g., 64-byte aligned blocks or 4096-byte page boundaries).
4. **Security & Corruption Faults:** Handling zero-byte requests, `NULL` frees, out-of-memory (`ENOMEM`), bad alignment parameters (`EINVAL`), and catching modified magic tokens.

### Running Unit Tests
To compile the kernel with testing modules enabled and execute the full unit test suite, execute the build system command from the repository root:

```bash
make test
```
