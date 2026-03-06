import { Package, ArrowRight } from 'lucide-react';

export function EmptyState() {
  return (
    <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
      <div className="text-center max-w-md">
        <div className="inline-flex p-4 bg-zinc-800 border border-zinc-700 rounded-2xl mb-4">
          <Package className="size-12 text-zinc-600" />
        </div>
        <h3 className="text-xl font-semibold mb-2">Start Building Your OS</h3>
        <p className="text-zinc-400 mb-4">
          Drag modules from the library on the left to begin designing your custom operating system
        </p>
        <div className="flex items-center justify-center gap-2 text-sm text-zinc-500">
          <span>Module Library</span>
          <ArrowRight className="size-4" />
          <span>Canvas</span>
        </div>
      </div>
    </div>
  );
}
