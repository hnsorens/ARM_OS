export default {
  "umid": "fs-fat32-001",
  "name": "FAT32 Filesystem",
  "version": "1.0.0",
  "description": "FAT32 filesystem for compatibility with legacy systems",
  "moduleType": "Filesystem",
  "icon": "FolderTree",
  "source": "verified-community",
  "targetArchitectures": [],
  "supportedExtensions": [],
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
      "key": "mountPoint",
      "label": "Mount Point",
      "type": "string",
      "default": "/mnt",
      "description": "Where to mount the filesystem"
    }
  ],
  "defaultConfig": {
    "mountPoint": "/mnt"
  },
  "incompatibleWith": [],
  "hardwareRequirements": ["Block storage device"]
};
