#ifndef STR_VTABLE_H
#define STR_VTABLE_H


#include "str_types.h"

/**
 * @brief String/Memory Operations VTable
 * 
 * Standard C library-style string and memory manipulation functions
 * optimized for kernel use (no dynamic allocation, safe for all contexts).
 */
typedef struct str_ops
{
  /**
    * @brief Fill memory with constant byte
    * 
    * Sets the first 'n' bytes of the memory area pointed to by 's'
    * to the specified value 'c'.
    * 
    * @param s Pointer to memory area to fill
    * @param c Value to set (converted to unsigned char)
    * @param n Number of bytes to set
    * @return void* Original pointer 's'
    * 
    */
  void* (*memset)(void* s, int c, unsigned long n);

  /**
    * @brief Copy memory area
    * 
    * Copies 'n' bytes from memory area 'src' to memory area 'dest'.
    * Memory areas must not overlap (use memmove for overlapping).
    * 
    * @param dest Destination memory area
    * @param src Source memory area
    * @param n Number of bytes to copy
    * @return void* Original destination pointer 'dest'
    * 
    */
  void* (*memcpy)(void* dest, const void* src, unsigned long n);

  /**
    * @brief Copy memory area (safe for overlap)
    * 
    * Copies 'n' bytes from memory area 'src' to memory area 'dest'.
    * Handles overlapping memory areas correctly.
    * 
    * @param dest Destination memory area
    * @param src Source memory area
    * @param n Number of bytes to copy
    * @return void* Original destination pointer 'dest'
    * 
    */
  void* (*memmove)(void* dest, const void* src, unsigned long n);

  /**
    * @brief Compare memory areas
    * 
    * Compares the first 'n' bytes of memory areas 's1' and 's2'.
    * 
    * @param s1 First memory area
    * @param s2 Second memory area
    * @param n Number of bytes to compare
    * @return int <0 if s1 < s2, 0 if s1 == s2, >0 if s1 > s2
    * 
    */
  int (*memcmp)(const void* s1, const void* s2, unsigned long n);

  /**
    * @brief Locate byte in memory
    * 
    * Scans the first 'n' bytes of memory area 's' for the first
    * occurrence of byte 'c' (converted to unsigned char).
    * 
    * @param s Memory area to scan
    * @param c Byte to search for
    * @param n Number of bytes to scan
    * @return void* Pointer to matching byte, or NULL if not found
    * 
    */
  void* (*memchr)(const void* s, int c, unsigned long n);

  /**
    * @brief Calculate string length
    * 
    * Computes the length of the string 's' (number of characters
    * before the terminating null byte).
    * 
    * @param s Null-terminated string
    * @return unsigned long Length of string in bytes
    * 
    */
  unsigned long (*strlen)(const char* s);

  /**
    * @brief Copy string
    * 
    * Copies the string pointed to by 'src' (including null terminator)
    * to the buffer pointed to by 'dest'.
    * 
    * @param dest Destination buffer (must be large enough)
    * @param src Source string
    * @return char* Original destination pointer 'dest'
    * 
    */
  char* (*strcpy)(char* dest, const char* src);

  /**
    * @brief Copy string with length limit
    * 
    * Copies at most 'n' characters from 'src' to 'dest'.
    * If 'src' is shorter than 'n', pads with null bytes.
    * 
    * @param dest Destination buffer
    * @param src Source string
    * @param n Maximum number of characters to copy
    * @return char* Original destination pointer 'dest'
    * 
    */
  char* (*strncpy)(char* dest, const char* src, unsigned long n);

  /**
    * @brief Concatenate strings
    * 
    * Appends a copy of 'src' to the end of 'dest'.
    * The null terminator from 'dest' is overwritten.
    * 
    * @param dest Destination buffer (must have enough space)
    * @param src Source string to append
    * @return char* Original destination pointer 'dest'
    * 
    */
  char* (*strcat)(char* dest, const char* src);

  /**
    * @brief Concatenate strings with length limit
    * 
    * Appends at most 'n' characters from 'src' to 'dest',
    * then adds a null terminator.
    * 
    * @param dest Destination buffer
    * @param src Source string to append
    * @param n Maximum number of characters to append
    * @return char* Original destination pointer 'dest'
    * 
    */
  char* (*strncat)(char* dest, const char* src, unsigned long n);

  /**
    * @brief Compare two strings
    * 
    * Compares strings 's1' and 's2' lexicographically.
    * 
    * @param s1 First string
    * @param s2 Second string
    * @return int <0 if s1 < s2, 0 if s1 == s2, >0 if s1 > s2
    * 
    */
  int (*strcmp)(const char* s1, const char* s2);

  /**
    * @brief Compare strings with length limit
    * 
    * Compares at most 'n' characters of strings 's1' and 's2'.
    * 
    * @param s1 First string
    * @param s2 Second string
    * @param n Maximum number of characters to compare
    * @return int <0 if s1 < s2, 0 if s1 == s2, >0 if s1 > s2
    * 
    */
  int (*strncmp)(const char* s1, const char* s2, unsigned long n);

  /**
    * @brief Locate first occurrence of character in string
    * 
    * Returns a pointer to the first occurrence of character 'c'
    * in the string 's'. The null terminator is considered part
    * of the string.
    * 
    * @param s String to search
    * @param c Character to locate (converted to char)
    * @return char* Pointer to character, or NULL if not found
    * 
    */
  char* (*strchr)(const char* s, int c);

  /**
    * @brief Locate last occurrence of character in string
    * 
    * Returns a pointer to the last occurrence of character 'c'
    * in the string 's'.
    * 
    * @param s String to search
    * @param c Character to locate
    * @return char* Pointer to last occurrence, or NULL if not found
    * 
    */
  char* (*strrchr)(const char* s, int c);

  /**
    * @brief Locate substring
    * 
    * Finds the first occurrence of the substring 'needle'
    * in the string 'haystack'. The null terminator is not compared.
    * 
    * @param haystack String to search in
    * @param needle Substring to search for
    * @return char* Pointer to beginning of substring, or NULL if not found
    * 
    */
  char* (*strstr)(const char* haystack, const char* needle);

  /**
    * @brief Duplicate string
    * 
    * Returns a pointer to a new string which is a duplicate of 's'.
    * Memory is obtained with kmalloc and must be freed with kfree.
    * 
    * @param s String to duplicate
    * @return char* Pointer to duplicated string, or NULL on failure
    * 
    */
  char* (*strdup)(const char* s);

} str_ops;

#endif