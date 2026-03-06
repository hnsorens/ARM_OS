import { Activity, Cpu, HardDrive, Network, Zap } from 'lucide-react';

interface SimulationOverlayProps {
  active: boolean;
  moduleCount: number;
}

export function SimulationOverlay({ active, moduleCount }: SimulationOverlayProps) {
  if (!active) return null;

  return (
    <div className="absolute top-4 left-1/2 -translate-x-1/2 z-20">
      <div className="bg-green-600 text-white px-4 py-2 rounded-lg shadow-lg flex items-center gap-3">
        <Activity className="size-4 animate-pulse" />
        <span className="text-sm font-medium">Simulation Running</span>
        <div className="flex items-center gap-4 ml-4 text-xs">
          <div className="flex items-center gap-1">
            <Cpu className="size-3" />
            <span>{Math.floor(Math.random() * 60 + 20)}%</span>
          </div>
          <div className="flex items-center gap-1">
            <HardDrive className="size-3" />
            <span>{Math.floor(Math.random() * 512)}MB</span>
          </div>
          <div className="flex items-center gap-1">
            <Network className="size-3" />
            <span>{Math.floor(Math.random() * 100)}KB/s</span>
          </div>
          <div className="flex items-center gap-1">
            <Zap className="size-3" />
            <span>{moduleCount} modules</span>
          </div>
        </div>
      </div>
    </div>
  );
}
