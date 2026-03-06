export default {
  "umid": "ipc-msgqueue-001",
  "name": "Message Queue IPC",
  "version": "1.0.0",
  "description": "Asynchronous message passing between processes",
  "moduleType": "IPC",
  "icon": "MessageSquare",
  "source": "builtin",
  "targetArchitectures": [],
  "supportedExtensions": [],
  "dependencies": [
    {
      "type": "Memory Manager",
      "description": "Message buffer allocation",
      "optional": false,
      "requiredExtensions": []
    }
  ],
  "configSchema": [
    {
      "key": "maxQueues",
      "label": "Max Queues",
      "type": "number",
      "default": 256,
      "min": 1,
      "max": 4096,
      "description": "Maximum concurrent message queues"
    }
  ],
  "defaultConfig": {
    "maxQueues": 256,
    "maxMessageSize": 8192
  },
  "incompatibleWith": [],
  "hardwareRequirements": []
};
