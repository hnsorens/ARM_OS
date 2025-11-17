#ifndef BITMAP_H
#define BITMAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct bitmap_t {
    uint64_t *bits;
    size_t size;        // Number of bits
    size_t num_words;   // Number of uint64_t words needed
} bitmap_t;

// Create bitmap with 'num_bits' bits (all initialized to 0)
bitmap_t* bitmap_create(uintptr_t bitmap_memory, size_t num_bits);

size_t bitmap_memory_size(size_t num_bits);

// Set bit at position (set to 1)
void bitmap_set(bitmap_t *bitmap, size_t pos);

// Clear bit at position (set to 0)
void bitmap_clear(bitmap_t *bitmap, size_t pos);

// Get bit value at position
bool bitmap_get(const bitmap_t *bitmap, size_t pos);

uint8_t bitmap_get_nibble(const bitmap_t *bitmap, size_t pos);


#endif
