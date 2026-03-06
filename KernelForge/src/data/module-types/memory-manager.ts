export default {
  "typeName": "Memory Manager",
  "description": "Memory allocation and virtual memory management",
  "layer": "kernel",
  "vtable": [
    {
      "name": "init",
      "signature": "void (*init)(void)",
      "description": "Initialize memory manager",
      "required": true
    },
    {
      "name": "alloc",
      "signature": "void* (*alloc)(size_t size)",
      "description": "Allocate memory",
      "required": true
    },
    {
      "name": "free",
      "signature": "void (*free)(void* ptr)",
      "description": "Free memory",
      "required": true
    },
    {
      "name": "map",
      "signature": "int (*map)(void* virt, void* phys, size_t size)",
      "description": "Map virtual to physical",
      "required": false
    },
    {
      "name": "unmap",
      "signature": "int (*unmap)(void* virt, size_t size)",
      "description": "Unmap virtual address",
      "required": false
    }
  ],
  "extensions": []
};
