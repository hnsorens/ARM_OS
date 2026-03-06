export default {
  "typeName": "VFS",
  "description": "Virtual File System - unified interface layer for all filesystems",
  "layer": "kernel",
  "vtable": [
    {
      "name": "init",
      "signature": "void (*init)(void)",
      "description": "Initialize filesystem",
      "required": true
    },
    {
      "name": "open",
      "signature": "file_t* (*open)(const char* path, int flags)",
      "description": "Open file",
      "required": true
    },
    {
      "name": "close",
      "signature": "int (*close)(file_t* file)",
      "description": "Close file",
      "required": true
    },
    {
      "name": "read",
      "signature": "ssize_t (*read)(file_t* file, void* buf, size_t count)",
      "description": "Read from file",
      "required": true
    },
    {
      "name": "write",
      "signature": "ssize_t (*write)(file_t* file, const void* buf, size_t count)",
      "description": "Write to file",
      "required": true
    },
    {
      "name": "mkdir",
      "signature": "int (*mkdir)(const char* path)",
      "description": "Create directory",
      "required": false
    }
  ],
  "extensions": []
};
