#ifndef MODULES_H
#define MODULES_H

#define IMPORT_MODULE(Type, ModuleName, ApiName) \
__attribute__((section(".import." #Type "." #ModuleName), used, \
	       aligned(8))) volatile static const Type##_interface_t ApiName;

#define IMPORT_MODULE_ANY(Type, ApiName) \
__attribute__((section(".import." #Type), used, \
	       aligned(8))) volatile static const Type##_interface_t ApiName;

#define EXPORT_MODULE(Type, ModuleName, ...) \
__attribute__((section(".export." #Type "." #ModuleName), used, \
	       aligned(8))) volatile static const Type##_interface_t __export__ = __VA_ARGS__;

#endif
