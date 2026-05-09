#ifndef MODULES_H
#define MODULES_H

#define IMPORT_INTERFACE(Type, ModuleName, ApiName) \
__attribute__((section(".import." #Type "." #ModuleName), used, \
	       aligned(8))) volatile const Type##_interface_t ApiName;

#define IMPORT_INTERFACE_ANY(Type, ApiName) \
__attribute__((section(".import." #Type), used, \
	       aligned(8))) volatile const Type##_interface_t ApiName;

#define EXPORT_INTERFACE(Type, ModuleName, ...) \
__attribute__((section(".export." #Type "." #ModuleName), used, \
	       aligned(8))) volatile const Type##_interface_t __export__ = __VA_ARGS__;

#define EXTERN_IMPORT_INTERFACE(Type, ApiName) \
extern volatile const Type##_interface_t ApiName;

#endif
