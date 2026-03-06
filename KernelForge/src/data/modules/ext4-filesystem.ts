export default {
  "umid": "fs-ext4-001",
  "name": "EXT4 Filesystem",
  "version": "1.0.0",
  "description": "Modern journaling filesystem with extent-based allocation",
  "moduleType": "Filesystem",
  "icon": "FolderTree",
  "source": "builtin",
  "targetArchitectures": [],
  "supportedExtensions": ["symlink", "hard_links", "acl", "quota"],
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
      "key": "journaling",
      "label": "Enable Journaling",
      "type": "boolean",
      "default": true,
      "description": "Crash recovery via write-ahead logging"
    },
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
    "journaling": true,
    "blockSize": 4096,
    "mountPoint": "/"
  },
  "incompatibleWith": [],
  "hardwareRequirements": ["Block storage device"]
};
