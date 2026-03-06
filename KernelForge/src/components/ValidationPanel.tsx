import { useState, useMemo } from 'react';
import { AlertTriangle, CheckCircle, Info, X, ChevronDown, ChevronRight } from 'lucide-react';
import { type Module } from '../types';
import { validateModules } from '../utils/validation';

interface ValidationPanelProps {
  modules: Module[];
  onClose: () => void;
  architecture: string;
  processor: string;
}

export function ValidationPanel({ modules, onClose, architecture, processor }: ValidationPanelProps) {
  const [expanded, setExpanded] = useState(true);

  // Compute validation using shared validation logic
  const validation = useMemo(() => {
    return validateModules(modules, architecture);
  }, [modules, architecture]);

  const { errors, warnings } = validation;

  const info = [
    { message: `${modules.length} modules in design` },
    { message: `Target: ${architecture}, ${processor} boot` },
  ];

  if (!expanded) {
    return (
      <button
        onClick={() => setExpanded(true)}
        className="absolute bottom-4 left-4 px-3 py-2 bg-zinc-800 border border-zinc-700 rounded-lg text-sm flex items-center gap-2 hover:bg-zinc-750 transition-colors"
      >
        <ChevronRight className="size-4" />
        Validation
        {errors.length > 0 && (
          <span className="px-1.5 py-0.5 bg-red-600 text-white text-xs rounded">
            {errors.length}
          </span>
        )}
        {warnings.length > 0 && (
          <span className="px-1.5 py-0.5 bg-yellow-600 text-white text-xs rounded">
            {warnings.length}
          </span>
        )}
      </button>
    );
  }

  return (
    <div className="absolute bottom-4 left-4 w-96 bg-zinc-800 border border-zinc-700 rounded-lg shadow-xl overflow-hidden">
      <div className="px-3 py-2 bg-zinc-900 border-b border-zinc-700 flex items-center justify-between">
        <div className="flex items-center gap-2">
          <ChevronDown className="size-4" />
          <span className="text-sm font-medium">Validation</span>
        </div>
        <button
          onClick={() => setExpanded(false)}
          className="p-1 hover:bg-zinc-800 rounded transition-colors"
        >
          <X className="size-3" />
        </button>
      </div>

      <div className="max-h-64 overflow-y-auto">
        {errors.length > 0 && (
          <div className="p-3 border-b border-zinc-700">
            <div className="flex items-center gap-2 mb-2">
              <AlertTriangle className="size-4 text-red-500" />
              <span className="text-sm font-medium text-red-500">
                {errors.length} Error{errors.length !== 1 ? 's' : ''}
              </span>
            </div>
            <div className="space-y-2">
              {errors.map((error, i) => (
                <div key={i} className="text-xs text-zinc-300 pl-6">
                  <span className="font-medium">{error.module.name}:</span>{' '}
                  {error.message}
                </div>
              ))}
            </div>
          </div>
        )}

        {warnings.length > 0 && (
          <div className="p-3 border-b border-zinc-700">
            <div className="flex items-center gap-2 mb-2">
              <AlertTriangle className="size-4 text-yellow-500" />
              <span className="text-sm font-medium text-yellow-500">
                {warnings.length} Warning{warnings.length !== 1 ? 's' : ''}
              </span>
            </div>
            <div className="space-y-2">
              {warnings.map((warning, i) => (
                <div key={i} className="text-xs text-zinc-300 pl-6">
                  <span className="font-medium">{warning.module.name}:</span>{' '}
                  {warning.message}
                </div>
              ))}
            </div>
          </div>
        )}

        {errors.length === 0 && warnings.length === 0 && (
          <div className="p-3 border-b border-zinc-700">
            <div className="flex items-center gap-2">
              <CheckCircle className="size-4 text-green-500" />
              <span className="text-sm text-green-500">All modules validated</span>
            </div>
          </div>
        )}

        <div className="p-3">
          <div className="flex items-center gap-2 mb-2">
            <Info className="size-4 text-blue-500" />
            <span className="text-sm font-medium text-blue-500">Information</span>
          </div>
          <div className="space-y-1">
            {info.map((item, i) => (
              <div key={i} className="text-xs text-zinc-400 pl-6">
                {item.message}
              </div>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}