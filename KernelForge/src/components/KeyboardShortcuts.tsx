import { X, Command } from 'lucide-react';

interface KeyboardShortcutsProps {
  onClose: () => void;
}

export function KeyboardShortcuts({ onClose }: KeyboardShortcutsProps) {
  const shortcuts = [
    { keys: ['Ctrl', 'S'], action: 'Save project' },
    { keys: ['Ctrl', 'O'], action: 'Open project' },
    { keys: ['Ctrl', 'N'], action: 'New project' },
    { keys: ['Ctrl', 'Z'], action: 'Undo' },
    { keys: ['Ctrl', 'Y'], action: 'Redo' },
    { keys: ['Delete'], action: 'Delete selected module' },
    { keys: ['Ctrl', 'D'], action: 'Duplicate selected module' },
    { keys: ['Ctrl', '+'], action: 'Zoom in' },
    { keys: ['Ctrl', '-'], action: 'Zoom out' },
    { keys: ['Ctrl', '0'], action: 'Reset zoom' },
    { keys: ['Space'], action: 'Pan canvas (hold)' },
    { keys: ['Ctrl', 'B'], action: 'Build OS' },
    { keys: ['Ctrl', 'R'], action: 'Run simulation' },
    { keys: ['Escape'], action: 'Deselect / Cancel' },
  ];

  return (
    <div className="absolute inset-0 bg-zinc-950/80 backdrop-blur-sm flex items-center justify-center z-50">
      <div className="w-[500px] bg-zinc-900 border border-zinc-800 rounded-xl shadow-2xl overflow-hidden">
        <div className="px-6 py-4 border-b border-zinc-800 flex items-center justify-between">
          <div className="flex items-center gap-2">
            <Command className="size-5 text-blue-400" />
            <h2 className="font-semibold">Keyboard Shortcuts</h2>
          </div>
          <button
            onClick={onClose}
            className="p-1 hover:bg-zinc-800 rounded transition-colors"
          >
            <X className="size-4" />
          </button>
        </div>

        <div className="p-6 max-h-[500px] overflow-y-auto">
          <div className="space-y-2">
            {shortcuts.map((shortcut, i) => (
              <div
                key={i}
                className="flex items-center justify-between py-2 px-3 rounded hover:bg-zinc-800 transition-colors"
              >
                <span className="text-sm text-zinc-300">{shortcut.action}</span>
                <div className="flex items-center gap-1">
                  {shortcut.keys.map((key, j) => (
                    <div key={j} className="flex items-center gap-1">
                      <kbd className="px-2 py-1 bg-zinc-800 border border-zinc-700 rounded text-xs font-mono">
                        {key}
                      </kbd>
                      {j < shortcut.keys.length - 1 && (
                        <span className="text-zinc-600">+</span>
                      )}
                    </div>
                  ))}
                </div>
              </div>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}
