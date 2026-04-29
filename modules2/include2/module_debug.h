

#include <stdint.h>
#define MODULE_DEBUG_ENABLED


#ifndef MODULE_DEBUG
#define MODULE_DEBUG


#ifdef MODULE_DEBUG_ENABLED
  #define BREAK while(1);
  #define LOG(reg, val) __asm__ volatile("mov " #reg ", %0" :: "r"((unsigned long)val));
#else
  #define BREAK
  #define LOG(reg, val)
#endif


#endif
