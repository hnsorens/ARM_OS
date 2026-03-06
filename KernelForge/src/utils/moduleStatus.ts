import type { Module } from '../types';
import { getModuleCategories } from '../data/loader';

export function isModuleConfigured(module: Module, allModules: Module[]): boolean {
  // Find the module type definition
  let moduleType = null;
  for (const modules of Object.values(getModuleCategories())) {
    const found = modules.find(mt => mt.name === module.name);
    if (found) {
      moduleType = found;
      break;
    }
  }

  if (!moduleType) return false;

  // Check if all required dependencies are bound
  const requiredDeps = moduleType.dependencies;
  return requiredDeps.every(dep => {
    const boundModuleId = module.dependencyBindings?.[dep.type];
    if (!boundModuleId) return false;
    // Also check that the bound module actually exists
    return allModules.some(m => m.id === boundModuleId);
  });
}