export default {
  "umid": "hw-hpet-001",
  "name": "HPET Timer",
  "version": "1.0.0",
  "description": "High Precision Event Timer",
  "moduleType": "Hardware",
  "icon": "Timer",
  "source": "builtin",
  "targetArchitectures": ["x86_64"],
  "supportedExtensions": [],
  "dependencies": [],
  "configSchema": [
    {
      "key": "frequency",
      "label": "Frequency (Hz)",
      "type": "number",
      "default": 1000,
      "min": 1,
      "max": 10000,
      "description": "Timer interrupt frequency"
    }
  ],
  "defaultConfig": {
    "frequency": 1000,
    "oneShot": false
  },
  "incompatibleWith": [],
  "hardwareRequirements": ["HPET"]
};
