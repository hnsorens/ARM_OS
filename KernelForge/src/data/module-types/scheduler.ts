export default {
  "typeName": "Scheduler",
  "description": "Process scheduling and task management",
  "layer": "kernel",
  "vtable": [
    {
      "name": "init",
      "signature": "void (*init)(void)",
      "description": "Initialize scheduler",
      "required": true
    },
    {
      "name": "schedule",
      "signature": "task_t* (*schedule)(void)",
      "description": "Select next task to run",
      "required": true
    },
    {
      "name": "add_task",
      "signature": "int (*add_task)(task_t* task)",
      "description": "Add task to scheduler",
      "required": true
    },
    {
      "name": "remove_task",
      "signature": "int (*remove_task)(task_t* task)",
      "description": "Remove task from scheduler",
      "required": true
    },
    {
      "name": "yield",
      "signature": "void (*yield)(void)",
      "description": "Yield CPU voluntarily",
      "required": false
    }
  ],
  "extensions": []
};
