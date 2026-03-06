export default {
  "umid": "sched-rt-001",
  "name": "Real-Time Scheduler",
  "version": "1.0.0",
  "description": "Priority-based scheduler with deadline guarantees for real-time systems",
  "moduleType": "Scheduler",
  "icon": "Clock",
  "source": "builtin",
  "targetArchitectures": [],
  "supportedExtensions": [],
  "dependencies": [
    {
      "type": "Memory Manager",
      "description": "Allocates memory for task structures",
      "optional": false,
      "requiredExtensions": []
    },
    {
      "type": "Hardware",
      "description": "Timer hardware for preemption",
      "optional": false,
      "requiredExtensions": []
    }
  ],
  "configSchema": [
    {
      "key": "timeSlice",
      "label": "Time Slice (ms)",
      "type": "number",
      "default": 10,
      "min": 1,
      "max": 1000,
      "description": "Time quantum for round-robin scheduling"
    },
    {
      "key": "algorithm",
      "label": "Scheduling Algorithm",
      "type": "enum",
      "default": "RR",
      "options": ["RR", "EDF", "RM", "FIFO"],
      "description": "Task scheduling strategy"
    }
  ],
  "defaultConfig": {
    "timeSlice": 10,
    "algorithm": "RR",
    "maxPriority": 255
  },
  "incompatibleWith": [],
  "hardwareRequirements": ["HPET or APIC timer"]
};
