export default {
  "umid": "mem-paging-001",
  "name": "Paging Memory Manager",
  "version": "1.0.0",
  "description": "Virtual memory with page tables and demand paging",
  "moduleType": "Memory Manager",
  "icon": "HardDrive",
  "source": "builtin",
  "targetArchitectures": [],
  "supportedExtensions": [],
  "dependencies": [],
  "configSchema": [
    {
      "key": "pageSize",
      "label": "Page Size (bytes)",
      "type": "enum",
      "default": 4096,
      "options": ["4096", "2097152", "1073741824"],
      "description": "Virtual memory page size (4KB, 2MB, 1GB)"
    },
    {
      "key": "enableSwap",
      "label": "Enable Swap",
      "type": "boolean",
      "default": true,
      "description": "Allow paging to disk when RAM is full"
    }
  ],
  "defaultConfig": {
    "pageSize": 4096,
    "enableSwap": true,
    "heapSize": "512M"
  },
  "incompatibleWith": [],
  "hardwareRequirements": ["MMU"]
};
