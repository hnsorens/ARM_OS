export default {
  "umid": "sched-coop-001",
  "name": "Cooperative Scheduler",
  "version": "1.0.0",
  "description": "Simple non-preemptive scheduler where tasks yield voluntarily",
  "moduleType": "Scheduler",
  "icon": "Users",
  "source": "verified-community",
  "targetArchitectures": [],
  "supportedExtensions": [],
  "dependencies": [],
  "configSchema": [
    {
      "key": "maxTasks",
      "label": "Maximum Tasks",
      "type": "number",
      "default": 64,
      "min": 1,
      "max": 256,
      "description": "Maximum number of concurrent tasks"
    }
  ],
  "defaultConfig": {
    "maxTasks": 64
  },
  "incompatibleWith": [],
  "hardwareRequirements": []
};
