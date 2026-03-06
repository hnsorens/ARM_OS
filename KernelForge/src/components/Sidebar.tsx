import { Package, Settings } from 'lucide-react';

interface SidebarProps {
  showLibrary: boolean;
  setShowLibrary: (show: boolean) => void;
  showProperties: boolean;
  setShowProperties: (show: boolean) => void;
}

export function Sidebar({ showLibrary, setShowLibrary, showProperties, setShowProperties }: SidebarProps) {
  return (
    <div className="bg-zinc-900 border-r border-zinc-800 flex flex-col w-16 select-none">
      <button
        onClick={() => setShowLibrary(!showLibrary)}
        className={`w-10 h-10 rounded flex items-center justify-center transition-colors relative group ${
          showLibrary ? 'bg-zinc-700 text-white' : 'hover:bg-zinc-800 text-zinc-400'
        }`}
        title="Module Library"
      >
        <Package className="size-5" />
        
        {/* Tooltip */}
        <div className="absolute left-full ml-2 px-2 py-1 bg-zinc-800 text-white text-xs rounded opacity-0 group-hover:opacity-100 pointer-events-none whitespace-nowrap z-50 transition-opacity">
          Module Library
        </div>
      </button>
      
      <button
        onClick={() => setShowProperties(!showProperties)}
        className={`w-10 h-10 rounded flex items-center justify-center transition-colors relative group ${
          showProperties ? 'bg-zinc-700 text-white' : 'hover:bg-zinc-800 text-zinc-400'
        }`}
        title="Properties"
      >
        <Settings className="size-5" />
        
        {/* Tooltip */}
        <div className="absolute left-full ml-2 px-2 py-1 bg-zinc-800 text-white text-xs rounded opacity-0 group-hover:opacity-100 pointer-events-none whitespace-nowrap z-50 transition-opacity">
          Properties
        </div>
      </button>
    </div>
  );
}