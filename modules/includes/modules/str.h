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

#define GLOBAL __attribute__((visibility("hidden")))

#ifdef __MAIN__

#define __STR__DEF(prefix) \
GLOBAL void* (*CONCAT_EXPAND(prefix, _memset))( void*, int, unsigned long ) = 0; \
GLOBAL void* (*CONCAT_EXPAND(prefix, _memcpy))( void*, const void*, unsigned long ) = 0; \
GLOBAL void* (*CONCAT_EXPAND(prefix, _memmove))( void*, const void*, unsigned long ) = 0; \
GLOBAL int (*CONCAT_EXPAND(prefix, _memcmp))( const void*, const void*, unsigned long ) = 0; \
GLOBAL void* (*CONCAT_EXPAND(prefix, _memchr))( const void*, int, unsigned long ) = 0; \
GLOBAL unsigned long (*CONCAT_EXPAND(prefix, _strlen))( const char* ) = 0; \
GLOBAL char* (*CONCAT_EXPAND(prefix, _strcpy))( char*, const char* ) = 0; \
GLOBAL char* (*CONCAT_EXPAND(prefix, _strncpy))( char*, const char*, unsigned long ) = 0; \
GLOBAL char* (*CONCAT_EXPAND(prefix, _strcat))( char*, const char* ) = 0; \
GLOBAL char* (*CONCAT_EXPAND(prefix, _strncat))( char*, const char*, unsigned long ) = 0; \
GLOBAL int (*CONCAT_EXPAND(prefix, _strcmp))( const char*, const char* ) = 0; \
GLOBAL int (*CONCAT_EXPAND(prefix, _strncmp))( const char*, const char*, unsigned long ) = 0; \
GLOBAL char* (*CONCAT_EXPAND(prefix, _strchr))( const char*, int ) = 0; \
GLOBAL char* (*CONCAT_EXPAND(prefix, _strrchr))( const char*, int ) = 0; \
GLOBAL char* (*CONCAT_EXPAND(prefix, _strstr))( const char*, const char* ) = 0; \
GLOBAL char* (*CONCAT_EXPAND(prefix, _strdup))( const char* ) = 0; \
\
static void str_fetch(kernel_vtable_t *kvtable){\
	str_vtable_t* module = (str_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_STR);\
	CONCAT_EXPAND(prefix, _memset) = module->memset;\
	CONCAT_EXPAND(prefix, _memcpy) = module->memcpy;\
	CONCAT_EXPAND(prefix, _memmove) = module->memmove;\
	CONCAT_EXPAND(prefix, _memcmp) = module->memcmp;\
	CONCAT_EXPAND(prefix, _memchr) = module->memchr;\
	CONCAT_EXPAND(prefix, _strlen) = module->strlen;\
	CONCAT_EXPAND(prefix, _strcpy) = module->strcpy;\
	CONCAT_EXPAND(prefix, _strncpy) = module->strncpy;\
	CONCAT_EXPAND(prefix, _strcat) = module->strcat;\
	CONCAT_EXPAND(prefix, _strncat) = module->strncat;\
	CONCAT_EXPAND(prefix, _strcmp) = module->strcmp;\
	CONCAT_EXPAND(prefix, _strncmp) = module->strncmp;\
	CONCAT_EXPAND(prefix, _strchr) = module->strchr;\
	CONCAT_EXPAND(prefix, _strrchr) = module->strrchr;\
	CONCAT_EXPAND(prefix, _strstr) = module->strstr;\
	CONCAT_EXPAND(prefix, _strdup) = module->strdup;\
}

__STR__DEF(STR) 
#undef __STR__DEF

#else

#define __STR__DEF(prefix) \
GLOBAL extern void* (*CONCAT_EXPAND(prefix, _memset))( void*, int, unsigned long ); \
GLOBAL extern void* (*CONCAT_EXPAND(prefix, _memcpy))( void*, const void*, unsigned long ); \
GLOBAL extern void* (*CONCAT_EXPAND(prefix, _memmove))( void*, const void*, unsigned long ); \
GLOBAL extern int (*CONCAT_EXPAND(prefix, _memcmp))( const void*, const void*, unsigned long ); \
GLOBAL extern void* (*CONCAT_EXPAND(prefix, _memchr))( const void*, int, unsigned long ); \
GLOBAL extern unsigned long (*CONCAT_EXPAND(prefix, _strlen))( const char* ); \
GLOBAL extern char* (*CONCAT_EXPAND(prefix, _strcpy))( char*, const char* ); \
GLOBAL extern char* (*CONCAT_EXPAND(prefix, _strncpy))( char*, const char*, unsigned long ); \
GLOBAL extern char* (*CONCAT_EXPAND(prefix, _strcat))( char*, const char* ); \
GLOBAL extern char* (*CONCAT_EXPAND(prefix, _strncat))( char*, const char*, unsigned long ); \
GLOBAL extern int (*CONCAT_EXPAND(prefix, _strcmp))( const char*, const char* ); \
GLOBAL extern int (*CONCAT_EXPAND(prefix, _strncmp))( const char*, const char*, unsigned long ); \
GLOBAL extern char* (*CONCAT_EXPAND(prefix, _strchr))( const char*, int ); \
GLOBAL extern char* (*CONCAT_EXPAND(prefix, _strrchr))( const char*, int ); \
GLOBAL extern char* (*CONCAT_EXPAND(prefix, _strstr))( const char*, const char* ); \
GLOBAL extern char* (*CONCAT_EXPAND(prefix, _strdup))( const char* ); \


__STR__DEF(STR) 
#undef GLOBAL
#undef __STR__DEF
#undef STR

#endif
#endif