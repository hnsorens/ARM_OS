#ifndef __STR_INC_H__
#define __STR_INC_H__


#include "str_driver.h"

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern
#define END ;
#endif

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
GLOBAL void* (*str_memset)( void* s, int c, unsigned long n ) END 
 
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
GLOBAL void* (*str_memcpy)( void* dest, const void* src, unsigned long n ) END 
 
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
GLOBAL void* (*str_memmove)( void* dest, const void* src, unsigned long n ) END 
 
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
GLOBAL int (*str_memcmp)( const void* s1, const void* s2, unsigned long n ) END 
 
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
GLOBAL void* (*str_memchr)( const void* s, int c, unsigned long n ) END 
 
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
GLOBAL unsigned long (*str_strlen)( const char* s ) END 
 
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
GLOBAL char* (*str_strcpy)( char* dest, const char* src ) END 
 
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
GLOBAL char* (*str_strncpy)( char* dest, const char* src, unsigned long n ) END 
 
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
GLOBAL char* (*str_strcat)( char* dest, const char* src ) END 
 
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
GLOBAL char* (*str_strncat)( char* dest, const char* src, unsigned long n ) END 
 
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
GLOBAL int (*str_strcmp)( const char* s1, const char* s2 ) END 
 
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
GLOBAL int (*str_strncmp)( const char* s1, const char* s2, unsigned long n ) END 
 
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
GLOBAL char* (*str_strchr)( const char* s, int c ) END 
 
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
GLOBAL char* (*str_strrchr)( const char* s, int c ) END 
 
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
GLOBAL char* (*str_strstr)( const char* haystack, const char* needle ) END 
 
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
GLOBAL char* (*str_strdup)( const char* s ) END 

#ifdef __MAIN__

static void str_fetch(core_ops *ops) {
	str_driver *driver = (str_driver*)ops->find_module_by_type(MODULE_STR);
str_memset = driver->str->memset;
str_memcpy = driver->str->memcpy;
str_memmove = driver->str->memmove;
str_memcmp = driver->str->memcmp;
str_memchr = driver->str->memchr;
str_strlen = driver->str->strlen;
str_strcpy = driver->str->strcpy;
str_strncpy = driver->str->strncpy;
str_strcat = driver->str->strcat;
str_strncat = driver->str->strncat;
str_strcmp = driver->str->strcmp;
str_strncmp = driver->str->strncmp;
str_strchr = driver->str->strchr;
str_strrchr = driver->str->strrchr;
str_strstr = driver->str->strstr;
str_strdup = driver->str->strdup;
}

#endif
#endif