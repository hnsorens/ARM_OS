#include "bitmap.h"

// Create bitmap with 'num_bits' bits (all initialized to 0)
bitmap_t* bitmap_create(uintptr_t bitmap_memory, size_t num_bits) {
    bitmap_t *bitmap = (bitmap_t*)bitmap_memory;
    if (!bitmap) return NULL;

    bitmap->size = num_bits;
    bitmap->num_words = (num_bits + 63) / 64;
    bitmap->bits = (uint64_t*)(bitmap_memory + sizeof(bitmap_t));

    if (!bitmap->bits) {
        //free(bitmap);
        return NULL;
    }

    return bitmap;
}

size_t bitmap_memory_size(size_t num_bits)
{
  return sizeof(bitmap_t) + ((num_bits + 63) / 64) * sizeof(uint64_t);
}

// Set bit at position (set to 1)
void bitmap_set(bitmap_t *bitmap, size_t pos) {
    if (pos < bitmap->size) {
        size_t word = pos / 64;
        size_t bit = pos % 64;
        bitmap->bits[word] |= (1ULL << bit);
    }
}

// Clear bit at position (set to 0)
void bitmap_clear(bitmap_t *bitmap, size_t pos) {
    if (pos < bitmap->size) {
        size_t word = pos / 64;
        size_t bit = pos % 64;
        bitmap->bits[word] &= ~(1ULL << bit);
    }
}

// Get bit value at position
bool bitmap_get(const bitmap_t *bitmap, size_t pos) {
    if (pos < bitmap->size) {
        size_t word = pos / 64;
        size_t bit = pos % 64;
        return (bitmap->bits[word] >> bit) & 1;
    }
    return false;
}

uint8_t bitmap_get_nibble(const bitmap_t *bitmap, size_t pos) {
    if (pos < bitmap->size) {
        size_t nibble_index = pos / 4;        // Which 4-bit group
        size_t word_index = nibble_index / 16; // 16 nibbles per uint64_t (64 bits / 4)
        size_t nibble_in_word = nibble_index % 16;
        size_t bit_offset = nibble_in_word * 4;

        return (bitmap->bits[word_index] >> bit_offset) & 0xF;
    }
    return 0;
}
