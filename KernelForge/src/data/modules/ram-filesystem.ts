export default {
  "umid": "fs-ram-001",
  "name": "RAM Filesystem",
  "version": "1.0.0",
  "description": "In-memory filesystem for temporary storage",
  "moduleType": "Filesystem",
  "icon": "Zap",
  "source": "community",
  "targetArchitectures": [],
  "supportedExtensions": ["symlink"],
  "dependencies": [
    {
      "type": "Memory Manager",
      "description": "Allocates RAM for filesystem",
      "optional": false,
      "requiredExtensions": []
    }
  ],
  "configSchema": [
    {
      "key": "maxSize",
      "label": "Maximum Size",
      "type": "string",
      "default": "128M",
      "description": "Maximum RAM allocation"
    }
  ],
  "defaultConfig": {
    "maxSize": "128M",
    "mountPoint": "/tmp"
  },
  "incompatibleWith": [],
  "hardwareRequirements": []
};
