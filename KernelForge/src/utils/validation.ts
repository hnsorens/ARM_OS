import type { Module } from '../types';
import { isModuleConfigured } from './moduleStatus';
import { getModuleCategories } from '../data/loader';

export interface ValidationResult {
  isValid: boolean;
  errors: { module: Module; message: string }[];
  warnings: { module: Module; message: string }[];
}

export function validateModules(modules: Module[], architecture: string): ValidationResult {
  const errors: { module: Module; message: string }[] = [];
  const warnings: { module: Module; message: string }[] = [];

  modules.forEach(module => {
    const configured = isModuleConfigured(module, modules);
    if (!configured) {
      errors.push({
        module,
        message: 'Missing required dependencies'
      });
    }

    // Check architecture compatibility for hardware layer modules
    if (module.layer === 'hardware') {
      // Find the module type definition
      let moduleType = null;
      for (const category of Object.values(getModuleCategories())) {
        const found = category.find(mt => mt.name === module.name);
        if (found) {
          moduleType = found;
          break;
        }
      }

      // Check if module has targetArchitectures restriction
      if (moduleType?.targetArchitectures && moduleType.targetArchitectures.length > 0) {
        if (!moduleType.targetArchitectures.includes(architecture)) {
          errors.push({
            module,
            message: `Not compatible with ${architecture} (only supports: ${moduleType.targetArchitectures.join(', ')})`
          });
        }
      }
    }

    // Check extension warnings
    // Find the module type definition
    let moduleType = null;
    for (const category of Object.values(getModuleCategories())) {
      const found = category.find(mt => mt.name === module.name);
      if (found) {
        moduleType = found;
        break;
      }
    }

    if (moduleType) {
      // Check for disabled but required extensions
      moduleType.dependencies?.forEach(dep => {
        if (dep.requiredExtensions && dep.requiredExtensions.length > 0) {
          const boundModuleId = module.dependencyBindings?.[dep.type];
          const boundModule = modules.find(m => m.id === boundModuleId);
          
          if (boundModule) {
            // Find bound module type to check available extensions
            let boundModuleType = null;
            for (const category of Object.values(getModuleCategories())) {
              const found = category.find(mt => mt.name === boundModule.name);
              if (found) {
                boundModuleType = found;
                break;
              }
            }

            if (boundModuleType) {
              const availableExtensionIds = new Set(
                (boundModuleType.availableExtensions || []).map(ext => ext.id)
              );
              const enabledExtensions = boundModule.enabledExtensions || [];

              dep.requiredExtensions.forEach(extId => {
                if (availableExtensionIds.has(extId) && !enabledExtensions.includes(extId)) {
                  warnings.push({
                    module,
                    message: `Extension '${extId}' required from ${boundModule.name} but is disabled`
                  });
                }
              });
            }
          }
        }
      });

      // Check for unused enabled extensions
      const enabledExtensions = module.enabledExtensions || [];
      if (enabledExtensions.length > 0) {
        enabledExtensions.forEach(extId => {
          // Check if any other module requires this extension from this module
          const isUsed = modules.some(otherModule => {
            if (otherModule.id === module.id) return false;

            // Find other module's type
            let otherModuleType = null;
            for (const category of Object.values(getModuleCategories())) {
              const found = category.find(mt => mt.name === otherModule.name);
              if (found) {
                otherModuleType = found;
                break;
              }
            }

            if (!otherModuleType) return false;

            // Check if other module depends on this module and requires this extension
            return otherModuleType.dependencies?.some(dep => {
              const boundModuleId = otherModule.dependencyBindings?.[dep.type];
              return boundModuleId === module.id && 
                     dep.requiredExtensions?.includes(extId);
            }) || false;
          });

          if (!isUsed) {
            warnings.push({
              module,
              message: `Extension '${extId}' is enabled but not used by any dependent module`
            });
          }
        });
      }
    }
  });

  return {
    isValid: errors.length === 0,
    errors,
    warnings
  };
}