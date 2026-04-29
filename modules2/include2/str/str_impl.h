#ifndef __STR_INC_H__
#define __STR_INC_H__


#include "str_types.h"

#ifndef CONCAT_HIDDEN
#define CONCAT_HIDDEN(a, b) a ## b
#define CONCAT(a, b) CONCAT_HIDDEN(a, b)
#endif

#define str_memset CONCAT(IMPL_NAME, _memset_func)
#define str_memcpy CONCAT(IMPL_NAME, _memcpy_func)
#define str_memmove CONCAT(IMPL_NAME, _memmove_func)
#define str_memcmp CONCAT(IMPL_NAME, _memcmp_func)
#define str_memchr CONCAT(IMPL_NAME, _memchr_func)
#define str_strlen CONCAT(IMPL_NAME, _strlen_func)
#define str_strcpy CONCAT(IMPL_NAME, _strcpy_func)
#define str_strncpy CONCAT(IMPL_NAME, _strncpy_func)
#define str_strcat CONCAT(IMPL_NAME, _strcat_func)
#define str_strncat CONCAT(IMPL_NAME, _strncat_func)
#define str_strcmp CONCAT(IMPL_NAME, _strcmp_func)
#define str_strncmp CONCAT(IMPL_NAME, _strncmp_func)
#define str_strchr CONCAT(IMPL_NAME, _strchr_func)
#define str_strrchr CONCAT(IMPL_NAME, _strrchr_func)
#define str_strstr CONCAT(IMPL_NAME, _strstr_func)
#define str_strdup CONCAT(IMPL_NAME, _strdup_func)

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
__attribute__((used)) void* str_memset( void* s, int c, unsigned long n );
 
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
__attribute__((used)) void* str_memcpy( void* dest, const void* src, unsigned long n );
 
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
__attribute__((used)) void* str_memmove( void* dest, const void* src, unsigned long n );
 
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
__attribute__((used)) int str_memcmp( const void* s1, const void* s2, unsigned long n );
 
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
__attribute__((used)) void* str_memchr( const void* s, int c, unsigned long n );
 
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
__attribute__((used)) unsigned long str_strlen( const char* s );
 
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
__attribute__((used)) char* str_strcpy( char* dest, const char* src );
 
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
__attribute__((used)) char* str_strncpy( char* dest, const char* src, unsigned long n );
 
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
__attribute__((used)) char* str_strcat( char* dest, const char* src );
 
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
__attribute__((used)) char* str_strncat( char* dest, const char* src, unsigned long n );
 
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
__attribute__((used)) int str_strcmp( const char* s1, const char* s2 );
 
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
__attribute__((used)) int str_strncmp( const char* s1, const char* s2, unsigned long n );
 
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
__attribute__((used)) char* str_strchr( const char* s, int c );
 
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
__attribute__((used)) char* str_strrchr( const char* s, int c );
 
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
__attribute__((used)) char* str_strstr( const char* haystack, const char* needle );
 
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
__attribute__((used)) char* str_strdup( const char* s );
typedef void (*init_fn_t)(void);
#define __init_func __attribute__((section(".init_array"), used))
#define MODULE_INIT(func) \
static init_fn_t __init_ptr##func __init_func = func;
#endif