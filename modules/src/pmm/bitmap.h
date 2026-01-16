#ifndef BITMAP_H
#define BITMAP_H

typedef unsigned long   bitmap_word_t;
typedef unsigned long   bitmap_size_t;
typedef int             bool_t;
typedef unsigned long   uintptr_t;

/**
 * @brief Bitmap structure for tracking binary states
 *
 * Represents an array of bits where each bit can be on or off
 * Used for tracking allocation stetes of buddy allocation blocks
 */
typedef struct bitmap_t {
    bitmap_word_t *data;
    bitmap_size_t total_bits;
    bitmap_size_t word_capacity;
} bitmap_t;

/**
 * @brief Create a bitmap in existing memory
 *
 * Initializes bitmap structure a prepares bit storage
 * The bitmap starts with all bits cleared (0)
 * To get the bitmap memory required size, use the bitmap_memory_size function
 *
 * @param bitmap_base Memory region to hold bitmap structure
 * @param bit_count Number of bits the bitmap will manager
 * @return Initialized bitmap structure
 */
bitmap_t* bitmap_create(uintptr_t bitmap_base, bitmap_size_t bit_count);

/**
 * @brief Calculate memory needed for a bitmap
 * 
 * Computes total bytes required for bitmap structure and bit storage.
 * 
 * @param num_bits Number of bits the bitmap needs to store
 * @return Bytes needed for bitmap structure and storage
 */
bitmap_size_t bitmap_memory_size(bitmap_size_t num_bits);

/**
 * @brief Turn a bit on
 * 
 * Sets the specified bit position to 1
 *
 * @param bitmap Bitmap to modify
 * @param position Which bit to set (0-indexed)
 */
void bitmap_set(bitmap_t *bitmap, bitmap_size_t position);

/**
 * @brief Turn a bit off
 * 
 * Sets the specified bit position to 0.
 * 
 * @param bitmap Bitmap to modify
 * @param position Which bit to clear (0-indexed)
 */
void bitmap_clear(bitmap_t *bitmap, bitmap_size_t position);


/**
 * @brief Check if a bit is on
 * 
 * Reads the current state of a bit.
 * 
 * @param bitmap Bitmap to query
 * @param position Which bit to read (0-indexed)
 * @return 1 if bit is set, 0 if clear or out of range
 */
bool_t bitmap_test(const bitmap_t *bitmap, bitmap_size_t pos);

#endif

