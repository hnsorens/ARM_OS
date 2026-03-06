export default {
  "umid": "drv-e1000-001",
  "name": "E1000 Ethernet Driver",
  "version": "1.0.0",
  "description": "Intel E1000 series network interface driver",
  "moduleType": "Driver",
  "icon": "Network",
  "source": "verified-community",
  "targetArchitectures": ["x86_64"],
  "supportedExtensions": [],
  "dependencies": [
    {
      "type": "Memory Manager",
      "description": "Packet buffer allocation",
      "optional": false,
      "requiredExtensions": []
    }
  ],
  "configSchema": [
    {
      "key": "mtu",
      "label": "MTU Size",
      "type": "number",
      "default": 1500,
      "min": 576,
      "max": 9000,
      "description": "Maximum transmission unit"
    }
  ],
  "defaultConfig": {
    "mtu": 1500,
    "checksumOffload": true
  },
  "incompatibleWith": [],
  "hardwareRequirements": ["Intel E1000 NIC"]
};
