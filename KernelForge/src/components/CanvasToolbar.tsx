import { ZoomIn, ZoomOut, Maximize2, Grid3x3 } from 'lucide-react';

interface CanvasToolbarProps {
  zoom: number;
  setZoom: (zoom: number) => void;
  showGrid: boolean;
  setShowGrid: (show: boolean) => void;
}

export function CanvasToolbar({
  zoom,
  setZoom,
  showGrid,
  setShowGrid,
}: CanvasToolbarProps) {
  return (
    <div className="absolute top-4 right-4 flex flex-col gap-2 z-10 select-none">
      {/* Zoom controls */}
      <div className="bg-zinc-800 border border-zinc-700 rounded-lg p-1 flex flex-col gap-1">
        <button
          onClick={() => setZoom(Math.min(2, zoom * 1.2))}
          className="p-2 hover:bg-zinc-700 rounded transition-colors"
          title="Zoom In"
        >
          <ZoomIn className="size-4" />
        </button>
        <div className="text-xs text-center text-zinc-400 py-1">
          {Math.round(zoom * 100)}%
        </div>
        <button
          onClick={() => setZoom(Math.max(0.25, zoom / 1.2))}
          className="p-2 hover:bg-zinc-700 rounded transition-colors"
          title="Zoom Out"
        >
          <ZoomOut className="size-4" />
        </button>
        <button
          onClick={() => setZoom(1)}
          className="p-2 hover:bg-zinc-700 rounded transition-colors"
          title="Reset Zoom"
        >
          <Maximize2 className="size-4" />
        </button>
      </div>

      {/* View options */}
      <div className="bg-zinc-800 border border-zinc-700 rounded-lg p-1 flex flex-col gap-1">
        <button
          onClick={() => setShowGrid(!showGrid)}
          className={`p-2 rounded transition-colors ${
            showGrid ? 'bg-zinc-700' : 'hover:bg-zinc-700'
          }`}
          title="Toggle Grid"
        >
          <Grid3x3 className="size-4" />
        </button>
      </div>
    </div>
  );
}