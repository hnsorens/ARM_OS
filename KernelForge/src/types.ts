export interface Module {
  id: string;
  type: string; // e.g., "Scheduler", "Filesystem"
  name: string;
  position: { x: number; y: number };
  config: Record<string, any>;
  layer: 'hardware' | 'kernel' | 'service' | 'application';
  status: 'unconfigured' | 'configured' | 'error' | 'active';
  dependencyBindings: Record<string, string>; // Maps dependency type -> selected module ID
  vtable?: VTableFunction[];
  enabledExtensions?: string[]; // IDs of extensions that are enabled on this module
}

export interface VTableFunction {
  name: string;
  signature: string;
  description: string;
}

export interface Connection {
  id: string;
  from: { moduleId: string; portId: string };
  to: { moduleId: string; portId: string };
  type: 'data' | 'control' | 'irq' | 'memory';
}

export interface ModuleType {
  type: string; // Combined type/category: "Scheduler", "Filesystem", etc.
  name: string;
  description: string;
  icon: string;
  layer: 'hardware' | 'kernel' | 'service' | 'application';
  defaultConfig: Record<string, any>;
  configSchema: ConfigOption[];
  dependencies: ModuleDependency[]; // Module types this depends on
  vtable?: VTableFunction[];
  source?: 'builtin' | 'community' | 'verified-community'; // Module source indicator
  targetArchitectures?: string[]; // Supported architectures (for hardware modules)
  availableExtensions?: Extension[]; // Optional functionality this module type can provide
}

export interface Extension {
  id: string; // Unique identifier for the extension (e.g., "symlink", "acl", "quota")
  name: string; // Display name
  description: string; // What this extension provides
  vtable?: VTableFunction[]; // Optional vtable functions specific to this extension
}

export interface ModuleDependency {
  type: string; // Module type required (e.g., "Memory Manager")
  description: string; // Why this dependency is needed
  requiredExtensions?: string[]; // Extension IDs this module wants to use from this dependency
}

export interface ConfigOption {
  key: string;
  label: string;
  type: 'string' | 'number' | 'boolean' | 'enum';
  default: any;
  options?: string[];
  min?: number;
  max?: number;
  description: string;
}

export type Architecture = 'x86_64' | 'ARM' | 'RISC-V' | 'ARM64';