export default {
  "typeName": "Hardware",
  "description": "Low-level hardware controllers",
  "layer": "hardware",
  "vtable": [
    {
      "name": "init",
      "signature": "void (*init)(void)",
      "description": "Initialize hardware",
      "required": true
    },
    {
      "name": "enable",
      "signature": "void (*enable)(void)",
      "description": "Enable hardware",
      "required": true
    },
    {
      "name": "disable",
      "signature": "void (*disable)(void)",
      "description": "Disable hardware",
      "required": true
    },
    {
      "name": "read_register",
      "signature": "uint32_t (*read_register)(uint32_t offset)",
      "description": "Read hardware register",
      "required": false
    },
    {
      "name": "write_register",
      "signature": "void (*write_register)(uint32_t offset, uint32_t value)",
      "description": "Write hardware register",
      "required": false
    }
  ],
  "extensions": []
};
