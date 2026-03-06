# KernelForge Studio - Module Creation Guide

This guide explains how to create custom modules for KernelForge Studio by writing JSON and TypeScript files.

## Table of Contents
1. [Module System Overview](#module-system-overview)
2. [Module Type Files](#module-type-files)
3. [Module Instance Files](#module-instance-files)
4. [Step-by-Step Tutorial](#step-by-step-tutorial)
5. [Field Reference](#field-reference)

---

## Module System Overview

KernelForge uses a two-tier module system:

1. **Module Type Files** - Define categories of modules (e.g., "Scheduler", "Filesystem", "Driver")
   - Define the vtable (virtual function table) that all modules of this type must implement
   - Define available extensions (optional features)
   - Stored in `/data/module-types/`

2. **Module Instance Files** - Define specific implementations of a module type
   - Define configuration options, dependencies, and hardware requirements
   - Stored in `/data/modules/`

**Example:** The "Filesystem" module type defines what a filesystem can do. The "EXT4 Filesystem" module is a specific implementation that users can add to their OS.

---

## Module Type Files

Module type files define categories like "Scheduler", "Memory Manager", "Filesystem", etc.

### Location
`/data/module-types/your-type-name.ts`

### File Format

```typescript
export default {
  "typeName": "YourTypeName",
  "description": "Brief description of what this module type does",
  "layer": "kernel",
  "vtable": [
    // Virtual functions - see below
  ],
  "extensions": [
    // Optional features - see below
  ]
};
```

### Field Reference

#### `typeName` (string, required)
The unique identifier for this module type. Must exactly match the `moduleType` field in module instances.

**Examples:** `"Scheduler"`, `"Memory Manager"`, `"Filesystem"`, `"Driver"`, `"IPC"`, `"Hardware"`

#### `description` (string, required)
Short description of what modules of this type do.

**Example:** `"File and directory operations"`

#### `layer` (string, required)
The software layer where this module type operates.

**Options:**
- `"hardware"` - Direct hardware interaction (timers, interrupt controllers)
- `"kernel"` - Core OS functionality (schedulers, memory managers)
- `"service"` - System services (filesystems, IPC mechanisms)
- `"application"` - User-space applications

#### `vtable` (array, required)
Array of virtual functions that modules of this type must implement. Each function is an object with:

```typescript
{
  "name": "function_name",
  "signature": "return_type (*function_name)(parameters)",
  "description": "What this function does",
  "required": true | false
}
```

**Fields:**
- `name` - Function name (e.g., `"init"`, `"read"`, `"write"`)
- `signature` - C function pointer signature
- `description` - What the function does
- `required` - Whether this function must be implemented (true) or is optional (false)

**Example:**
```typescript
{
  "name": "read",
  "signature": "ssize_t (*read)(file_t* file, void* buf, size_t count)",
  "description": "Read data from file",
  "required": true
}
```

#### `extensions` (array, optional)
Optional features that modules of this type can provide. Each extension is an object:

```typescript
{
  "id": "extension_id",
  "name": "Display Name",
  "description": "What this extension provides",
  "vtable": [
    // Additional functions required for this extension
  ]
}
```

**Fields:**
- `id` - Unique identifier (e.g., `"symlink"`, `"quota"`)
- `name` - Display name shown in UI (e.g., `"Symbolic Links"`)
- `description` - What functionality this adds
- `vtable` - Array of additional functions (same format as module type vtable)

**Example:**
```typescript
{
  "id": "quota",
  "name": "Disk Quotas",
  "description": "Per-user disk usage limits",
  "vtable": [
    {
      "name": "get_quota",
      "signature": "quota_t* (*get_quota)(uid_t uid)",
      "description": "Get user disk quota",
      "required": true
    }
  ]
}
```

---

## Module Instance Files

Module instance files define specific implementations that users can drag onto the canvas.

### Location
Create two files in `/data/modules/`:
- `your-module-name.json` - Module definition
- `your-module-name.ts` - TypeScript export of the JSON

### JSON File Format

```json
{
  "umid": "unique-module-id",
  "name": "Display Name",
  "version": "1.0.0",
  "description": "What this module does",
  "moduleType": "TypeName",
  "icon": "IconName",
  "source": "builtin",
  "targetArchitectures": [],
  "supportedExtensions": [],
  "dependencies": [],
  "configSchema": [],
  "defaultConfig": {},
  "incompatibleWith": [],
  "hardwareRequirements": []
}
```

### TypeScript File Format

```typescript
export default {
  "umid": "unique-module-id",
  "name": "Display Name",
  // ... same as JSON ...
};
```

---

## Field Reference

### `umid` (string, required)
**Unique Module ID** - Must be globally unique across all modules.

**Format:** `prefix-name-number`
- Prefix examples: `sched-` (scheduler), `fs-` (filesystem), `drv-` (driver), `mem-` (memory), `ipc-` (IPC), `hw-` (hardware)

**Examples:**
- `"sched-rt-001"` - Real-Time Scheduler
- `"fs-ext4-001"` - EXT4 Filesystem
- `"drv-ahci-001"` - AHCI SATA Driver
- `"mem-paging-001"` - Paging Memory Manager

### `name` (string, required)
Display name shown in the UI when users browse modules.

**Examples:**
- `"Real-Time Scheduler"`
- `"EXT4 Filesystem"`
- `"AHCI SATA Driver"`

### `version` (string, required)
Module version in semantic versioning format.

**Format:** `"MAJOR.MINOR.PATCH"`

**Example:** `"1.0.0"`, `"2.1.3"`

### `description` (string, required)
Detailed description of what this module does and its key features.

**Example:** `"Priority-based scheduler with deadline guarantees for real-time systems"`

### `moduleType` (string, required)
Must exactly match the `typeName` from a module type file.

**Examples:** `"Scheduler"`, `"Filesystem"`, `"Memory Manager"`, `"Driver"`, `"IPC"`, `"Hardware"`

### `icon` (string, required)
Icon name from the [Lucide React](https://lucide.dev/) icon library.

**Common Icons:**
- Schedulers: `"Clock"`, `"Timer"`, `"Zap"`
- Filesystems: `"FolderTree"`, `"HardDrive"`, `"Database"`
- Memory: `"MemoryStick"`, `"Cpu"`, `"Binary"`
- Drivers: `"HardDrive"`, `"Wifi"`, `"Usb"`
- IPC: `"MessageSquare"`, `"Mail"`, `"Share2"`
- Hardware: `"Cpu"`, `"Zap"`, `"Activity"`

### `source` (string, required)
Indicates the origin/trust level of the module.

**Options:**
- `"builtin"` - Official module included with KernelForge
- `"verified-community"` - Community module that has been verified
- `"community"` - Unverified community contribution

### `targetArchitectures` (array, optional)
List of CPU architectures this module supports. Empty array means all architectures.

**Options:**
- `"x86_64"` - Intel/AMD 64-bit
- `"x86"` - Intel/AMD 32-bit
- `"aarch64"` - ARM 64-bit
- `"arm"` - ARM 32-bit
- `"riscv64"` - RISC-V 64-bit
- `"riscv32"` - RISC-V 32-bit

**Examples:**
- `[]` - Works on all architectures
- `["x86_64", "aarch64"]` - Only 64-bit Intel/ARM
- `["x86_64"]` - Intel/AMD only

**Note:** If a module is architecture-specific, KernelForge will show a warning if the user selects an incompatible architecture.

### `supportedExtensions` (array, optional)
List of extension IDs from the module type that this module supports.

**Format:** Array of extension `id` values from the module type's `extensions` array.

**Example for Filesystem:**
```json
"supportedExtensions": ["symlink", "hard_links", "acl", "quota"]
```

This means:
- This filesystem CAN support symbolic links, hard links, ACLs, and quotas
- Users can enable/disable each extension in the property editor
- When enabled, the module must implement the extension's vtable functions

**Example for Simple Filesystem:**
```json
"supportedExtensions": []
```
This filesystem doesn't support any extensions.

### `dependencies` (array, required)
Other module types that this module needs to function. Each dependency is an object:

```json
{
  "type": "Module Type Name",
  "description": "Why this dependency is needed",
  "optional": false,
  "requiredExtensions": []
}
```

**Fields:**
- `type` - Name of the required module type (must match a `typeName`)
- `description` - Explains why this dependency is needed
- `optional` - If `true`, module works without it; if `false`, it's required
- `requiredExtensions` - Array of extension IDs that must be enabled on the dependency

**Examples:**

```json
{
  "type": "Memory Manager",
  "description": "Allocates memory for task structures",
  "optional": false,
  "requiredExtensions": []
}
```

```json
{
  "type": "Filesystem",
  "description": "Underlying filesystem for quota tracking",
  "optional": false,
  "requiredExtensions": ["quota"]
}
```

**How Dependencies Work:**
1. User adds module to canvas
2. KernelForge shows dropdown for each dependency
3. User selects which specific module instance fulfills each dependency
4. Dependencies are validated before building

### `configSchema` (array, required)
Defines configuration variables that become `#define` values during compilation. Each config option is an object:

```json
{
  "key": "variableName",
  "label": "Display Label",
  "type": "boolean",
  "default": true,
  "description": "What this setting does"
}
```

**Common Fields:**
- `key` - Variable name (will become `#define KEY value`)
- `label` - Display name in property editor
- `type` - Data type (see below)
- `default` - Default value
- `description` - Explanation of what this setting does

**Type Options:**

#### `"boolean"` - True/false toggle
```json
{
  "key": "journaling",
  "label": "Enable Journaling",
  "type": "boolean",
  "default": true,
  "description": "Crash recovery via write-ahead logging"
}
```

#### `"number"` - Numeric value with optional min/max
```json
{
  "key": "timeSlice",
  "label": "Time Slice (ms)",
  "type": "number",
  "default": 10,
  "min": 1,
  "max": 1000,
  "description": "Time quantum for round-robin scheduling"
}
```

Additional fields for numbers:
- `min` - Minimum allowed value
- `max` - Maximum allowed value

#### `"string"` - Text input
```json
{
  "key": "mountPoint",
  "label": "Mount Point",
  "type": "string",
  "default": "/",
  "description": "Where to mount this filesystem"
}
```

#### `"enum"` - Dropdown selection
```json
{
  "key": "blockSize",
  "label": "Block Size",
  "type": "enum",
  "default": 4096,
  "options": ["1024", "2048", "4096"],
  "description": "Filesystem block size in bytes"
}
```

**Note:** `options` array values are always strings, even for numbers. The UI will display them as-is.

### `defaultConfig` (object, required)
Default values for all configuration variables.

**Format:** Key-value pairs where keys match `configSchema` keys.

**Example:**
```json
"defaultConfig": {
  "journaling": true,
  "blockSize": 4096,
  "mountPoint": "/"
}
```

**Rules:**
- Must include all keys from `configSchema`
- Can include additional keys not in `configSchema` (for internal use)
- Values must match the types defined in `configSchema`

### `incompatibleWith` (array, required)
List of module names that cannot be used together with this module.

**Example:**
```json
"incompatibleWith": ["FAT32 Filesystem", "EXT2 Filesystem"]
```

This prevents users from adding conflicting modules to the same OS design.

**Use Cases:**
- Multiple filesystems that handle the same mount point
- Schedulers that conflict with each other
- Drivers for the same hardware

**Note:** Can be empty array `[]` if no conflicts exist.

### `hardwareRequirements` (array, required)
Human-readable list of hardware this module needs.

**Examples:**
```json
"hardwareRequirements": ["AHCI controller"]
```

```json
"hardwareRequirements": ["HPET or APIC timer"]
```

```json
"hardwareRequirements": ["Block storage device"]
```

```json
"hardwareRequirements": []
```

**Note:** This is informational only - not validated by KernelForge.

---

## Step-by-Step Tutorial

Let's create a new "Round Robin Scheduler" module from scratch.

### Step 1: Create the Module Type (if it doesn't exist)

If "Scheduler" doesn't exist, create `/data/module-types/scheduler.ts`:

```typescript
export default {
  "typeName": "Scheduler",
  "description": "Task scheduling and process management",
  "layer": "kernel",
  "vtable": [
    {
      "name": "init",
      "signature": "void (*init)(void)",
      "description": "Initialize the scheduler",
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
      "description": "Add task to ready queue",
      "required": true
    },
    {
      "name": "remove_task",
      "signature": "int (*remove_task)(task_t* task)",
      "description": "Remove task from ready queue",
      "required": true
    }
  ],
  "extensions": [
    {
      "id": "priority",
      "name": "Priority Scheduling",
      "description": "Support for task priorities",
      "vtable": [
        {
          "name": "set_priority",
          "signature": "int (*set_priority)(task_t* task, int priority)",
          "description": "Set task priority",
          "required": true
        }
      ]
    }
  ]
};
```

### Step 2: Create the Module JSON

Create `/data/modules/round-robin-scheduler.json`:

```json
{
  "umid": "sched-rr-001",
  "name": "Round Robin Scheduler",
  "version": "1.0.0",
  "description": "Fair time-sharing scheduler that rotates between tasks",
  "moduleType": "Scheduler",
  "icon": "Clock",
  "source": "community",
  "targetArchitectures": [],
  "supportedExtensions": ["priority"],
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
      "max": 100,
      "description": "How long each task runs before switching"
    },
    {
      "key": "preemptive",
      "label": "Preemptive",
      "type": "boolean",
      "default": true,
      "description": "Forcibly switch tasks when time slice expires"
    },
    {
      "key": "maxTasks",
      "label": "Maximum Tasks",
      "type": "number",
      "default": 256,
      "min": 1,
      "max": 65536,
      "description": "Maximum number of concurrent tasks"
    }
  ],
  "defaultConfig": {
    "timeSlice": 10,
    "preemptive": true,
    "maxTasks": 256
  },
  "incompatibleWith": [],
  "hardwareRequirements": ["Timer (HPET, PIT, or APIC)"]
}
```

### Step 3: Create the Module TypeScript File

Create `/data/modules/round-robin-scheduler.ts`:

```typescript
export default {
  "umid": "sched-rr-001",
  "name": "Round Robin Scheduler",
  "version": "1.0.0",
  "description": "Fair time-sharing scheduler that rotates between tasks",
  "moduleType": "Scheduler",
  "icon": "Clock",
  "source": "community",
  "targetArchitectures": [],
  "supportedExtensions": ["priority"],
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
      "max": 100,
      "description": "How long each task runs before switching"
    },
    {
      "key": "preemptive",
      "label": "Preemptive",
      "type": "boolean",
      "default": true,
      "description": "Forcibly switch tasks when time slice expires"
    },
    {
      "key": "maxTasks",
      "label": "Maximum Tasks",
      "type": "number",
      "default": 256,
      "min": 1,
      "max": 65536,
      "description": "Maximum number of concurrent tasks"
    }
  ],
  "defaultConfig": {
    "timeSlice": 10,
    "preemptive": true,
    "maxTasks": 256
  },
  "incompatibleWith": [],
  "hardwareRequirements": ["Timer (HPET, PIT, or APIC)"]
};
```

### Step 4: Register the Module

Add your module to `/data/loader.ts`:

```typescript
// Add import at top
import roundRobinSchedulerData from './modules/round-robin-scheduler';

// Add to MODULE_DATA array
const MODULE_DATA = [
  // ... existing modules ...
  roundRobinSchedulerData,
];
```

### Step 5: Test Your Module

1. Restart KernelForge Studio
2. Open the Module Library sidebar
3. Look for "Round Robin Scheduler" under the Scheduler category
4. Drag it onto the canvas
5. Configure its properties
6. Set up its dependencies
7. Build your OS!

---

## Best Practices

### Naming Conventions
- **UMID:** Use descriptive prefixes (`sched-`, `fs-`, `drv-`, `mem-`, `ipc-`, `hw-`)
- **Names:** Use clear, descriptive names (e.g., "Real-Time Scheduler" not "RTS")
- **Config Keys:** Use camelCase (e.g., `timeSlice`, not `time_slice`)

### Dependencies
- Always specify `optional: false` for required dependencies
- Use clear descriptions explaining why each dependency is needed
- Only use `requiredExtensions` if you actually call those extension functions

### Configuration
- Provide sensible default values
- Use `min`/`max` for numbers to prevent invalid values
- Write helpful descriptions - users will read them!

### Extensions
- Only list extensions your module actually implements
- Extensions add vtable functions - make sure you can implement them
- Users can enable/disable extensions, so plan for both scenarios

### Compatibility
- Set `targetArchitectures` only if your module has architecture-specific code
- List incompatible modules to prevent conflicts
- Be specific with hardware requirements

### Documentation
- Write clear descriptions for modules, config options, and dependencies
- Explain what each vtable function does
- Document any special requirements or limitations

---

## Common Mistakes

### ❌ UMID Conflicts
```json
"umid": "001"  // Too generic - will conflict!
```

✅ Use descriptive, unique IDs:
```json
"umid": "sched-roundrobin-001"
```

### ❌ Module Type Mismatch
```json
"moduleType": "scheduler"  // Lowercase!
```

✅ Match exactly:
```json
"moduleType": "Scheduler"
```

### ❌ Missing Config Defaults
```json
"configSchema": [
  { "key": "timeSlice", ... }
],
"defaultConfig": {
  // Missing timeSlice!
}
```

✅ Include all config keys:
```json
"defaultConfig": {
  "timeSlice": 10
}
```

### ❌ Wrong Enum Format
```json
"options": [1024, 2048, 4096]  // Numbers!
```

✅ Always use strings:
```json
"options": ["1024", "2048", "4096"]
```

### ❌ Unsupported Extension
```json
"supportedExtensions": ["quota"]
```
But the module type doesn't have a "quota" extension defined!

✅ Only reference extensions from the module type:
```json
"supportedExtensions": ["priority"]
```

---

## Need Help?

- Check existing modules in `/data/modules/` for examples
- Look at module types in `/data/module-types/` for vtable and extension definitions
- The console will show errors if module loading fails

Happy module building! 🚀
