#include "bitmap.h"

#define BITS_PER_WORD           64
#define NIBBLE_BIT_WIDTH        4
#define NIBBLES_PER_WORD        16
#define BYTES_PER_WORD          8

static inline size_t calculate_required_words(size_t bit_count) {
    return (bit_count + BITS_PER_WORD - 1) / BITS_PER_WORD;
}

static inline size_t locate_word_index(size_t bit_position) {
    return bit_position / BITS_PER_WORD;
}

static inline size_t locate_bit_within_word(size_t bit_position) {
    return bit_position % BITS_PER_WORD;
}

bitmap_t* bitmap_create(void *bitmap_base, size_t bit_count) {
    bitmap_t *new_bitmap = (bitmap_t *)bitmap_base;

    new_bitmap->total_bits = bit_count;
    new_bitmap->word_capacity = calculate_required_words(bit_count);
    new_bitmap->data = (bitmap_word_t*)(bitmap_base + sizeof(bitmap_t));

    // Zeros out all bits
    for (size_t word_index = 0; word_index < new_bitmap->word_capacity; ++word_index) {
        new_bitmap->data[word_index] = 0;
    }

    return new_bitmap;
}

size_t bitmap_memory_size(size_t bit_count) {
    return sizeof(bitmap_t) + calculate_required_words(bit_count) * sizeof(bitmap_word_t);
}

void bitmap_set(bitmap_t* bitmap, size_t position) {
    // Checks to make sure bit is set in valid memory
    if (position < bitmap->total_bits) {
        size_t target_word   = locate_word_index(position);
        size_t bit_offset    = locate_bit_within_word(position);
        
        bitmap->data[target_word] |= (1ULL << bit_offset);
    }
}

void bitmap_clear(bitmap_t* bitmap, size_t position) {
    // Checks to make sure bit is cleared in valid memory
    if (position < bitmap->total_bits) {
        size_t target_word   = locate_word_index(position);
        size_t bit_offset    = locate_bit_within_word(position);
        
        bitmap->data[target_word] &= ~(1ULL << bit_offset);
    }
}

bool_t bitmap_test(const bitmap_t* bitmap, size_t position) {
    // Checks to make sure bit is fetched in valid memory
    if (position < bitmap->total_bits) {
        size_t target_word   = locate_word_index(position);
        size_t bit_offset    = locate_bit_within_word(position);
        
        return (bitmap->data[target_word] >> bit_offset) & 1;
    }
    return 0;
}

