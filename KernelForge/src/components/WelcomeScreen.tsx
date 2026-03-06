import { Cpu, BookOpen, FolderOpen, Sparkles, ArrowRight } from 'lucide-react';
import { Logo } from './Logo';

interface WelcomeScreenProps {
  onNewProject: () => void;
  onOpenTemplate: () => void;
  onClose: () => void;
}

export function WelcomeScreen({ onNewProject, onOpenTemplate, onClose }: WelcomeScreenProps) {
  return (
    <div className="absolute inset-0 bg-zinc-950/95 backdrop-blur-sm flex items-center justify-center z-50">
      <div className="w-[700px] bg-zinc-900 border border-zinc-800 rounded-2xl shadow-2xl overflow-hidden">
        {/* Header */}
        <div className="px-8 py-6 border-b border-zinc-800 bg-gradient-to-r from-blue-600/20 to-purple-600/20">
          <div className="flex items-center justify-center mb-2">
            <Logo />
          </div>
          <p className="text-zinc-400 text-center text-sm">Visual OS Design IDE</p>
        </div>

        {/* Content */}
        <div className="p-8">
          <h2 className="text-lg font-semibold mb-4">Get Started</h2>
          
          <div className="space-y-3 mb-6">
            <button
              onClick={() => {
                onNewProject();
                onClose();
              }}
              className="w-full p-4 bg-zinc-800 hover:bg-zinc-750 border border-zinc-700 hover:border-zinc-600 rounded-lg transition-all text-left group"
            >
              <div className="flex items-center justify-between">
                <div className="flex items-center gap-3">
                  <div className="p-2 bg-blue-600/20 border border-blue-600/40 text-blue-400 rounded-lg">
                    <Sparkles className="size-5" />
                  </div>
                  <div>
                    <h3 className="font-medium group-hover:text-blue-400 transition-colors">
                      New Project
                    </h3>
                    <p className="text-sm text-zinc-500">
                      Start with a blank canvas
                    </p>
                  </div>
                </div>
                <ArrowRight className="size-5 text-zinc-600 group-hover:text-blue-400 group-hover:translate-x-1 transition-all" />
              </div>
            </button>

            <button
              onClick={() => {
                onOpenTemplate();
              }}
              className="w-full p-4 bg-zinc-800 hover:bg-zinc-750 border border-zinc-700 hover:border-zinc-600 rounded-lg transition-all text-left group"
            >
              <div className="flex items-center justify-between">
                <div className="flex items-center gap-3">
                  <div className="p-2 bg-green-600/20 border border-green-600/40 text-green-400 rounded-lg">
                    <BookOpen className="size-5" />
                  </div>
                  <div>
                    <h3 className="font-medium group-hover:text-green-400 transition-colors">
                      Browse Templates
                    </h3>
                    <p className="text-sm text-zinc-500">
                      Start from pre-configured OS designs
                    </p>
                  </div>
                </div>
                <ArrowRight className="size-5 text-zinc-600 group-hover:text-green-400 group-hover:translate-x-1 transition-all" />
              </div>
            </button>

            <button
              className="w-full p-4 bg-zinc-800 hover:bg-zinc-750 border border-zinc-700 hover:border-zinc-600 rounded-lg transition-all text-left group"
            >
              <div className="flex items-center justify-between">
                <div className="flex items-center gap-3">
                  <div className="p-2 bg-purple-600/20 border border-purple-600/40 text-purple-400 rounded-lg">
                    <FolderOpen className="size-5" />
                  </div>
                  <div>
                    <h3 className="font-medium group-hover:text-purple-400 transition-colors">
                      Open Project
                    </h3>
                    <p className="text-sm text-zinc-500">
                      Load an existing .kfproject file
                    </p>
                  </div>
                </div>
                <ArrowRight className="size-5 text-zinc-600 group-hover:text-purple-400 group-hover:translate-x-1 transition-all" />
              </div>
            </button>
          </div>

          <div className="pt-4 border-t border-zinc-800">
            <h3 className="text-sm font-medium mb-2">Quick Tips</h3>
            <ul className="space-y-1 text-sm text-zinc-400">
              <li>• Drag modules from the library onto the canvas</li>
              <li>• Click ports to create connections between modules</li>
              <li>• Select a module to configure its properties</li>
              <li>• Use the Build button to generate your OS</li>
            </ul>
          </div>
        </div>

        {/* Footer */}
        <div className="px-8 py-4 border-t border-zinc-800 bg-zinc-900/50">
          <div className="flex items-center justify-between text-sm text-zinc-500">
            <span>Version 1.0.0 (Prototype)</span>
            <button
              onClick={onClose}
              className="text-blue-400 hover:text-blue-300 transition-colors"
            >
              Skip →
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}