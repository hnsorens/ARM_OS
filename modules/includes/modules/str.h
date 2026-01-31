#ifndef STR_H
#define STR_H

#include "modules/structures/str.h"
#include "module_vtables.h"

#ifndef STR
#define STR str
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

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
GLOBAL void* (*CONCAT_EXPAND(STR, _memset))( void* s, int c, unsigned long n ) END 
 
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
GLOBAL void* (*CONCAT_EXPAND(STR, _memcpy))( void* dest, const void* src, unsigned long n ) END 
 
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
GLOBAL void* (*CONCAT_EXPAND(STR, _memmove))( void* dest, const void* src, unsigned long n ) END 
 
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
GLOBAL int (*CONCAT_EXPAND(STR, _memcmp))( const void* s1, const void* s2, unsigned long n ) END 
 
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
GLOBAL void* (*CONCAT_EXPAND(STR, _memchr))( const void* s, int c, unsigned long n ) END 
 
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
GLOBAL unsigned long (*CONCAT_EXPAND(STR, _strlen))( const char* s ) END 
 
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
GLOBAL char* (*CONCAT_EXPAND(STR, _strcpy))( char* dest, const char* src ) END 
 
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
GLOBAL char* (*CONCAT_EXPAND(STR, _strncpy))( char* dest, const char* src, unsigned long n ) END 
 
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
GLOBAL char* (*CONCAT_EXPAND(STR, _strcat))( char* dest, const char* src ) END 
 
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
GLOBAL char* (*CONCAT_EXPAND(STR, _strncat))( char* dest, const char* src, unsigned long n ) END 
 
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
GLOBAL int (*CONCAT_EXPAND(STR, _strcmp))( const char* s1, const char* s2 ) END 
 
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
GLOBAL int (*CONCAT_EXPAND(STR, _strncmp))( const char* s1, const char* s2, unsigned long n ) END 
 
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
GLOBAL char* (*CONCAT_EXPAND(STR, _strchr))( const char* s, int c ) END 
 
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
GLOBAL char* (*CONCAT_EXPAND(STR, _strrchr))( const char* s, int c ) END 
 
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
GLOBAL char* (*CONCAT_EXPAND(STR, _strstr))( const char* haystack, const char* needle ) END 
 
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
GLOBAL char* (*CONCAT_EXPAND(STR, _strdup))( const char* s ) END 
#ifdef __MAIN__

static void str_fetch(kernel_vtable_t *kvtable){
	str_vtable_t* module = (str_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_STR);
	CONCAT_EXPAND(STR, _memset) = module->memset;
	CONCAT_EXPAND(STR, _memcpy) = module->memcpy;
	CONCAT_EXPAND(STR, _memmove) = module->memmove;
	CONCAT_EXPAND(STR, _memcmp) = module->memcmp;
	CONCAT_EXPAND(STR, _memchr) = module->memchr;
	CONCAT_EXPAND(STR, _strlen) = module->strlen;
	CONCAT_EXPAND(STR, _strcpy) = module->strcpy;
	CONCAT_EXPAND(STR, _strncpy) = module->strncpy;
	CONCAT_EXPAND(STR, _strcat) = module->strcat;
	CONCAT_EXPAND(STR, _strncat) = module->strncat;
	CONCAT_EXPAND(STR, _strcmp) = module->strcmp;
	CONCAT_EXPAND(STR, _strncmp) = module->strncmp;
	CONCAT_EXPAND(STR, _strchr) = module->strchr;
	CONCAT_EXPAND(STR, _strrchr) = module->strrchr;
	CONCAT_EXPAND(STR, _strstr) = module->strstr;
	CONCAT_EXPAND(STR, _strdup) = module->strdup;
}
#endif

#undef GLOBAL
#undef STR

#endif