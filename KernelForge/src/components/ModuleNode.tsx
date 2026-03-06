import { useEffect, useRef } from 'react';
import { type Module, type ModuleType } from '../types';
import { Trash2, AlertTriangle, CheckCircle, Play } from 'lucide-react';
import * as Icons from 'lucide-react';
import { getModuleCategories } from '../data/loader';
import { isModuleConfigured } from '../utils/moduleStatus';

interface ModuleNodeProps {
  module: Module;
  selected: boolean;
  onSelect: () => void;
  onDrag: (id: string, position: { x: number; y: number }) => void;
  onDelete: () => void;
  simulationMode: boolean;
  debugMode: boolean;
  zoom: number;
  pan: { x: number; y: number };
  canvasRect: DOMRect | null;
  allModules: Module[];
}

export function ModuleNode({
  module,
  selected,
  onSelect,
  onDrag,
  onDelete,
  simulationMode,
  debugMode,
  zoom,
  pan,
  canvasRect,
  allModules,
}: ModuleNodeProps) {
  const nodeRef = useRef<HTMLDivElement>(null);
  const dragInfoRef = useRef<{ startX: number; startY: number; offsetX: number; offsetY: number } | null>(null);

  const handleMouseDown = (e: React.MouseEvent) => {
    if (e.button !== 0) return; // Only left click
    
    // Calculate offset in world space
    if (canvasRect) {
      const worldX = (e.clientX - canvasRect.left - pan.x) / zoom;
      const worldY = (e.clientY - canvasRect.top - pan.y) / zoom;
      
      dragInfoRef.current = {
        startX: module.position.x,
        startY: module.position.y,
        offsetX: worldX - module.position.x,
        offsetY: worldY - module.position.y,
      };
    }
    
    onSelect();
    e.stopPropagation();
    
    // Add listeners
    document.addEventListener('mousemove', handleMouseMove);
    document.addEventListener('mouseup', handleMouseUp);
  };

  const handleMouseMove = (e: MouseEvent) => {
    if (!dragInfoRef.current || !canvasRect) return;
    
    // Convert screen coordinates to world coordinates
    const worldX = (e.clientX - canvasRect.left - pan.x) / zoom;
    const worldY = (e.clientY - canvasRect.top - pan.y) / zoom;
    
    onDrag(module.id, {
      x: worldX - dragInfoRef.current.offsetX,
      y: worldY - dragInfoRef.current.offsetY,
    });
  };

  const handleMouseUp = () => {
    dragInfoRef.current = null;
    document.removeEventListener('mousemove', handleMouseMove);
    document.removeEventListener('mouseup', handleMouseUp);
  };

  // Touch event handlers
  const handleTouchStart = (e: React.TouchEvent) => {
    if (e.touches.length !== 1) return; // Only single touch
    
    const touch = e.touches[0];
    
    // Calculate offset in world space
    if (canvasRect) {
      const worldX = (touch.clientX - canvasRect.left - pan.x) / zoom;
      const worldY = (touch.clientY - canvasRect.top - pan.y) / zoom;
      
      dragInfoRef.current = {
        startX: module.position.x,
        startY: module.position.y,
        offsetX: worldX - module.position.x,
        offsetY: worldY - module.position.y,
      };
    }
    
    onSelect();
    e.stopPropagation();
    
    // Add listeners
    document.addEventListener('touchmove', handleTouchMove);
    document.addEventListener('touchend', handleTouchEnd);
  };

  const handleTouchMove = (e: TouchEvent) => {
    if (!dragInfoRef.current || !canvasRect || e.touches.length !== 1) return;
    
    e.preventDefault(); // Prevent scrolling
    const touch = e.touches[0];
    
    // Convert screen coordinates to world coordinates
    const worldX = (touch.clientX - canvasRect.left - pan.x) / zoom;
    const worldY = (touch.clientY - canvasRect.top - pan.y) / zoom;
    
    onDrag(module.id, {
      x: worldX - dragInfoRef.current.offsetX,
      y: worldY - dragInfoRef.current.offsetY,
    });
  };

  const handleTouchEnd = () => {
    dragInfoRef.current = null;
    document.removeEventListener('touchmove', handleTouchMove);
    document.removeEventListener('touchend', handleTouchEnd);
  };

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      document.removeEventListener('mousemove', handleMouseMove);
      document.removeEventListener('mouseup', handleMouseUp);
      document.removeEventListener('touchmove', handleTouchMove);
      document.removeEventListener('touchend', handleTouchEnd);
    };
  }, []);

  // Get module type to access dependencies
  const moduleType: ModuleType | undefined = Object.values(getModuleCategories())
    .flat()
    .find(mt => mt.name === module.name);

  const layerColors = {
    hardware: 'border-red-500 bg-red-950/50',
    kernel: 'border-blue-500 bg-blue-950/50',
    service: 'border-green-500 bg-green-950/50',
    application: 'border-purple-500 bg-purple-950/50',
  };

  const statusColors = {
    unconfigured: 'text-yellow-500',
    configured: 'text-green-500',
    error: 'text-red-500',
    active: 'text-blue-500',
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

  const getTypeColor = (type: string) => typeColors[type] || '#6b7280'; // gray fallback

  // Get icon component
  const IconComponent = (Icons as any)[module.name.split(' ').map((w: string) => w[0]).join('') || 'Box'] || Icons.Box;

  // Compute actual configuration status
  const isConfigured = isModuleConfigured(module, allModules);

  return (
    <div
      ref={nodeRef}
      className={`absolute w-64 bg-zinc-800 rounded-lg border-2 transition-none cursor-move ${
        layerColors[module.layer]
      } ${selected ? 'ring-2 ring-blue-500 shadow-lg shadow-blue-500/50' : ''} ${
        simulationMode && module.status === 'active' ? 'animate-pulse' : ''
      }`}
      style={{
        left: module.position.x,
        top: module.position.y,
      }}
      onMouseDown={handleMouseDown}
      onTouchStart={handleTouchStart}
    >
      {/* Header */}
      <div className="px-3 py-2 border-b border-zinc-700 flex items-center justify-between">
        <div className="flex items-center gap-2">
          <IconComponent className="size-4" />
          <span className="text-sm font-semibold truncate">{module.name}</span>
        </div>
        <div className="flex items-center gap-1">
          {isConfigured && <CheckCircle className={`size-4 ${statusColors['configured']}`} />}
          {!isConfigured && <AlertTriangle className={`size-4 text-red-500`} />}
          {simulationMode && module.status === 'active' && <Play className="size-4 text-blue-500" />}
          <button
            onClick={(e) => {
              e.stopPropagation();
              onDelete();
            }}
            className="p-1 hover:bg-red-500/20 rounded"
          >
            <Trash2 className="size-3 text-red-500" />
          </button>
        </div>
      </div>

      {/* Body */}
      <div className="p-3 space-y-2 relative">
        <div className="text-xs text-zinc-400">
          <div className="flex items-center justify-between">
            <span>Type:</span>
            <span 
              className="text-zinc-300 font-medium px-2 py-0.5 rounded"
              style={{ backgroundColor: getTypeColor(module.type) + '20' }}
            >
              {module.type}
            </span>
          </div>
          <div className="flex items-center justify-between">
            <span>Layer:</span>
            <span className="text-zinc-300 font-medium">{module.layer}</span>
          </div>
        </div>

        {/* Dependencies (Inputs) - Left side */}
        {moduleType?.dependencies && moduleType.dependencies.length > 0 && (
          <div className="pt-2 border-t border-zinc-700 relative">
            <div className="text-xs font-medium text-zinc-400 mb-1">Inputs:</div>
            <div className="space-y-1.5">
              {moduleType.dependencies.map((dep, index) => (
                <div key={index} className="flex items-center gap-2 text-xs relative pl-2">
                  {/* Circle on the left edge */}
                  <div
                    className="absolute left-0 w-3 h-3 rounded-full border-2 flex-shrink-0 -translate-x-1/2 border-zinc-800"
                    style={{ backgroundColor: getTypeColor(dep.type) }}
                    title={`Input: ${dep.type}`}
                  />
                  <span className="text-zinc-300">{dep.type}</span>
                </div>
              ))}
            </div>
          </div>
        )}

        {/* Output - Right side */}
        <div className="pt-2 border-t border-zinc-700 relative flex justify-end">
          <div className="text-right">
            <div className="text-xs font-medium text-zinc-400 mb-1">Output:</div>
            <div className="flex items-center gap-2 text-xs relative justify-end pr-2">
              <span className="text-zinc-300">{module.type}</span>
              {/* Circle on the right edge */}
              <div
                className="absolute right-0 w-3 h-3 rounded-full border-2 flex-shrink-0 translate-x-1/2 border-zinc-800"
                style={{ backgroundColor: getTypeColor(module.type) }}
                title={`Output: ${module.type}`}
              />
            </div>
          </div>
        </div>

        {debugMode && (
          <div className="text-xs font-mono text-zinc-500 pt-2 border-t border-zinc-700">
            ID: {module.id.slice(0, 12)}...
          </div>
        )}
      </div>
    </div>
  );
}