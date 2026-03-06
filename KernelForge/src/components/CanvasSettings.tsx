import { Settings, Wand2, X } from 'lucide-react';
import { type Module } from '../types';

interface CanvasSettingsProps {
  routingStyle: 'straight' | 'orthogonal';
  onRoutingStyleChange: (style: 'straight' | 'orthogonal') => void;
  onAutoLayout: () => void;
  onClose: () => void;
}

export function CanvasSettings({
  routingStyle,
  onRoutingStyleChange,
  onAutoLayout,
  onClose,
}: CanvasSettingsProps) {
  return (
    <div className="absolute top-4 right-4 w-80 bg-zinc-900 border border-zinc-700 rounded-lg shadow-xl z-50">
      {/* Header */}
      <div className="px-4 py-3 border-b border-zinc-800 flex items-center justify-between">
        <div className="flex items-center gap-2">
          <Settings className="size-5 text-blue-400" />
          <h2 className="font-semibold">Canvas Settings</h2>
        </div>
        <button
          onClick={onClose}
          className="p-1 hover:bg-zinc-800 rounded transition-colors"
        >
          <X className="size-4" />
        </button>
      </div>

      {/* Content */}
      <div className="p-4 space-y-4">
        {/* Routing Style */}
        <div>
          <label className="text-sm font-medium text-zinc-300 mb-2 block">
            Connection Routing
          </label>
          <div className="space-y-2">
            <label className="flex items-center gap-2 cursor-pointer">
              <input
                type="radio"
                name="routing"
                value="straight"
                checked={routingStyle === 'straight'}
                onChange={() => onRoutingStyleChange('straight')}
                className="w-4 h-4 text-blue-500"
              />
              <span className="text-sm text-zinc-400">Straight Lines</span>
            </label>
            <label className="flex items-center gap-2 cursor-pointer">
              <input
                type="radio"
                name="routing"
                value="orthogonal"
                checked={routingStyle === 'orthogonal'}
                onChange={() => onRoutingStyleChange('orthogonal')}
                className="w-4 h-4 text-blue-500"
              />
              <span className="text-sm text-zinc-400">Orthogonal (Smart Routing)</span>
            </label>
          </div>
        </div>

        {/* Auto Layout */}
        <div className="pt-3 border-t border-zinc-800">
          <label className="text-sm font-medium text-zinc-300 mb-2 block">
            Layout
          </label>
          <button
            onClick={onAutoLayout}
            className="w-full px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded flex items-center justify-center gap-2 transition-colors"
          >
            <Wand2 className="size-4" />
            Auto-Format Nodes
          </button>
          <p className="text-xs text-zinc-500 mt-2">
            Automatically arrange nodes in a hierarchical layout based on dependencies
          </p>
        </div>
      </div>
    </div>
  );
}

// Auto-layout algorithm using hierarchical layering
export function autoLayoutModules(modules: Module[]): Module[] {
  if (modules.length === 0) return modules;
  
  // Build dependency graph
  const graph = new Map<string, Set<string>>();
  modules.forEach(m => {
    graph.set(m.id, new Set(Object.values(m.dependencyBindings || {})));
  });
  
  // Topological sort to determine layers
  const layers: string[][] = [];
  const visited = new Set<string>();
  const layerMap = new Map<string, number>();
  
  // Find modules with no dependencies (root nodes)
  const roots = modules.filter(m => {
    const deps = Object.values(m.dependencyBindings || {});
    return deps.length === 0;
  });
  
  // BFS to assign layers
  const queue: Array<{ id: string; layer: number }> = roots.map(m => ({ id: m.id, layer: 0 }));
  
  while (queue.length > 0) {
    const { id, layer } = queue.shift()!;
    
    if (visited.has(id)) continue;
    visited.add(id);
    
    if (!layers[layer]) layers[layer] = [];
    layers[layer].push(id);
    layerMap.set(id, layer);
    
    // Find modules that depend on this one
    modules.forEach(m => {
      const deps = Object.values(m.dependencyBindings || {});
      if (deps.includes(id) && !visited.has(m.id)) {
        queue.push({ id: m.id, layer: layer + 1 });
      }
    });
  }
  
  // Add any unconnected modules
  modules.forEach(m => {
    if (!visited.has(m.id)) {
      if (!layers[0]) layers[0] = [];
      layers[0].push(m.id);
      layerMap.set(m.id, 0);
    }
  });
  
  // Position modules
  const horizontalSpacing = 350;
  const verticalSpacing = 200;
  const startX = 100;
  const startY = 100;
  
  const updatedModules = modules.map(module => {
    const layer = layerMap.get(module.id) || 0;
    const indexInLayer = layers[layer].indexOf(module.id);
    
    return {
      ...module,
      position: {
        x: startX + layer * horizontalSpacing,
        y: startY + indexInLayer * verticalSpacing,
      },
    };
  });
  
  return updatedModules;
}
