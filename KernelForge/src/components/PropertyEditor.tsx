import { useMemo } from 'react';
import { type Module } from '../types';
import { CheckCircle, AlertTriangle, Check, X } from 'lucide-react';
import * as Icons from 'lucide-react';
import { getModuleCategories } from '../data/loader';
import { isModuleConfigured } from '../utils/moduleStatus';

interface PropertyEditorProps {
  module: Module;
  onClose: () => void;
  onUpdateModule: (id: string, updates: Partial<Module>) => void;
  allModules: Module[];
}

export function PropertyEditor({ module, onClose, onUpdateModule, allModules }: PropertyEditorProps) {
  const moduleType = useMemo(() => {
    for (const modules of Object.values(getModuleCategories())) {
      const found = modules.find(mt => mt.name === module.name);
      if (found) return found;
    }
    return null;
  }, [module.name]);

  if (!moduleType) return null;

  // Helper function to get module type for a module instance
  const getModuleTypeForModule = (moduleInstance: Module) => {
    for (const modules of Object.values(getModuleCategories())) {
      const found = modules.find(mt => mt.name === moduleInstance.name);
      if (found) return found;
    }
    return null;
  };

  const handleConfigChange = (key: string, value: any) => {
    onUpdateModule(module.id, {
      config: { ...module.config, [key]: value },
    });
  };

  const handleDependencyBinding = (depType: string, moduleId: string) => {
    onUpdateModule(module.id, {
      dependencyBindings: { ...module.dependencyBindings, [depType]: moduleId },
    });
  };

  const handleExtensionToggle = (extId: string, enabled: boolean) => {
    const currentExtensions = module.enabledExtensions || [];
    const newExtensions = enabled
      ? [...currentExtensions, extId]
      : currentExtensions.filter(id => id !== extId);
    
    onUpdateModule(module.id, {
      enabledExtensions: newExtensions,
    });
  };

  // Check if all required dependencies are bound
  const isFullyConfigured = useMemo(() => {
    return isModuleConfigured(module, allModules);
  }, [module, allModules]);

  return (
    <div className="w-80 bg-zinc-900 border-l border-zinc-800 flex flex-col overflow-hidden select-none">
      {/* Header */}
      <div className="px-4 py-3 border-b border-zinc-800 flex items-center justify-between">
        <div className="flex items-center gap-2">
          <Icons.Settings className="size-5 text-green-400" />
          <h2 className="font-semibold">Properties</h2>
        </div>
      </div>

      {/* Content */}
      <div className="flex-1 overflow-y-auto">
        {/* Module Info */}
        <div className="px-4 py-3 border-b border-zinc-800">
          <h3 className="text-sm font-semibold mb-2">{moduleType.name}</h3>
          <p className="text-xs text-zinc-400 mb-3">{moduleType.description}</p>
          
          <div className="space-y-2">
            <div className="flex items-center gap-2 text-xs">
              <Icons.Layers className="size-3 text-zinc-500" />
              <span className="text-zinc-500">Layer:</span>
              <span className="text-zinc-300">{moduleType.layer}</span>
            </div>
            <div className="flex items-center gap-2 text-xs">
              <Icons.Package className="size-3 text-zinc-500" />
              <span className="text-zinc-500">Type:</span>
              <span className="text-zinc-300">{moduleType.type}</span>
            </div>
            <div className="flex items-center gap-2 text-xs">
              <Icons.Code className="size-3 text-zinc-500" />
              <span className="text-zinc-500">ID:</span>
              <span className="text-zinc-300 font-mono text-xs">{module.id}</span>
            </div>
          </div>
        </div>

        {/* Dependencies */}
        {moduleType.dependencies.length > 0 && (
          <div className="px-4 py-3 border-b border-zinc-800">
            <h4 className="text-sm font-semibold mb-3 flex items-center gap-2">
              <Icons.Link className="size-4" />
              Dependencies (Inputs)
            </h4>
            <p className="text-xs text-zinc-500 mb-3">
              Select which module instances fulfill each dependency. These are the modules whose vtable functions this module will call.
            </p>

            <div className="space-y-3">
              {moduleType.dependencies.map(dep => {
                const availableModules = allModules.filter(m => 
                  m.type === dep.type && m.id !== module.id
                );
                const selectedModuleId = module.dependencyBindings?.[dep.type];
                const selectedModule = allModules.find(m => m.id === selectedModuleId);
                const selectedModuleType = selectedModule ? getModuleTypeForModule(selectedModule) : null;
                
                // Get available extensions from the selected module
                const availableExtensions = selectedModuleType?.availableExtensions || [];
                const availableExtensionIds = new Set(availableExtensions.map(ext => ext.id));

                return (
                  <div key={dep.type} className="space-y-1">
                    <label className="text-xs font-medium text-zinc-300 flex items-center justify-between">
                      <span>{dep.type}</span>
                    </label>
                    <p className="text-[10px] text-zinc-500 mb-1">{dep.description}</p>
                    
                    <select
                      value={selectedModuleId || ''}
                      onChange={(e) => handleDependencyBinding(dep.type, e.target.value)}
                      className="w-full px-3 py-1.5 bg-zinc-800 border border-zinc-700 rounded text-sm focus:outline-none focus:ring-2 focus:ring-blue-500"
                    >
                      <option value="">-- Select {dep.type} --</option>
                      {availableModules.map(m => (
                        <option key={m.id} value={m.id}>
                          {m.name} ({m.layer})
                        </option>
                      ))}
                    </select>

                    {selectedModule && (
                      <div className="space-y-1">
                        <div className="flex items-center gap-2 mt-1 p-2 bg-zinc-800 rounded text-xs">
                          <CheckCircle className="size-3 text-green-500" />
                          <span className="text-zinc-400">Bound to: {selectedModule.name}</span>
                        </div>
                        
                        {/* Extension Compatibility */}
                        {dep.requiredExtensions && dep.requiredExtensions.length > 0 && (
                          <div className="mt-2 p-2 bg-zinc-900 border border-zinc-700 rounded">
                            <p className="text-[10px] text-zinc-500 mb-1.5">Extension Compatibility:</p>
                            <div className="space-y-1">
                              {dep.requiredExtensions.map(extId => {
                                const isAvailable = availableExtensionIds.has(extId);
                                const isEnabled = (selectedModule.enabledExtensions || []).includes(extId);
                                const extension = availableExtensions.find(e => e.id === extId);
                                
                                return (
                                  <div key={extId} className="flex items-center gap-1.5 text-[11px]">
                                    {isAvailable && isEnabled ? (
                                      <>
                                        <Check className="size-3 text-green-500 flex-shrink-0" />
                                        <span className="text-green-400">{extId}</span>
                                        {extension && (
                                          <span className="text-zinc-600">- enabled</span>
                                        )}
                                      </>
                                    ) : isAvailable && !isEnabled ? (
                                      <>
                                        <AlertTriangle className="size-3 text-yellow-500 flex-shrink-0" />
                                        <span className="text-yellow-400">{extId}</span>
                                        <span className="text-zinc-600">- disabled (needs enabling)</span>
                                      </>
                                    ) : (
                                      <>
                                        <X className="size-3 text-red-500 flex-shrink-0" />
                                        <span className="text-red-400">{extId}</span>
                                        <span className="text-zinc-600">- not supported</span>
                                      </>
                                    )}
                                  </div>
                                );
                              })}
                            </div>
                          </div>
                        )}
                      </div>
                    )}

                    {!selectedModuleId && (
                      <div className="flex items-center gap-2 mt-1 p-2 bg-red-900/20 border border-red-700/30 rounded text-xs">
                        <AlertTriangle className="size-3 text-red-500" />
                        <span className="text-red-400">Required dependency not set</span>
                      </div>
                    )}

                    {availableModules.length === 0 && (
                      <div className="flex items-center gap-2 mt-1 p-2 bg-red-900/20 border border-red-700/30 rounded text-xs">
                        <AlertTriangle className="size-3 text-red-500" />
                        <span className="text-red-400">No {dep.type} modules in design</span>
                      </div>
                    )}
                  </div>
                );
              })}
            </div>
          </div>
        )}

        {/* Module Type Output */}
        <div className="px-4 py-3 border-b border-zinc-800">
          <h4 className="text-sm font-semibold mb-3 flex items-center gap-2">
            <Icons.Package className="size-4" />
            Module Output
          </h4>
          <p className="text-xs text-zinc-500 mb-3">
            This module provides a <span className="text-green-400 font-semibold">{moduleType.type}</span> interface that other modules can depend on.
          </p>
          <div className="p-3 bg-green-900/20 border border-green-700/30 rounded">
            <div className="flex items-center gap-2 text-sm">
              <div className="w-3 h-3 rounded-full bg-green-500" />
              <span className="text-green-300 font-medium">{moduleType.type}</span>
            </div>
          </div>
        </div>

        {/* Configuration */}
        <div className="px-4 py-3 border-b border-zinc-800">
          <h4 className="text-sm font-semibold mb-3 flex items-center gap-2">
            <Icons.Settings className="size-4" />
            Configuration
          </h4>
          <p className="text-xs text-zinc-500 mb-3">
            These values will be #defined when compiling this module
          </p>

          <div className="space-y-3">
            {moduleType.configSchema.map(option => (
              <div key={option.key} className="space-y-1">
                <label className="text-xs font-medium text-zinc-300">
                  {option.label}
                </label>
                
                {option.type === 'string' && (
                  <input
                    type="text"
                    value={module.config[option.key] || option.default}
                    onChange={(e) => handleConfigChange(option.key, e.target.value)}
                    className="w-full px-3 py-1.5 bg-zinc-800 border border-zinc-700 rounded text-sm focus:outline-none focus:ring-2 focus:ring-blue-500"
                  />
                )}

                {option.type === 'number' && (
                  <input
                    type="number"
                    value={module.config[option.key] || option.default}
                    onChange={(e) => handleConfigChange(option.key, Number(e.target.value))}
                    min={option.min}
                    max={option.max}
                    className="w-full px-3 py-1.5 bg-zinc-800 border border-zinc-700 rounded text-sm focus:outline-none focus:ring-2 focus:ring-blue-500"
                  />
                )}

                {option.type === 'boolean' && (
                  <label className="flex items-center gap-2 cursor-pointer">
                    <input
                      type="checkbox"
                      checked={module.config[option.key] ?? option.default}
                      onChange={(e) => handleConfigChange(option.key, e.target.checked)}
                      className="w-4 h-4 bg-zinc-800 border border-zinc-700 rounded focus:ring-2 focus:ring-blue-500"
                    />
                    <span className="text-sm text-zinc-400">Enabled</span>
                  </label>
                )}

                {option.type === 'enum' && (
                  <select
                    value={module.config[option.key] || option.default}
                    onChange={(e) => handleConfigChange(option.key, e.target.value)}
                    className="w-full px-3 py-1.5 bg-zinc-800 border border-zinc-700 rounded text-sm focus:outline-none focus:ring-2 focus:ring-blue-500"
                  >
                    {option.options?.map(opt => (
                      <option key={opt} value={opt}>{opt}</option>
                    ))}
                  </select>
                )}

                {option.description && (
                  <p className="text-xs text-zinc-500">{option.description}</p>
                )}
              </div>
            ))}
          </div>
        </div>

        {/* Extensions */}
        {moduleType.availableExtensions && moduleType.availableExtensions.length > 0 && (
          <div className="px-4 py-3 border-b border-zinc-800">
            <h4 className="text-sm font-semibold mb-3 flex items-center gap-2">
              <Icons.Puzzle className="size-4" />
              Extensions
            </h4>
            <p className="text-xs text-zinc-500 mb-3">
              Enable optional functionality for this module. Extensions provide additional vtable functions.
            </p>

            <div className="space-y-3">
              {moduleType.availableExtensions.map(ext => {
                const isEnabled = (module.enabledExtensions || []).includes(ext.id);
                
                return (
                  <div key={ext.id} className="space-y-1">
                    <label className="flex items-start gap-2 cursor-pointer group">
                      <input
                        type="checkbox"
                        checked={isEnabled}
                        onChange={(e) => handleExtensionToggle(ext.id, e.target.checked)}
                        className="mt-0.5 w-4 h-4 bg-zinc-800 border border-zinc-700 rounded focus:ring-2 focus:ring-blue-500"
                      />
                      <div className="flex-1">
                        <div className="flex items-center gap-2">
                          <span className="text-sm font-medium text-zinc-300">{ext.name}</span>
                          <span className="px-1.5 py-0.5 bg-purple-500/20 border border-purple-500/40 rounded text-[10px] text-purple-300">
                            {ext.id}
                          </span>
                        </div>
                        <p className="text-xs text-zinc-500 mt-0.5">{ext.description}</p>
                        
                        {/* Show vtable functions if enabled */}
                        {isEnabled && ext.vtable && ext.vtable.length > 0 && (
                          <div className="mt-2 p-2 bg-zinc-950 border border-zinc-800 rounded">
                            <p className="text-[10px] text-zinc-600 mb-1.5 flex items-center gap-1">
                              <Icons.Code className="size-2.5" />
                              Functions ({ext.vtable.length})
                            </p>
                            <div className="space-y-1.5">
                              {ext.vtable.map((func, i) => (
                                <div key={i} className="text-[10px]">
                                  <code className="text-green-500 font-mono break-all block leading-tight">
                                    {func.signature}
                                  </code>
                                  <p className="text-zinc-600 mt-0.5">{func.description}</p>
                                </div>
                              ))}
                            </div>
                          </div>
                        )}
                      </div>
                    </label>
                  </div>
                );
              })}
            </div>
          </div>
        )}

        {/* VTable */}
      </div>

      {/* Footer */}
      <div className="px-4 py-3 border-t border-zinc-800 bg-zinc-900">
        <div className="flex items-center gap-2">
          {isFullyConfigured ? (
            <>
              <CheckCircle className="size-4 text-green-500" />
              <span className="text-xs text-green-500">Configured</span>
            </>
          ) : (
            <>
              <AlertTriangle className="size-4 text-yellow-500" />
              <span className="text-xs text-yellow-500">Needs configuration</span>
            </>
          )}
        </div>
      </div>
    </div>
  );
}