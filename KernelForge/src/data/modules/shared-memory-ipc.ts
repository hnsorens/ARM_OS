export default {
  "umid": "ipc-shmem-001",
  "name": "Shared Memory IPC",
  "version": "1.0.0",
  "description": "Zero-copy memory region sharing between processes",
  "moduleType": "IPC",
  "icon": "Share2",
  "source": "verified-community",
  "targetArchitectures": [],
  "supportedExtensions": [],
  "dependencies": [
    {
      "type": "Memory Manager",
      "description": "Virtual memory mapping",
      "optional": false,
      "requiredExtensions": []
    }
  ],
  "configSchema": [
    {
      "key": "maxRegions",
      "label": "Max Regions",
      "type": "number",
      "default": 128,
      "min": 1,
      "max": 1024,
      "description": "Maximum shared memory regions"
    }
  ],
  "defaultConfig": {
    "maxRegions": 128,
    "permissions": "rw"
  },
  "incompatibleWith": [],
  "hardwareRequirements": ["MMU"]
};
