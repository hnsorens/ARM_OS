import { useState } from 'react';
import { Cpu, X } from 'lucide-react';

interface ArchitectureSelectorProps {
  architecture: string;
  processor: string;
  onArchitectureChange: (arch: string, proc: string) => void;
  onClose: () => void;
}

// Architecture options with their processors
const architectureData = {
  'x86_64': {
    name: 'x86-64 (AMD64)',
    processors: [
      { id: 'intel_core_i7', name: 'Intel Core i7', family: 'Intel' },
      { id: 'intel_core_i5', name: 'Intel Core i5', family: 'Intel' },
      { id: 'intel_xeon', name: 'Intel Xeon', family: 'Intel' },
      { id: 'amd_ryzen_7', name: 'AMD Ryzen 7', family: 'AMD' },
      { id: 'amd_ryzen_5', name: 'AMD Ryzen 5', family: 'AMD' },
      { id: 'amd_epyc', name: 'AMD EPYC', family: 'AMD' },
    ],
  },
  'aarch64': {
    name: 'ARM 64-bit (AArch64)',
    processors: [
      { id: 'cortex_a72', name: 'ARM Cortex-A72', family: 'Cortex-A' },
      { id: 'cortex_a53', name: 'ARM Cortex-A53', family: 'Cortex-A' },
      { id: 'cortex_a76', name: 'ARM Cortex-A76', family: 'Cortex-A' },
      { id: 'cortex_a78', name: 'ARM Cortex-A78', family: 'Cortex-A' },
      { id: 'apple_m1', name: 'Apple M1', family: 'Apple Silicon' },
      { id: 'apple_m2', name: 'Apple M2', family: 'Apple Silicon' },
      { id: 'snapdragon_888', name: 'Qualcomm Snapdragon 888', family: 'Snapdragon' },
    ],
  },
  'riscv64': {
    name: 'RISC-V 64-bit',
    processors: [
      { id: 'sifive_u74', name: 'SiFive U74', family: 'SiFive' },
      { id: 'sifive_u54', name: 'SiFive U54', family: 'SiFive' },
      { id: 'sifive_e76', name: 'SiFive E76', family: 'SiFive' },
      { id: 'generic_rv64', name: 'Generic RV64GC', family: 'Generic' },
    ],
  },
  'arm': {
    name: 'ARM 32-bit',
    processors: [
      { id: 'cortex_a9', name: 'ARM Cortex-A9', family: 'Cortex-A' },
      { id: 'cortex_a7', name: 'ARM Cortex-A7', family: 'Cortex-A' },
      { id: 'cortex_m4', name: 'ARM Cortex-M4', family: 'Cortex-M' },
      { id: 'cortex_m7', name: 'ARM Cortex-M7', family: 'Cortex-M' },
    ],
  },
};

export function ArchitectureSelector({
  architecture,
  processor,
  onArchitectureChange,
  onClose,
}: ArchitectureSelectorProps) {
  const [selectedArch, setSelectedArch] = useState(architecture);
  const [selectedProc, setSelectedProc] = useState(processor);

  const currentArchData = architectureData[selectedArch as keyof typeof architectureData];

  const handleApply = () => {
    onArchitectureChange(selectedArch, selectedProc);
    onClose();
  };

  return (
    <div className="absolute top-16 left-1/2 -translate-x-1/2 w-[600px] bg-zinc-800 border border-zinc-700 rounded-lg shadow-2xl z-50">
      {/* Header */}
      <div className="px-4 py-3 border-b border-zinc-700 flex items-center justify-between">
        <div className="flex items-center gap-2">
          <Cpu className="size-5 text-blue-400" />
          <h2 className="font-semibold">Target Architecture & Processor</h2>
        </div>
        <button
          onClick={onClose}
          className="p-1 hover:bg-zinc-700 rounded transition-colors"
        >
          <X className="size-4" />
        </button>
      </div>

      {/* Content */}
      <div className="p-4">
        <div className="grid grid-cols-2 gap-4">
          {/* Architecture Selection */}
          <div>
            <label className="text-sm font-medium text-zinc-300 mb-2 block">
              Architecture
            </label>
            <div className="space-y-2">
              {Object.entries(architectureData).map(([key, data]) => (
                <button
                  key={key}
                  onClick={() => {
                    setSelectedArch(key);
                    // Set first processor as default
                    setSelectedProc(data.processors[0].id);
                  }}
                  className={`
                    w-full px-3 py-2 rounded-lg border text-left transition-colors
                    ${
                      selectedArch === key
                        ? 'bg-blue-500/20 border-blue-500/40 text-blue-300'
                        : 'bg-zinc-900 border-zinc-700 text-zinc-300 hover:bg-zinc-800'
                    }
                  `}
                >
                  <div className="text-sm font-medium">{data.name}</div>
                  <div className="text-xs text-zinc-500 mt-0.5">
                    {data.processors.length} processors available
                  </div>
                </button>
              ))}
            </div>
          </div>

          {/* Processor Selection */}
          <div>
            <label className="text-sm font-medium text-zinc-300 mb-2 block">
              Specific Processor
            </label>
            <div className="space-y-2 max-h-[320px] overflow-y-auto">
              {currentArchData?.processors.map((proc) => (
                <button
                  key={proc.id}
                  onClick={() => setSelectedProc(proc.id)}
                  className={`
                    w-full px-3 py-2 rounded-lg border text-left transition-colors
                    ${
                      selectedProc === proc.id
                        ? 'bg-green-500/20 border-green-500/40 text-green-300'
                        : 'bg-zinc-900 border-zinc-700 text-zinc-300 hover:bg-zinc-800'
                    }
                  `}
                >
                  <div className="text-sm font-medium">{proc.name}</div>
                  <div className="text-xs text-zinc-500">{proc.family}</div>
                </button>
              ))}
            </div>
          </div>
        </div>

        {/* Selection Info */}
        <div className="mt-4 p-3 bg-zinc-900 border border-zinc-700 rounded-lg">
          <div className="text-xs text-zinc-400 mb-1">Current Selection:</div>
          <div className="text-sm font-medium text-zinc-200">
            {architectureData[selectedArch as keyof typeof architectureData]?.name} →{' '}
            {
              currentArchData?.processors.find((p) => p.id === selectedProc)
                ?.name
            }
          </div>
          <div className="text-xs text-zinc-500 mt-2">
            This will affect instruction set, calling conventions, and available features during code generation.
          </div>
        </div>
      </div>

      {/* Footer */}
      <div className="px-4 py-3 border-t border-zinc-700 flex items-center justify-end gap-2">
        <button
          onClick={onClose}
          className="px-4 py-2 text-sm text-zinc-400 hover:text-zinc-200 transition-colors"
        >
          Cancel
        </button>
        <button
          onClick={handleApply}
          className="px-4 py-2 text-sm bg-blue-600 hover:bg-blue-700 rounded-lg transition-colors font-medium"
        >
          Apply
        </button>
      </div>
    </div>
  );
}
