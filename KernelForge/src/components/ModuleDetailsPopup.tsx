import { type ModuleType } from '../types';
import { X, Plus, Code, Shield, ShieldCheck, AlertTriangle } from 'lucide-react';
import { 
  Clock, Users, HardDrive, Box, FolderTree, Zap, Network, 
  MessageSquare, Share2, Timer
} from 'lucide-react';

interface ModuleDetailsPopupProps {
  moduleType: ModuleType;
  onClose: () => void;
  onInsert: (moduleType: ModuleType) => void;
}

const iconMap: Record<string, any> = {
  Clock, Users, HardDrive, Box, FolderTree, Zap, Network,
  MessageSquare, Share2, Timer,
};

export function ModuleDetailsPopup({ moduleType, onClose, onInsert }: ModuleDetailsPopupProps) {
  const IconComponent = iconMap[moduleType.icon] || Box;

  const layerColors = {
    hardware: 'bg-red-500/20 border-red-500/40 text-red-400',
    kernel: 'bg-blue-500/20 border-blue-500/40 text-blue-400',
    service: 'bg-green-500/20 border-green-500/40 text-green-400',
    application: 'bg-purple-500/20 border-purple-500/40 text-purple-400',
  };

  const typeColors: Record<string, string> = {
    'Scheduler': '#3b82f6',
    'Memory Manager': '#8b5cf6',
    'Filesystem': '#10b981',
    'Driver': '#f59e0b',
    'IPC': '#ec4899',
    'Hardware': '#ef4444',
  };

  const getTypeColor = (type: string) => typeColors[type] || '#6b7280';

  const handleInsert = () => {
    onInsert(moduleType);
    onClose();
  };

  return (
    <>
      {/* Backdrop */}
      <div 
        className="fixed inset-0 bg-black/20 z-30"
        onClick={onClose}
      />
      
      {/* Popup */}
      <div className="fixed left-[368px] top-1/2 -translate-y-1/2 w-96 bg-zinc-800 border border-zinc-700 rounded-lg shadow-2xl z-40 select-none">
        {/* Header */}
        <div className="px-4 py-3 border-b border-zinc-700 flex items-center justify-between">
          <div className="flex items-center gap-2">
            <div className="p-2 bg-zinc-700 rounded">
              <IconComponent className="size-5" />
            </div>
            <div>
              <h3 className="font-semibold text-sm">{moduleType.name}</h3>
              <p className="text-xs text-zinc-400">{moduleType.type}</p>
            </div>
          </div>
          <button
            onClick={onClose}
            className="p-1 hover:bg-zinc-700 rounded transition-colors"
          >
            <X className="size-4" />
          </button>
        </div>

        {/* Content */}
        <div className="p-4 space-y-4 max-h-[600px] overflow-y-auto">
          {/* Description */}
          <div>
            <h4 className="text-xs font-semibold text-zinc-400 mb-1">Description</h4>
            <p className="text-sm text-zinc-300">{moduleType.description}</p>
          </div>

          {/* Badges */}
          <div className="flex flex-wrap gap-2">
            <span className={`text-xs px-2 py-1 rounded border ${layerColors[moduleType.layer]}`}>
              {moduleType.layer}
            </span>
            
            {moduleType.source === 'builtin' && (
              <div className="flex items-center gap-1 px-2 py-1 bg-blue-500/20 border border-blue-500/40 rounded text-xs text-blue-300">
                <Shield className="size-3" />
                Built-in
              </div>
            )}
            {moduleType.source === 'verified-community' && (
              <div className="flex items-center gap-1 px-2 py-1 bg-green-500/20 border border-green-500/40 rounded text-xs text-green-300">
                <ShieldCheck className="size-3" />
                Verified
              </div>
            )}
            {moduleType.source === 'community' && (
              <div className="flex items-center gap-1 px-2 py-1 bg-yellow-500/20 border border-yellow-500/40 rounded text-xs text-yellow-300">
                <AlertTriangle className="size-3" />
                Community
              </div>
            )}
          </div>

          {/* Hardware Support */}
          <div>
            <h4 className="text-xs font-semibold text-zinc-400 mb-2">Hardware Support</h4>
            <div className="text-sm">
              {!moduleType.targetArchitectures || moduleType.targetArchitectures.length === 0 ? (
                <span className="text-zinc-300">All architectures</span>
              ) : (
                <div className="flex flex-wrap gap-1">
                  {moduleType.targetArchitectures.map((arch, idx) => (
                    <span key={idx} className="px-2 py-1 bg-zinc-700 border border-zinc-600 rounded text-xs text-zinc-300">
                      {arch}
                    </span>
                  ))}
                </div>
              )}
            </div>
          </div>

          {/* Dependencies */}
          {moduleType.dependencies && moduleType.dependencies.length > 0 && (
            <div>
              <h4 className="text-xs font-semibold text-zinc-400 mb-2">Required Inputs</h4>
              <div className="space-y-2">
                {moduleType.dependencies.map((dep, index) => (
                  <div key={index} className="p-2 bg-zinc-900 border border-zinc-700 rounded">
                    <div className="flex items-center gap-2 text-sm mb-1">
                      <div 
                        className="w-3 h-3 rounded-full flex-shrink-0"
                        style={{ backgroundColor: getTypeColor(dep.type) }}
                      />
                      <span className="text-zinc-300 font-medium">{dep.type}</span>
                    </div>
                    <p className="text-xs text-zinc-400 ml-5">{dep.description}</p>
                    
                    {/* Extension Requirements */}
                    {dep.requiredExtensions && dep.requiredExtensions.length > 0 && (
                      <div className="ml-5 mt-2 pt-2 border-t border-zinc-800">
                        <p className="text-xs text-zinc-500 mb-1">Required Extensions:</p>
                        <div className="flex flex-wrap gap-1">
                          {dep.requiredExtensions.map((extId, idx) => (
                            <span key={idx} className="px-1.5 py-0.5 bg-purple-500/20 border border-purple-500/40 rounded text-xs text-purple-300">
                              {extId}
                            </span>
                          ))}
                        </div>
                      </div>
                    )}
                  </div>
                ))}
              </div>
            </div>
          )}

          {/* Output */}
          <div>
            <h4 className="text-xs font-semibold text-zinc-400 mb-2">Output</h4>
            <div className="flex items-center gap-2 text-sm">
              <div 
                className="w-3 h-3 rounded-full flex-shrink-0"
                style={{ backgroundColor: getTypeColor(moduleType.type) }}
              />
              <span className="text-zinc-300">{moduleType.type}</span>
            </div>
          </div>

          {/* VTable */}
          {moduleType.vtable && moduleType.vtable.length > 0 && (
            <div>
              <h4 className="text-xs font-semibold text-zinc-400 mb-2 flex items-center gap-2">
                <Code className="size-3" />
                Function Interface ({moduleType.vtable.length})
              </h4>
              <div className="p-3 bg-zinc-900 border border-zinc-700 rounded text-xs font-mono space-y-2">
                {moduleType.vtable.map((func, i) => (
                  <div key={i} className="pb-2 border-b border-zinc-800 last:border-0 last:pb-0">
                    <code className="text-green-400 text-[11px] leading-tight break-all">
                      {func.signature}
                    </code>
                    <div className="text-zinc-500 mt-1 text-[11px]">{func.description}</div>
                  </div>
                ))}
              </div>
            </div>
          )}

          {/* Supported Extensions */}
          {moduleType.availableExtensions && moduleType.availableExtensions.length > 0 && (
            <div>
              <h4 className="text-xs font-semibold text-zinc-400 mb-2">Supported Extensions</h4>
              <div className="space-y-3">
                {moduleType.availableExtensions.map((ext, index) => (
                  <div key={index} className="p-3 bg-zinc-900 border border-zinc-700 rounded">
                    <div className="flex items-center gap-2 mb-1">
                      <span className="px-1.5 py-0.5 bg-purple-500/20 border border-purple-500/40 rounded text-xs text-purple-300 font-medium">
                        {ext.id}
                      </span>
                      <span className="text-sm text-zinc-300 font-medium">{ext.name}</span>
                    </div>
                    <p className="text-xs text-zinc-400 mb-2">{ext.description}</p>
                    
                    {/* Extension VTable */}
                    {ext.vtable && ext.vtable.length > 0 && (
                      <div className="mt-2 pt-2 border-t border-zinc-800">
                        <p className="text-xs text-zinc-500 mb-2 flex items-center gap-1">
                          <Code className="size-3" />
                          Functions ({ext.vtable.length})
                        </p>
                        <div className="space-y-2 pl-2">
                          {ext.vtable.map((func, i) => (
                            <div key={i} className="pb-2 border-b border-zinc-800 last:border-0 last:pb-0">
                              <code className="text-green-400 text-[10px] leading-tight break-all block">
                                {func.signature}
                              </code>
                              <div className="text-zinc-600 mt-1 text-[10px]">{func.description}</div>
                            </div>
                          ))}
                        </div>
                      </div>
                    )}
                  </div>
                ))}
              </div>
            </div>
          )}

          {/* Config Variables */}
          {moduleType.configVariables && moduleType.configVariables.length > 0 && (
            <div>
              <h4 className="text-xs font-semibold text-zinc-400 mb-2">Configuration Variables</h4>
              <div className="space-y-2">
                {moduleType.configVariables.map((config, index) => (
                  <div key={index} className="p-2 bg-zinc-900 border border-zinc-700 rounded">
                    <div className="flex items-center justify-between">
                      <span className="text-sm font-mono text-blue-400">{config.name}</span>
                      <span className="text-xs text-zinc-500">{config.type}</span>
                    </div>
                    <p className="text-xs text-zinc-400 mt-1">{config.description}</p>
                    {config.defaultValue && (
                      <p className="text-xs text-zinc-500 mt-1">
                        Default: <code className="text-green-400">{config.defaultValue}</code>
                      </p>
                    )}
                  </div>
                ))}
              </div>
            </div>
          )}
        </div>

        {/* Footer */}
        <div className="px-4 py-3 border-t border-zinc-700 flex items-center justify-between gap-2">
          <p className="text-xs text-zinc-500">
            Click Insert to add to canvas
          </p>
          <button
            onClick={handleInsert}
            className="px-4 py-2 bg-blue-600 hover:bg-blue-700 rounded-lg text-sm font-medium transition-colors flex items-center gap-2"
          >
            <Plus className="size-4" />
            Insert
          </button>
        </div>
      </div>
    </>
  );
}