export default {
  "umid": "mem-simple-001",
  "name": "Simple Allocator",
  "version": "1.0.0",
  "description": "Basic fixed-size block allocator for embedded systems",
  "moduleType": "Memory Manager",
  "icon": "Box",
  "source": "verified-community",
  "targetArchitectures": [],
  "supportedExtensions": [],
  "dependencies": [],
  "configSchema": [
    {
      "key": "blockSize",
      "label": "Block Size (bytes)",
      "type": "number",
      "default": 256,
      "min": 16,
      "max": 4096,
      "description": "Fixed allocation block size"
    }
  ],
  "defaultConfig": {
    "blockSize": 256,
    "poolSize": "64K"
  },
  "incompatibleWith": [],
  "hardwareRequirements": []
};
