import { useRef, useState, useCallback, useEffect } from 'react';
import { type Module, type Connection } from '../types';
import { ModuleNode } from './ModuleNode';
import { ConnectionLine } from './ConnectionLine';
import { CanvasToolbar } from './CanvasToolbar';
import { SimulationOverlay } from './SimulationOverlay';
import { ValidationPanel } from './ValidationPanel';
import { EmptyState } from './EmptyState';
import { DependencyArrow } from './DependencyArrow';
import { CanvasSettings, autoLayoutModules } from './CanvasSettings';
import { ZoomIn, ZoomOut, Maximize2, Settings as SettingsIcon } from 'lucide-react';
import { AnimatePresence, motion } from 'motion/react';
import { getModuleCategories } from '../data/loader';

interface CanvasProps {
  modules: Module[];
  connections: Connection[];
  selectedModule: Module | null;
  onSelectModule: (module: Module | null) => void;
  onUpdateModule: (id: string, updates: Partial<Module>) => void;
  onDeleteModule: (id: string) => void;
  onAddConnection: (connection: Connection) => void;
  onDeleteConnection: (id: string) => void;
  architecture: string;
  processor: string;
}

export function Canvas({
  modules,
  connections,
  selectedModule,
  onSelectModule,
  onUpdateModule,
  onDeleteModule,
  onAddConnection,
  onDeleteConnection,
  architecture,
  processor,
}: CanvasProps) {
  const canvasRef = useRef<HTMLDivElement>(null);
  const [zoom, setZoom] = useState(1);
  const [pan, setPan] = useState({ x: 0, y: 0 });
  const [isPanning, setIsPanning] = useState(false);
  const [panStart, setPanStart] = useState({ x: 0, y: 0 });
  const [mouseDownPos, setMouseDownPos] = useState({ x: 0, y: 0 });
  const [showGrid, setShowGrid] = useState(true);
  const [showValidation, setShowValidation] = useState(true);
  const [showSettings, setShowSettings] = useState(false);
  const [routingStyle, setRoutingStyle] = useState<'straight' | 'orthogonal'>('orthogonal');

  // Touch support for zoom
  const [touchState, setTouchState] = useState<{
    initialDistance: number | null;
    initialZoom: number;
    initialPan: { x: number; y: number };
  }>({
    initialDistance: null,
    initialZoom: 1,
    initialPan: { x: 0, y: 0 },
  });

  // Handle delete key press
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Delete' && selectedModule) {
        onDeleteModule(selectedModule.id);
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [selectedModule, onDeleteModule]);

  const handleDrop = useCallback((e: React.DragEvent) => {
    e.preventDefault();
    const moduleTypeData = e.dataTransfer.getData('moduleType');
    if (moduleTypeData) {
      const moduleType = JSON.parse(moduleTypeData);
      const rect = canvasRef.current?.getBoundingClientRect();
      if (rect) {
        const x = (e.clientX - rect.left - pan.x) / zoom;
        const y = (e.clientY - rect.top - pan.y) / zoom;
        
        // Dispatch custom event to add module immediately
        window.dispatchEvent(new CustomEvent('moduleDrop', {
          detail: { moduleType, position: { x, y } }
        }));
      }
    }
  }, [pan, zoom]);

  const handleDragOver = (e: React.DragEvent) => {
    e.preventDefault();
    e.dataTransfer.dropEffect = 'copy';
  };

  const handleMouseDown = (e: React.MouseEvent) => {
    // Allow panning when clicking on canvas background or transformed container
    const target = e.target as HTMLElement;
    const isBackground = target === e.currentTarget || 
                        target.classList.contains('canvas-background') ||
                        target.tagName === 'svg' ||
                        target.tagName === 'rect';
    
    if (isBackground) {
      setIsPanning(true);
      setPanStart({ x: e.clientX - pan.x, y: e.clientY - pan.y });
      setMouseDownPos({ x: e.clientX, y: e.clientY });
      // Don't deselect here - wait for mouseUp to check if it was a click or pan
    }
  };

  const handleMouseMove = useCallback((e: React.MouseEvent) => {
    if (isPanning) {
      setPan({
        x: e.clientX - panStart.x,
        y: e.clientY - panStart.y,
      });
    }
  }, [isPanning, panStart]);

  const handleMouseUp = (e: React.MouseEvent) => {
    if (isPanning) {
      // Check if mouse moved significantly (more than 5 pixels)
      const dx = e.clientX - mouseDownPos.x;
      const dy = e.clientY - mouseDownPos.y;
      const distanceMoved = Math.sqrt(dx * dx + dy * dy);
      
      // Only deselect if it was a click (not a pan)
      if (distanceMoved < 5) {
        const target = e.target as HTMLElement;
        const isBackground = target === e.currentTarget || 
                            target.classList.contains('canvas-background') ||
                            target.tagName === 'svg' ||
                            target.tagName === 'rect';
        
        if (isBackground) {
          onSelectModule(null);
        }
      }
    }
    
    setIsPanning(false);
  };

  const handleWheel = (e: React.WheelEvent) => {
    e.preventDefault();
    const delta = e.deltaY > 0 ? 0.9 : 1.1;
    setZoom(prev => Math.max(0.25, Math.min(2, prev * delta)));
  };

  // Touch event handlers
  const getTouchDistance = (touches: React.TouchList) => {
    const dx = touches[0].clientX - touches[1].clientX;
    const dy = touches[0].clientY - touches[1].clientY;
    return Math.sqrt(dx * dx + dy * dy);
  };

  const getTouchCenter = (touches: React.TouchList) => {
    return {
      x: (touches[0].clientX + touches[1].clientX) / 2,
      y: (touches[0].clientY + touches[1].clientY) / 2,
    };
  };

  const handleTouchStart = (e: React.TouchEvent) => {
    const target = e.target as HTMLElement;
    const isBackground = target === e.currentTarget || 
                        target.classList.contains('canvas-background') ||
                        target.tagName === 'svg' ||
                        target.tagName === 'rect';

    if (e.touches.length === 2) {
      // Two-finger pinch to zoom
      const distance = getTouchDistance(e.touches);
      setTouchState({
        initialDistance: distance,
        initialZoom: zoom,
        initialPan: pan,
      });
      e.preventDefault();
    } else if (e.touches.length === 1 && isBackground) {
      // Single finger pan
      setIsPanning(true);
      setPanStart({ x: e.touches[0].clientX - pan.x, y: e.touches[0].clientY - pan.y });
      setMouseDownPos({ x: e.touches[0].clientX, y: e.touches[0].clientY });
    }
  };

  const handleTouchMove = (e: React.TouchEvent) => {
    if (e.touches.length === 2 && touchState.initialDistance !== null) {
      // Pinch to zoom
      e.preventDefault();
      const currentDistance = getTouchDistance(e.touches);
      const scale = currentDistance / touchState.initialDistance;
      const newZoom = Math.max(0.25, Math.min(2, touchState.initialZoom * scale));
      
      setZoom(newZoom);
    } else if (e.touches.length === 1 && isPanning) {
      // Pan with single finger
      e.preventDefault();
      setPan({
        x: e.touches[0].clientX - panStart.x,
        y: e.touches[0].clientY - panStart.y,
      });
    }
  };

  const handleTouchEnd = (e: React.TouchEvent) => {
    if (e.touches.length === 0) {
      // All touches ended
      if (isPanning) {
        // Check if it was a tap (not a pan)
        const touch = e.changedTouches[0];
        const dx = touch.clientX - mouseDownPos.x;
        const dy = touch.clientY - mouseDownPos.y;
        const distanceMoved = Math.sqrt(dx * dx + dy * dy);
        
        if (distanceMoved < 5) {
          const target = e.target as HTMLElement;
          const isBackground = target === e.currentTarget || 
                              target.classList.contains('canvas-background') ||
                              target.tagName === 'svg' ||
                              target.tagName === 'rect';
          
          if (isBackground) {
            onSelectModule(null);
          }
        }
      }
      
      setIsPanning(false);
      setTouchState({
        initialDistance: null,
        initialZoom: zoom,
        initialPan: pan,
      });
    } else if (e.touches.length === 1) {
      // One finger remaining, reset pinch state
      setTouchState({
        initialDistance: null,
        initialZoom: zoom,
        initialPan: pan,
      });
    }
  };

  const handleModuleDrag = (id: string, newPosition: { x: number; y: number }) => {
    onUpdateModule(id, { position: newPosition });
  };

  const handleAutoLayout = () => {
    const layoutedModules = autoLayoutModules(modules);
    layoutedModules.forEach(m => {
      onUpdateModule(m.id, { position: m.position });
    });
  };

  return (
    <div className="flex-1 relative bg-zinc-900 overflow-hidden">
      <CanvasToolbar 
        zoom={zoom}
        setZoom={setZoom}
        showGrid={showGrid}
        setShowGrid={setShowGrid}
      />

      <div
        ref={canvasRef}
        className="w-full h-full cursor-grab active:cursor-grabbing"
        onMouseDown={handleMouseDown}
        onMouseMove={handleMouseMove}
        onMouseUp={handleMouseUp}
        onWheel={handleWheel}
        onDrop={handleDrop}
        onDragOver={handleDragOver}
        onTouchStart={handleTouchStart}
        onTouchMove={handleTouchMove}
        onTouchEnd={handleTouchEnd}
      >
        <div
          className="relative w-full h-full canvas-background"
          style={{
            transform: `translate(${pan.x}px, ${pan.y}px) scale(${zoom})`,
            transformOrigin: '0 0',
          }}
        >
          {/* Grid */}
          {showGrid && (
            <svg className="absolute pointer-events-none" style={{ left: '-5000px', top: '-5000px', width: '10000px', height: '10000px' }}>
              <defs>
                <pattern id="grid" width="40" height="40" patternUnits="userSpaceOnUse">
                  <path d="M 40 0 L 0 0 0 40" fill="none" stroke="rgb(63 63 70)" strokeWidth="0.5" />
                </pattern>
              </defs>
              <rect width="100%" height="100%" fill="url(#grid)" />
            </svg>
          )}

          {/* Connections - removed, now using dependency arrows only */}
          <svg 
            className="absolute pointer-events-none z-10" 
            style={{ 
              left: 0,
              top: 0,
              width: '100%', 
              height: '100%',
              overflow: 'visible'
            }}
          >
            <AnimatePresence>
            {/* Dependency arrows with new component */}
            {(() => {
              // Track which dependency pairs we've already drawn to avoid duplicates
              const drawnPairs = new Set<string>();
              
              // Color mapping for module types
              const typeColors: Record<string, string> = {
                'Scheduler': '#3b82f6',      // blue
                'Memory Manager': '#8b5cf6', // purple
                'Filesystem': '#10b981',     // green
                'Driver': '#f59e0b',         // amber
                'IPC': '#ec4899',            // pink
                'Hardware': '#ef4444',       // red
              };
              
              return modules.flatMap(module => 
                Object.entries(module.dependencyBindings || {}).map(([depType, depId]) => {
                  const depModule = modules.find(m => m.id === depId);
                  if (!depModule) return null;

                  // Create a unique pair ID
                  const pairId = `${depModule.id}->${module.id}`;
                  if (drawnPairs.has(pairId)) return null;
                  drawnPairs.add(pairId);

                  return (
                    <DependencyArrow
                      key={pairId}
                      fromModule={depModule}
                      toModule={module}
                      depType={depType}
                      allModules={modules}
                      routingStyle={routingStyle}
                      typeColors={typeColors}
                    />
                  );
                })
              ).filter(Boolean);
            })()}
            </AnimatePresence>
          </svg>

          {/* Modules */}
          {modules.map(module => (
            <ModuleNode
              key={module.id}
              module={module}
              selected={selectedModule?.id === module.id}
              onSelect={() => onSelectModule(module)}
              onDrag={handleModuleDrag}
              onDelete={() => onDeleteModule(module.id)}
              simulationMode={false}
              debugMode={false}
              zoom={zoom}
              pan={pan}
              canvasRect={canvasRef.current?.getBoundingClientRect() || null}
              allModules={modules}
            />
          ))}

          {/* Empty state */}
          {modules.length === 0 && <EmptyState />}
        </div>
      </div>

      {/* Mini-map */}
      <div className="absolute bottom-4 right-4 w-48 h-32 bg-zinc-800 border border-zinc-700 rounded-lg overflow-hidden">
        <div className="relative w-full h-full">
          {(() => {
            if (modules.length === 0) return null;

            // Calculate bounds of all modules
            const padding = 200;
            const minX = Math.min(...modules.map(m => m.position.x)) - padding;
            const maxX = Math.max(...modules.map(m => m.position.x)) + padding;
            const minY = Math.min(...modules.map(m => m.position.y)) - padding;
            const maxY = Math.max(...modules.map(m => m.position.y)) + padding;

            const worldWidth = maxX - minX;
            const worldHeight = maxY - minY;

            // Minimap dimensions
            const minimapWidth = 192; // w-48 = 192px
            const minimapHeight = 128; // h-32 = 128px

            return modules.map(module => {
              // Normalize position to 0-1 range within world bounds
              const normalizedX = (module.position.x - minX) / worldWidth;
              const normalizedY = (module.position.y - minY) / worldHeight;

              return (
                <div
                  key={module.id}
                  className="absolute w-2 h-2 bg-blue-500 rounded-sm"
                  style={{
                    left: `${normalizedX * 100}%`,
                    top: `${normalizedY * 100}%`,
                  }}
                />
              );
            });
          })()}
        </div>
      </div>

      {/* Simulation Overlay */}
      <SimulationOverlay active={false} moduleCount={modules.length} />

      {/* Validation Panel */}
      {showValidation && <ValidationPanel modules={modules} architecture={architecture} processor={processor} onClose={() => setShowValidation(false)} />}

      {/* Settings Button */}
      <button
        onClick={() => setShowSettings(!showSettings)}
        className="absolute bottom-20 right-4 p-2 bg-zinc-800 hover:bg-zinc-700 border border-zinc-700 rounded-lg transition-colors z-40"
        title="Canvas Settings"
      >
        <SettingsIcon className="size-5 text-zinc-300" />
      </button>

      {/* Settings Panel */}
      {showSettings && (
        <CanvasSettings
          routingStyle={routingStyle}
          onRoutingStyleChange={setRoutingStyle}
          onAutoLayout={handleAutoLayout}
          onClose={() => setShowSettings(false)}
        />
      )}
    </div>
  );
}