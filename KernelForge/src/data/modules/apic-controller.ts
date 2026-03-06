export default {
  "umid": "hw-apic-001",
  "name": "APIC Controller",
  "version": "1.0.0",
  "description": "Advanced Programmable Interrupt Controller",
  "moduleType": "Hardware",
  "icon": "Zap",
  "source": "builtin",
  "targetArchitectures": ["x86_64"],
  "supportedExtensions": [],
  "dependencies": [],
  "configSchema": [
    {
      "key": "mode",
      "label": "APIC Mode",
      "type": "enum",
      "default": "symmetric",
      "options": ["symmetric", "asymmetric"],
      "description": "Interrupt distribution mode"
    }
  ],
  "defaultConfig": {
    "mode": "symmetric",
    "spuriousVector": "0xFF"
  },
  "incompatibleWith": [],
  "hardwareRequirements": ["x86 APIC"]
};
