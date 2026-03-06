export default {
  "umid": "fs-ext2-001",
  "name": "EXT2 Filesystem",
  "version": "1.0.0",
  "description": "Simple ext2 filesystem without journaling",
  "moduleType": "Filesystem",
  "icon": "FolderTree",
  "source": "builtin",
  "targetArchitectures": [],
  "supportedExtensions": ["symlink", "hard_links"],
  "dependencies": [
    {
      "type": "Memory Manager",
      "description": "Manages filesystem cache",
      "optional": false,
      "requiredExtensions": []
    },
    {
      "type": "Driver",
      "description": "Block device driver for disk access",
      "optional": false,
      "requiredExtensions": []
    }
  ],
  "configSchema": [
    {
      "key": "blockSize",
      "label": "Block Size",
      "type": "enum",
      "default": 4096,
      "options": ["1024", "2048", "4096"],
      "description": "Filesystem block size"
    }
  ],
  "defaultConfig": {
    "blockSize": 4096,
    "mountPoint": "/"
  },
  "incompatibleWith": [],
  "hardwareRequirements": ["Block storage device"]
};
