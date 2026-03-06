import type{ ModuleType, Extension, VTableFunction, ModuleDependency, ConfigOption } from '../types';

// Import all module type TS files statically
import schedulerTypeData from './module-types/scheduler';
import memoryManagerTypeData from './module-types/memory-manager';
import filesystemTypeData from './module-types/filesystem';
import vfsTypeData from './module-types/vfs';
import driverTypeData from './module-types/driver';
import ipcTypeData from './module-types/ipc';
import hardwareTypeData from './module-types/hardware';

// Import all module TS files statically
import realtimeSchedulerData from './modules/realtime-scheduler';
import cooperativeSchedulerData from './modules/cooperative-scheduler';
import pagingMemoryManagerData from './modules/paging-memory-manager';
import simpleAllocatorData from './modules/simple-allocator';
import ext4FilesystemData from './modules/ext4-filesystem';
import ext2FilesystemData from './modules/ext2-filesystem';
import fat32FilesystemData from './modules/fat32-filesystem';
import ramFilesystemData from './modules/ram-filesystem';
import vfsData from './modules/vfs';
import ahciSataDriverData from './modules/ahci-sata-driver';
import e1000EthernetDriverData from './modules/e1000-ethernet-driver';
import messageQueueIpcData from './modules/message-queue-ipc';
import sharedMemoryIpcData from './modules/shared-memory-ipc';
import apicControllerData from './modules/apic-controller';
import hpetTimerData from './modules/hpet-timer';

// Array of module type data to load
const MODULE_TYPE_DATA = [
  schedulerTypeData,
  memoryManagerTypeData,
  filesystemTypeData,
  vfsTypeData,
  driverTypeData,
  ipcTypeData,
  hardwareTypeData,
];

// Array of module data to load
const MODULE_DATA = [
  realtimeSchedulerData,
  cooperativeSchedulerData,
  pagingMemoryManagerData,
  simpleAllocatorData,
  ext4FilesystemData,
  ext2FilesystemData,
  fat32FilesystemData,
  ramFilesystemData,
  vfsData,
  ahciSataDriverData,
  e1000EthernetDriverData,
  messageQueueIpcData,
  sharedMemoryIpcData,
  apicControllerData,
  hpetTimerData,
];

// JSON structure for module type files
interface ModuleTypeJSON {
  typeName: string;
  description: string;
  layer: 'hardware' | 'kernel' | 'service' | 'application';
  vtable: VTableFunction[];
  extensions: Extension[];
}

// JSON structure for individual module files
interface ModuleJSON {
  umid: string;
  name: string;
  version: string;
  description: string;
  moduleType: string;
  icon: string;
  source: 'builtin' | 'community' | 'verified-community';
  targetArchitectures: string[];
  supportedExtensions: string[];
  dependencies: ModuleDependency[];
  configSchema: ConfigOption[];
  defaultConfig: Record<string, any>;
}

// Global storage for loaded data
let moduleTypesMap: Map<string, ModuleTypeJSON> = new Map();
let modulesMap: Map<string, ModuleJSON> = new Map();
let modulesByCategory: Record<string, ModuleType[]> = {};

/**
 * Load all module types from JSON files
 */
function loadModuleTypes(): void {
  const allJsonFiles = import.meta.glob('../../../modules/includes/**/*.json', { eager: true });
  console.log(allJsonFiles);
  const valuesArray = Object.values(allJsonFiles);
  valuesArray.forEach((data) => {
    try {
      moduleTypesMap.set(data.typeName, data);
    } catch (error) {
      console.error(`Failed to load module type:`, error);
    }
  });
}

/**
 * Load all modules from JSON files
 */
function loadModules(): void {
  const allJsonFiles = import.meta.glob('../../../modules/src/**/*.json', { eager: true });
  console.log(allJsonFiles);
  const valuesArray = Object.values(allJsonFiles);
  valuesArray.forEach((data) => {
    try {
      modulesMap.set(data.umid, data);
    } catch (error) {
      console.error(`Failed to load module:`, error);
    }
  });
}

/**
 * Convert loaded data into the ModuleType format used by the application
 */
function buildModuleCategories(): void {
  modulesByCategory = {};

  modulesMap.forEach((moduleData) => {
    const moduleTypeData = moduleTypesMap.get(moduleData.moduleType);
    if (!moduleTypeData) {
      console.error(`Module type ${moduleData.moduleType} not found for module ${moduleData.name}`);
      return;
    }

    // Build ModuleType object
    const moduleType: ModuleType = {
      type: moduleData.moduleType,
      name: moduleData.name,
      description: moduleData.description,
      icon: moduleData.icon,
      layer: moduleTypeData.layer,
      defaultConfig: moduleData.defaultConfig,
      configSchema: moduleData.configSchema,
      dependencies: moduleData.dependencies,
      vtable: moduleTypeData.vtable,
      source: moduleData.source,
      targetArchitectures: moduleData.targetArchitectures.length > 0 ? moduleData.targetArchitectures : undefined,
    };

    // Add available extensions if this module supports them
    if (moduleData.supportedExtensions.length > 0) {
      moduleType.availableExtensions = moduleData.supportedExtensions
        .map(extId => moduleTypeData.extensions.find(ext => ext.id === extId))
        .filter((ext): ext is Extension => ext !== undefined);
    }

    // Add to category
    if (!modulesByCategory[moduleData.moduleType]) {
      modulesByCategory[moduleData.moduleType] = [];
    }
    modulesByCategory[moduleData.moduleType].push(moduleType);
  });
}

/**
 * Initialize the module data system - loads all JSON files and builds the data structures
 */
export async function initializeModuleData(): Promise<void> {
  console.log('Loading module data...');
  
  // Load module types and modules synchronously
  loadModuleTypes();
  loadModules();

  // Build the final data structure
  buildModuleCategories();

  console.log(`Loaded ${moduleTypesMap.size} module types and ${modulesMap.size} modules`);
}

// Initialize data immediately when this module loads
console.log('Initializing module data at startup...');
loadModuleTypes();
loadModules();
buildModuleCategories();
console.log(`Loaded ${moduleTypesMap.size} module types and ${modulesMap.size} modules`);

/**
 * Get all module categories (used by the UI)
 */
export function getModuleCategories(): Record<string, ModuleType[]> {
  return modulesByCategory;
}

/**
 * Get a specific module type by name
 */
export function getModuleType(typeName: string): ModuleTypeJSON | undefined {
  return moduleTypesMap.get(typeName);
}

/**
 * Get a specific module by UMID
 */
export function getModule(umid: string): ModuleJSON | undefined {
  return modulesMap.get(umid);
}

/**
 * Get all module types
 */
export function getAllModuleTypes(): ModuleTypeJSON[] {
  return Array.from(moduleTypesMap.values());
}

/**
 * Get all modules
 */
export function getAllModules(): ModuleJSON[] {
  return Array.from(modulesMap.values());
}

/**
 * Get all unique target architectures from all modules
 */
export function getAllArchitectures(): string[] {
  const architectures = new Set<string>();
  
  modulesMap.forEach((moduleData) => {
    if (moduleData.targetArchitectures && moduleData.targetArchitectures.length > 0) {
      moduleData.targetArchitectures.forEach(arch => architectures.add(arch));
    }
  });
  
  return Array.from(architectures).sort();
}