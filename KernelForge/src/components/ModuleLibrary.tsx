import { useState, useMemo, useEffect, useRef } from 'react';
import { Search, X, Package, ChevronDown, ChevronRight, Code, Shield, ShieldCheck, AlertTriangle, Filter } from 'lucide-react';
import { getModuleCategories, getAllArchitectures } from '../data/loader';
import { type ModuleType } from '../types';
import { 
  Clock, Users, HardDrive, Box, FolderTree, Zap, Network, 
  MessageSquare, Share2, Timer
} from 'lucide-react';
import { ModuleDetailsPopup } from './ModuleDetailsPopup';

interface ModuleLibraryProps {
  onClose: () => void;
  onModuleSelect: (moduleType: ModuleType, position: { x: number; y: number }) => void;
  architecture: string;
  processor: string;
}

const iconMap: Record<string, any> = {
  Clock, Users, HardDrive, Box, FolderTree, Zap, Network,
  MessageSquare, Share2, Timer,
};

export function ModuleLibrary({ onClose, onModuleSelect, architecture, processor }: ModuleLibraryProps) {
  const [searchQuery, setSearchQuery] = useState('');
  const [expandedCategories, setExpandedCategories] = useState<Set<string>>(
    new Set(['Scheduler', 'Memory Manager'])
  );
  const [draggedModule, setDraggedModule] = useState<ModuleType | null>(null);
  const [selectedModuleType, setSelectedModuleType] = useState<ModuleType | null>(null);
  const [selectedArchitectureFilter, setSelectedArchitectureFilter] = useState<string>('all');
  const [isFilterDropdownOpen, setIsFilterDropdownOpen] = useState(false);
  const filterDropdownRef = useRef<HTMLDivElement>(null);

  // Get all available architectures
  const availableArchitectures = useMemo(() => getAllArchitectures(), []);

  // Close dropdown when clicking outside
  useEffect(() => {
    const handleClickOutside = (event: MouseEvent) => {
      if (filterDropdownRef.current && !filterDropdownRef.current.contains(event.target as Node)) {
        setIsFilterDropdownOpen(false);
      }
    };

    if (isFilterDropdownOpen) {
      document.addEventListener('mousedown', handleClickOutside);
      return () => document.removeEventListener('mousedown', handleClickOutside);
    }
  }, [isFilterDropdownOpen]);

  // Check if module is compatible with selected filter architecture for filtering
  const isModuleCompatible = (moduleType: ModuleType): boolean => {
    // If "all" is selected, show everything
    if (selectedArchitectureFilter === 'all') {
      return true;
    }
    
    // If no targetArchitectures specified, it's compatible with all
    if (!moduleType.targetArchitectures || moduleType.targetArchitectures.length === 0) {
      return true;
    }
    
    // Check if selected architecture is in the supported list
    return moduleType.targetArchitectures.includes(selectedArchitectureFilter);
  };

  // Check if module is compatible with current project architecture (for warning icon)
  const isCompatibleWithProjectArch = (moduleType: ModuleType): boolean => {
    // If no targetArchitectures specified, it's compatible with all
    if (!moduleType.targetArchitectures || moduleType.targetArchitectures.length === 0) {
      return true;
    }
    // Check if current project architecture is in the supported list
    return moduleType.targetArchitectures.includes(architecture);
  };

  const filteredCategories = useMemo(() => {
    let baseCategories = getModuleCategories();
    
    // Apply search filter
    if (searchQuery) {
      const filtered: Record<string, ModuleType[]> = {};
      Object.entries(baseCategories).forEach(([category, modules]) => {
        const matchedModules = modules.filter(mod =>
          mod.name.toLowerCase().includes(searchQuery.toLowerCase()) ||
          mod.description.toLowerCase().includes(searchQuery.toLowerCase()) ||
          mod.type.toLowerCase().includes(searchQuery.toLowerCase())
        );
        if (matchedModules.length > 0) {
          filtered[category] = matchedModules;
        }
      });
      baseCategories = filtered;
    }
    
    // Apply compatibility filter
    if (selectedArchitectureFilter !== 'all') {
      const filtered: Record<string, ModuleType[]> = {};
      Object.entries(baseCategories).forEach(([category, modules]) => {
        const compatibleModules = modules.filter(mod => isModuleCompatible(mod));
        if (compatibleModules.length > 0) {
          filtered[category] = compatibleModules;
        }
      });
      return filtered;
    }
    
    return baseCategories;
  }, [searchQuery, selectedArchitectureFilter]);

  const toggleCategory = (category: string) => {
    const newExpanded = new Set(expandedCategories);
    if (newExpanded.has(category)) {
      newExpanded.delete(category);
    } else {
      newExpanded.add(category);
    }
    setExpandedCategories(newExpanded);
  };

  const handleDragStart = (e: React.DragEvent, moduleType: ModuleType) => {
    setDraggedModule(moduleType);
    e.dataTransfer.effectAllowed = 'copy';
    e.dataTransfer.setData('moduleType', JSON.stringify(moduleType));
  };

  const handleDragEnd = () => {
    setDraggedModule(null);
  };

  const layerColors = {
    hardware: 'bg-red-500/20 border-red-500/40 text-red-400',
    kernel: 'bg-blue-500/20 border-blue-500/40 text-blue-400',
    service: 'bg-green-500/20 border-green-500/40 text-green-400',
    application: 'bg-purple-500/20 border-purple-500/40 text-purple-400',
  };

  // Color mapping for module types
  const typeColors: Record<string, string> = {
    'Scheduler': '#3b82f6',      // blue
    'Memory Manager': '#8b5cf6', // purple
    'Filesystem': '#10b981',     // green
    'Driver': '#f59e0b',         // amber
    'IPC': '#ec4899',            // pink
    'Hardware': '#ef4444',       // red
  };

  const getTypeColor = (type: string) => typeColors[type] || '#6b7280';

  const handleModuleClick = (e: React.MouseEvent, moduleType: ModuleType) => {
    // Don't show popup if we're starting a drag
    if (draggedModule) return;
    
    e.stopPropagation();
    setSelectedModuleType(moduleType);
  };

  const handleInsertModule = (moduleType: ModuleType) => {
    // Insert at center of canvas (we'll use a default position)
    // The canvas will be at 0,0 in world space initially
    onModuleSelect(moduleType, { x: 400, y: 300 });
  };

  return (
    <div className="w-80 bg-zinc-900 border-r border-zinc-800 flex flex-col select-none">
      {/* Header */}
      <div className="px-4 py-3 border-b border-zinc-800 flex items-center justify-between">
        <div className="flex items-center gap-2">
          <Package className="size-5 text-blue-400" />
          <h2 className="font-semibold">Module Library</h2>
        </div>
        <button
          onClick={onClose}
          className="p-1 hover:bg-zinc-800 rounded transition-colors"
        >
          <X className="size-4" />
        </button>
      </div>

      {/* Search */}
      <div className="px-4 py-3 border-b border-zinc-800 space-y-2">
        <div className="relative">
          <Search className="absolute left-3 top-1/2 -translate-y-1/2 size-4 text-zinc-500" />
          <input
            type="text"
            placeholder="Search modules..."
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            className="w-full pl-9 pr-3 py-2 bg-zinc-800 border border-zinc-700 rounded-lg text-sm focus:outline-none focus:ring-2 focus:ring-blue-500"
          />
        </div>
        <div className="relative" ref={filterDropdownRef}>
          <button
            onClick={() => setIsFilterDropdownOpen(!isFilterDropdownOpen)}
            className={`w-full flex items-center gap-2 px-3 py-2 rounded-lg text-xs transition-colors ${
              selectedArchitectureFilter !== 'all' 
                ? 'bg-blue-600 text-white' 
                : 'bg-zinc-800 text-zinc-400 hover:bg-zinc-750'
            }`}
          >
            <Filter className="size-3.5" />
            {selectedArchitectureFilter !== 'all' ? `Compatible with ${selectedArchitectureFilter}` : 'Show all modules'}
          </button>
          {isFilterDropdownOpen && (
            <div className="absolute left-0 top-full w-full bg-zinc-900 border border-zinc-700 rounded-b-lg z-10">
              <button
                onClick={() => {
                  setSelectedArchitectureFilter('all');
                  setIsFilterDropdownOpen(false);
                }}
                className={`w-full px-3 py-2 text-xs text-left hover:bg-zinc-750 transition-colors ${
                  selectedArchitectureFilter === 'all' ? 'bg-blue-600 text-white' : 'text-zinc-400'
                }`}
              >
                All Architectures
              </button>
              {availableArchitectures.map(arch => (
                <button
                  key={arch}
                  onClick={() => {
                    setSelectedArchitectureFilter(arch);
                    setIsFilterDropdownOpen(false);
                  }}
                  className={`w-full px-3 py-2 text-xs text-left hover:bg-zinc-750 transition-colors ${
                    selectedArchitectureFilter === arch ? 'bg-blue-600 text-white' : 'text-zinc-400'
                  }`}
                >
                  {arch}
                </button>
              ))}
            </div>
          )}
        </div>
      </div>

      {/* Categories and Modules */}
      <div className="flex-1 overflow-y-auto px-4 py-2">
        {Object.entries(filteredCategories).map(([category, modules]) => (
          <div key={category} className="mb-3">
            <button
              onClick={() => toggleCategory(category)}
              className="w-full flex items-center gap-2 px-2 py-1.5 hover:bg-zinc-800 rounded transition-colors text-sm font-medium"
            >
              {expandedCategories.has(category) ? (
                <ChevronDown className="size-4" />
              ) : (
                <ChevronRight className="size-4" />
              )}
              <span>{category}</span>
              <span className="ml-auto text-xs text-zinc-500">{modules.length}</span>
            </button>

            {expandedCategories.has(category) && (
              <div className="mt-1 space-y-1 pl-2">
                {modules.map(moduleType => {
                  const IconComponent = iconMap[moduleType.icon] || Box;
                  const isCompatible = isCompatibleWithProjectArch(moduleType);
                  
                  return (
                    <div key={moduleType.name} className="space-y-1 relative group select-none">
                      <div
                        draggable
                        onDragStart={(e) => handleDragStart(e, moduleType)}
                        onDragEnd={handleDragEnd}
                        onClick={(e) => handleModuleClick(e, moduleType)}
                        title={!isCompatible ? `Incompatible with ${architecture}` : undefined}
                        className={`
                          p-3 bg-zinc-800 rounded-lg cursor-pointer hover:bg-zinc-750 transition-colors
                          ${draggedModule?.type === moduleType.type ? 'opacity-50' : ''}
                          ${!isCompatible ? 'border-2 border-red-500' : 'border border-zinc-700'}
                        `}
                      >
                        {/* Incompatibility indicator */}
                        {!isCompatible && (
                          <div className="absolute top-1 right-1">
                            <div className="relative">
                              <AlertTriangle className="size-4 text-red-500" />
                              <div className="absolute bottom-full right-0 mb-2 hidden group-hover:block w-48 p-2 bg-zinc-950 border border-red-500/50 rounded text-xs text-zinc-300 z-50">
                                <div className="font-semibold text-red-400 mb-1">Incompatible Architecture</div>
                                <div>This module is not supported on {architecture}.</div>
                                {moduleType.targetArchitectures && moduleType.targetArchitectures.length > 0 && (
                                  <div className="mt-1 text-zinc-400">
                                    Supported: {moduleType.targetArchitectures.join(', ')}
                                  </div>
                                )}
                              </div>
                            </div>
                          </div>
                        )}
                        <div className="flex items-start gap-2">
                          <div className="p-1.5 bg-zinc-700 rounded">
                            <IconComponent className="size-4" />
                          </div>
                          <div className="flex-1 min-w-0">
                            <div className="flex items-center gap-2">
                              <h3 className="text-sm font-medium truncate">{moduleType.name}</h3>
                              {/* Source Badge */}
                              {moduleType.source === 'builtin' && (
                                <div className="flex items-center gap-1 px-1.5 py-0.5 bg-blue-500/20 border border-blue-500/40 rounded text-[10px] text-blue-300">
                                  <Shield className="size-2.5" />
                                  Built-in
                                </div>
                              )}
                              {moduleType.source === 'verified-community' && (
                                <div className="flex items-center gap-1 px-1.5 py-0.5 bg-green-500/20 border border-green-500/40 rounded text-[10px] text-green-300">
                                  <ShieldCheck className="size-2.5" />
                                  Verified
                                </div>
                              )}
                              {moduleType.source === 'community' && (
                                <div className="flex items-center gap-1 px-1.5 py-0.5 bg-yellow-500/20 border border-yellow-500/40 rounded text-[10px] text-yellow-300">
                                  <AlertTriangle className="size-2.5" />
                                  Community
                                </div>
                              )}
                            </div>
                            <p className="text-xs text-zinc-400 line-clamp-2 mt-0.5">
                              {moduleType.description}
                            </p>
                            <div className="flex items-center gap-2 mt-2">
                              <span className={`text-xs px-2 py-0.5 rounded border ${layerColors[moduleType.layer]}`}>
                                {moduleType.layer}
                              </span>
                            </div>
                          </div>
                        </div>

                        {/* Port indicators */}
                        <div className="flex items-center gap-3 mt-2 pt-2 border-t border-zinc-700 text-xs text-zinc-500">
                          <div className="flex items-center gap-1">
                            <div 
                              className="w-2 h-2 rounded-full" 
                              style={{ backgroundColor: '#6b7280' }}
                            />
                            {moduleType.dependencies.length} inputs
                          </div>
                          <div className="flex items-center gap-1">
                            <div 
                              className="w-2 h-2 rounded-full"
                              style={{ backgroundColor: getTypeColor(moduleType.type) }}
                            />
                            1 output ({moduleType.type})
                          </div>
                        </div>
                      </div>
                    </div>
                  );
                })}
              </div>
            )}
          </div>
        ))}
      </div>

      {/* Instructions */}
      <div className="px-4 py-3 border-t border-zinc-800 bg-zinc-900">
        <p className="text-xs text-zinc-500">
          Drag modules onto the canvas to build your OS. Each category represents a module type with its own vtable.
        </p>
      </div>

      {/* Module Details Popup */}
      {selectedModuleType && (
        <ModuleDetailsPopup
          moduleType={selectedModuleType}
          onClose={() => setSelectedModuleType(null)}
          onInsert={handleInsertModule}
        />
      )}
    </div>
  );
}