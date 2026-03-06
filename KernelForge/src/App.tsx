import { useState, useEffect, useCallback } from 'react';
import type { Module, Connection, ModuleType } from './types';
import { TopNav } from './components/TopNav';
import { Sidebar } from './components/Sidebar';
import { Canvas } from './components/Canvas';
import { PropertyEditor } from './components/PropertyEditor';
import { ModuleLibrary } from './components/ModuleLibrary';
import { WelcomeScreen } from './components/WelcomeScreen';
import { TemplatesPanel } from './components/TemplatesPanel';
import { ToastContainer } from './components/Toast';
import { initializeModuleData } from './data/loader';

export default function App() {
  const [modules, setModules] = useState<Module[]>([]);
  const [connections, setConnections] = useState<Connection[]>([]);
  const [selectedModule, setSelectedModule] = useState<Module | null>(null);
  const [showLibrary, setShowLibrary] = useState(true);
  const [showProperties, setShowProperties] = useState(true);
  const [showWelcome, setShowWelcome] = useState(true);
  const [showTemplates, setShowTemplates] = useState(false);
  const [architecture, setArchitecture] = useState('x86_64');
  const [processor, setProcessor] = useState('intel_core_i7');
  const [isLoading, setIsLoading] = useState(true);

  // Load all module data on startup
  useEffect(() => {
    const loadData = async () => {
      try {
        await initializeModuleData();
        setIsLoading(false);
      } catch (error) {
        console.error('Failed to load module data:', error);
        setIsLoading(false);
      }
    };
    loadData();
  }, []);

  const addModule = (moduleType: ModuleType, position: { x: number; y: number }) => {
    const newModule: Module = {
      id: `module-${Date.now()}`,
      type: moduleType.type,
      name: moduleType.name,
      position,
      config: { ...moduleType.defaultConfig },
      layer: moduleType.layer || 'kernel',
      status: 'unconfigured',
      dependencyBindings: {},
      vtable: moduleType.vtable || [],
      enabledExtensions: [],
    };
    setModules([...modules, newModule]);
    setSelectedModule(newModule);
  };

  // Listen for module drops from drag-and-drop
  useEffect(() => {
    const handleModuleDrop = (e: CustomEvent) => {
      const { moduleType, position } = e.detail;
      const newModule: Module = {
        id: `module-${Date.now()}`,
        type: moduleType.type,
        name: moduleType.name,
        position,
        config: { ...moduleType.defaultConfig },
        layer: moduleType.layer || 'kernel',
        status: 'unconfigured',
        dependencyBindings: {},
        vtable: moduleType.vtable || [],
        enabledExtensions: [],
      };
      setModules(prev => [...prev, newModule]);
      setSelectedModule(newModule);
    };

    window.addEventListener('moduleDrop' as any, handleModuleDrop);
    return () => window.removeEventListener('moduleDrop' as any, handleModuleDrop);
  }, []);

  const updateModule = useCallback((id: string, updates: Partial<Module>) => {
    setModules(prev => prev.map(m => m.id === id ? { ...m, ...updates } : m));
    setSelectedModule(prev => {
      if (prev?.id === id) {
        return { ...prev, ...updates };
      }
      return prev;
    });
  }, []);

  const deleteModule = useCallback((id: string) => {
    setModules(prev => prev.filter(m => m.id !== id));
    setConnections(prev => prev.filter(c => c.from.moduleId !== id && c.to.moduleId !== id));
    setSelectedModule(prev => prev?.id === id ? null : prev);
  }, []);

  const addConnection = useCallback((connection: Connection) => {
    setConnections(prev => [...prev, connection]);
  }, []);

  const deleteConnection = useCallback((id: string) => {
    setConnections(prev => prev.filter(c => c.id !== id));
  }, []);

  const handleImport = useCallback((importedModules: Module[], importedConnections: Connection[], arch: string, proc: string) => {
    setModules(importedModules);
    setConnections(importedConnections);
    setArchitecture(arch);
    setProcessor(proc);
    setSelectedModule(null);
  }, []);

  return (
    <div className="flex flex-col h-screen bg-zinc-950 text-zinc-100">
      <TopNav 
        modules={modules}
        connections={connections}
        architecture={architecture}
        setArchitecture={setArchitecture}
        processor={processor}
        setProcessor={setProcessor}
        onArchitectureChange={(arch, proc) => {
          setArchitecture(arch);
          setProcessor(proc);
        }}
        onValidate={() => {}}
        onExport={() => {}}
        onImport={handleImport}
        onTemplateSelect={() => setShowTemplates(true)}
        modulesCount={modules.length}
      />

      <div className="flex flex-1 overflow-hidden">
        <Sidebar
          showLibrary={showLibrary}
          setShowLibrary={setShowLibrary}
          showProperties={showProperties}
          setShowProperties={setShowProperties}
        />

        {showLibrary && (
          <ModuleLibrary 
            onClose={() => setShowLibrary(false)}
            onModuleSelect={(moduleType, position) => addModule(moduleType, position)}
            architecture={architecture}
            processor={processor}
          />
        )}
        
        <Canvas
          modules={modules}
          connections={connections}
          selectedModule={selectedModule}
          onSelectModule={setSelectedModule}
          onUpdateModule={updateModule}
          onDeleteModule={deleteModule}
          onAddConnection={addConnection}
          onDeleteConnection={deleteConnection}
          architecture={architecture}
          processor={processor}
        />
        
        {showProperties && selectedModule && (
          <PropertyEditor 
            module={selectedModule}
            onClose={() => setShowProperties(false)}
            onUpdateModule={updateModule}
            allModules={modules}
          />
        )}
      </div>

      {/* Overlays */}
      {showWelcome && (
        <WelcomeScreen
          onNewProject={() => setShowWelcome(false)}
          onOpenTemplate={() => {
            setShowWelcome(false);
            setShowTemplates(true);
          }}
          onClose={() => setShowWelcome(false)}
        />
      )}

      {showTemplates && (
        <TemplatesPanel
          onClose={() => setShowTemplates(false)}
          onSelectTemplate={(template) => {
            setShowTemplates(false);
            // Load template logic would go here
          }}
        />
      )}

      <ToastContainer />
    </div>
  );
}