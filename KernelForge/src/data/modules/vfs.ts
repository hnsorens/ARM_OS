export default {
  "umid": "vfs-001",
  "name": "Virtual File System",
  "version": "1.0.0",
  "description": "Unified interface layer for all filesystems",
  "moduleType": "VFS",
  "icon": "FolderTree",
  "source": "builtin",
  "targetArchitectures": [],
  "supportedExtensions": [],
  "dependencies": [
    {
      "type": "Filesystem",
      "description": "Underlying filesystem implementation",
      "optional": false,
      "requiredExtensions": ["symlink", "hard_links"]
    },
    {
      "type": "Memory Manager",
      "description": "Manages VFS cache",
      "optional": false,
      "requiredExtensions": []
    }
  ],
  "configSchema": [
    {
      "key": "maxOpenFiles",
      "label": "Max Open Files",
      "type": "number",
      "default": 1024,
      "min": 64,
      "max": 65536,
      "description": "Maximum number of simultaneously open files"
    },
    {
      "key": "cacheSizeMB",
      "label": "Cache Size (MB)",
      "type": "number",
      "default": 64,
      "min": 8,
      "max": 512,
      "description": "Size of VFS cache in megabytes"
    }
  ],
  "defaultConfig": {
    "maxOpenFiles": 1024,
    "cacheSizeMB": 64
  },
  "incompatibleWith": [],
  "hardwareRequirements": []
};
