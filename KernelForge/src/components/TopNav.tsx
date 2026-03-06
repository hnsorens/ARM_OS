import { Play, Pause, Bug, Settings, Save, FolderOpen, Share2, Cpu, HardDrive, Layers, Zap, Upload } from 'lucide-react';
import { type Module, type Connection } from '../types';
import { Logo } from './Logo';
import { ArchitectureSelector } from './ArchitectureSelector';
import { validateModules } from '../utils/validation';
import { ToastManager } from './Toast';
import { useState, useRef } from 'react';
import { getModuleCategories } from '../data/loader';

interface TopNavProps {
  modules: Module[];
  connections: Connection[];
  architecture: string;
  setArchitecture: (arch: string) => void;
  processor: string;
  setProcessor: (proc: string) => void;
  onArchitectureChange: (architecture: string, processor: string) => void;
  onValidate: () => void;
  onExport: () => void;
  onImport: (modules: Module[], connections: Connection[], architecture: string, processor: string) => void;
  onTemplateSelect: () => void;
  modulesCount: number;
}

export function TopNav({
  modules,
  connections,
  architecture,
  setArchitecture,
  processor,
  setProcessor,
  onArchitectureChange,
  onValidate,
  onExport,
  onImport,
  onTemplateSelect,
  modulesCount,
}: TopNavProps) {
  const [showArchSelector, setShowArchSelector] = useState(false);
  const fileInputRef = useRef<HTMLInputElement>(null);
  
  const handleFileChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = (e) => {
      try {
        const content = e.target?.result;
        if (typeof content !== 'string') {
          throw new Error('Invalid file content');
        }

        const data = JSON.parse(content);

        // Validate the JSON structure
        if (!data.metadata || !data.modules || !Array.isArray(data.modules)) {
          throw new Error('Invalid OS build file structure');
        }

        // Get all available module types
        const allModuleTypes = Object.values(getModuleCategories()).flat();
        const moduleTypeMap = new Map(allModuleTypes.map(mt => [mt.name, mt]));

        // Validate that all module types exist
        const missingModules = [];
        for (const moduleData of data.modules) {
          if (!moduleTypeMap.has(moduleData.name)) {
            missingModules.push(moduleData.name);
          }
        }

        if (missingModules.length > 0) {
          throw new Error(`Missing module types: ${missingModules.join(', ')}`);
        }

        // Convert the imported modules back to Module objects
        const importedModules: Module[] = data.modules.map((moduleData: any) => ({
          id: crypto.randomUUID(), // Generate new IDs
          type: moduleData.type,
          name: moduleData.name,
          layer: moduleData.layer,
          position: moduleData.position,
          status: moduleData.status || 'active',
          config: moduleData.config || {},
          dependencyBindings: {},
          extensions: moduleData.extensions || [],
          hardwareSupport: moduleData.hardwareSupport || {},
        }));

        // Create ID mapping from old to new
        const idMap = new Map<string, string>();
        data.modules.forEach((moduleData: any, index: number) => {
          idMap.set(moduleData.id, importedModules[index].id);
        });

        // Restore dependency bindings with new IDs
        data.modules.forEach((moduleData: any, index: number) => {
          const bindings: Record<string, string> = {};
          
          if (moduleData.dependencies && Array.isArray(moduleData.dependencies)) {
            moduleData.dependencies.forEach((dep: any) => {
              const newDepId = idMap.get(dep.boundToModuleId);
              if (newDepId) {
                bindings[dep.dependencyType] = newDepId;
              }
            });
          }
          
          importedModules[index].dependencyBindings = bindings;
        });

        // Import connections if they exist
        const importedConnections: Connection[] = [];
        if (data.connections && Array.isArray(data.connections)) {
          data.connections.forEach((connData: any) => {
            const fromId = idMap.get(connData.from.moduleId || '');
            const toId = idMap.get(connData.to.moduleId || '');
            
            if (fromId && toId) {
              importedConnections.push({
                id: crypto.randomUUID(),
                from: {
                  moduleId: fromId,
                  portId: connData.from.portId,
                },
                to: {
                  moduleId: toId,
                  portId: connData.to.portId,
                },
                type: connData.type,
              });
            }
          });
        }

        // Import architecture and processor
        const importedArch = data.metadata.architecture || data.buildConfiguration?.architecture || 'x86_64';
        const importedProc = data.metadata.processor || data.buildConfiguration?.processor || 'Intel';

        // Call the import handler
        onImport(importedModules, importedConnections, importedArch, importedProc);

        ToastManager.success('Import Successful', {
          description: `Successfully imported ${importedModules.length} modules`,
          duration: 3000,
        });
      } catch (error) {
        console.error('Import error:', error);
        ToastManager.error('Import Failed', {
          description: error instanceof Error ? error.message : 'Failed to import OS build file',
          duration: 5000,
        });
      }
    };

    reader.onerror = () => {
      ToastManager.error('Import Failed', {
        description: 'Failed to read file',
        duration: 3000,
      });
    };

    reader.readAsText(file);

    // Reset the input so the same file can be imported again
    if (fileInputRef.current) {
      fileInputRef.current.value = '';
    }
  };

  const handleImportClick = () => {
    fileInputRef.current?.click();
  };

  const generateBuildJson = () => {
    // Get all module types from categories
    const allModuleTypes = Object.values(getModuleCategories()).flat();
    const moduleTypeMap = new Map(allModuleTypes.map(mt => [mt.name, mt]));

    // Validate modules before building
    const validationErrors = validateModules(modules, allModuleTypes);
    if (validationErrors.length > 0) {
      ToastManager.error('Build Failed', {
        description: `Found ${validationErrors.length} validation error(s). Please fix them before building.`,
        duration: 4000,
      });
      return null;
    }

    // Create a mapping of module IDs to UUIDs for stable references
    const moduleUUIDs = new Map(modules.map(m => [m.id, crypto.randomUUID()]));

    const osJson = {
      metadata: {
        name: "Custom OS",
        version: "1.0.0",
        buildDate: new Date().toISOString(),
        architecture: architecture,
        processor: processor,
      },
      
      modules: modules.map(module => {
        const moduleType = moduleTypeMap.get(module.name);
        
        return {
          id: moduleUUIDs.get(module.id),
          type: module.type,
          name: module.name,
          layer: module.layer,
          position: module.position,
          status: module.status,
          
          config: module.config,
          
          dependencies: moduleType?.dependencies?.map(dep => ({
            dependencyType: dep.type,
            required: dep.required || false,
            boundToModuleId: module.dependencyBindings?.[dep.type] || null,
            boundToModuleName: module.dependencyBindings?.[dep.type] 
              ? modules.find(m => m.id === module.dependencyBindings[dep.type])?.name 
              : null,
          })) || [],
          
          extensions: (module.enabledExtensions || []).map(extName => {
            const ext = moduleType?.extensions?.find(e => e.name === extName);
            return {
              name: extName,
              description: ext?.description || '',
              config: module.config?.[`ext_${extName}`] || {},
            };
          }),
          
          hardwareSupport: {
            supportedArchitectures: moduleType?.supportedArchitectures || [],
            supportedProcessors: moduleType?.supportedProcessors || [],
            currentlyCompatible: moduleType?.supportedArchitectures?.includes(architecture) || false,
          },
          
          vtable: moduleType?.vtable || [],
        };
      }),
      
      connections: connections.map(conn => ({
        id: conn.id,
        from: {
          moduleUUID: moduleUUIDs.get(conn.from.moduleId),
          portId: conn.from.portId,
        },
        to: {
          moduleUUID: moduleUUIDs.get(conn.to.moduleId),
          portId: conn.to.portId,
        },
        type: conn.type,
      })),
      
      buildConfiguration: {
        optimizationLevel: "O2",
        debugSymbols: false,
        targetPlatform: `${processor}-unknown-uefi`,
        architecture: architecture,
        processor: processor,
      },
    };

    return osJson;
  };

  const handleSave = () => {
    const osJson = generateBuildJson();
    if (!osJson) return;

    console.log("Saving OS Configuration:");
    console.log(JSON.stringify(osJson, null, 2));
    
    // Download the JSON file
    const blob = new Blob([JSON.stringify(osJson, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `os-save-${new Date().toISOString().replace(/[:.]/g, '-')}.json`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
    
    // Show success notification
    ToastManager.success('Save Successful', {
      description: `OS configuration saved with ${modules.length} modules`,
      duration: 4000,
    });
  };

  const handleBuild = () => {
    const osJson = generateBuildJson();
    if (!osJson) return;

    console.log("OS Build Configuration:");
    console.log(JSON.stringify(osJson, null, 2));
    
    // Download the JSON file
    const blob = new Blob([JSON.stringify(osJson, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `os-build-${new Date().toISOString().replace(/[:.]/g, '-')}.json`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
    
    // Show success notification
    ToastManager.success('Build Successful', {
      description: `OS successfully built for ${architecture} with ${modules.length} modules`,
      duration: 4000,
    });
  };

  return (
    <nav className="h-14 bg-zinc-900 border-b border-zinc-800 flex items-center justify-between px-4 select-none">
      <div className="flex items-center gap-4">
        <Logo />
        
        <div className="flex items-center gap-1 ml-4">
          <input
            ref={fileInputRef}
            type="file"
            accept=".json"
            onChange={handleFileChange}
            className="hidden"
          />
          <button 
            onClick={handleImportClick}
            className="px-3 py-1.5 text-sm hover:bg-zinc-800 rounded transition-colors"
          >
            <Upload className="size-4 inline mr-1.5" />
            Import
          </button>
          <button className="px-3 py-1.5 text-sm hover:bg-zinc-800 rounded transition-colors" onClick={handleSave}>
            <Save className="size-4 inline mr-1.5" />
            Save
          </button>
          <button className="px-3 py-1.5 text-sm hover:bg-zinc-800 rounded transition-colors" onClick={handleBuild}>
            <Share2 className="size-4 inline mr-1.5" />
            Export
          </button>
        </div>
      </div>

      <div className="flex items-center gap-2">
        <button 
          onClick={() => setShowArchSelector(true)}
          className="flex items-center gap-1 px-2 py-1 bg-zinc-800 hover:bg-zinc-700 rounded transition-colors cursor-pointer"
          title="Change target architecture and processor"
        >
          <Cpu className="size-4 text-zinc-400" />
          <span className="text-xs text-zinc-400">{architecture}</span>
        </button>
        
        {showArchSelector && (
          <ArchitectureSelector
            architecture={architecture}
            processor={processor}
            onArchitectureChange={onArchitectureChange}
            onClose={() => setShowArchSelector(false)}
          />
        )}
        
        <div className="w-px h-6 bg-zinc-700 mx-2"></div>
        
        <button className="px-4 py-1.5 text-sm bg-blue-600 hover:bg-blue-700 rounded transition-colors font-medium" onClick={handleBuild}>
          <Zap className="size-4 inline mr-1.5" />
          Build OS
        </button>
      </div>
    </nav>
  );
}