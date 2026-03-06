import { BookOpen, X, Rocket, Server, Smartphone, Radio, Lock } from 'lucide-react';

interface TemplatesPanelProps {
  onClose: () => void;
  onSelectTemplate: (template: string) => void;
}

const templates = [
  {
    id: 'rtos',
    name: 'Real-Time OS',
    description: 'Minimal RTOS for embedded systems with deterministic scheduling',
    icon: Radio,
    modules: 8,
    color: 'blue',
  },
  {
    id: 'webserver',
    name: 'Web Server OS',
    description: 'Optimized for HTTP serving with networking and filesystem support',
    icon: Server,
    modules: 12,
    color: 'green',
  },
  {
    id: 'iot',
    name: 'IoT Sensor Hub',
    description: 'Lightweight OS for sensor data collection and transmission',
    icon: Smartphone,
    modules: 6,
    color: 'purple',
  },
  {
    id: 'microkernel',
    name: 'Microkernel Design',
    description: 'Minimal kernel with services in userspace for security',
    icon: Lock,
    modules: 10,
    color: 'amber',
  },
];

export function TemplatesPanel({ onClose, onSelectTemplate }: TemplatesPanelProps) {
  return (
    <div className="absolute inset-0 bg-zinc-950/80 backdrop-blur-sm flex items-center justify-center z-50">
      <div className="w-[800px] bg-zinc-900 border border-zinc-800 rounded-xl shadow-2xl overflow-hidden">
        <div className="px-6 py-4 border-b border-zinc-800 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <BookOpen className="size-6 text-blue-400" />
            <div>
              <h2 className="font-semibold text-lg">Project Templates</h2>
              <p className="text-sm text-zinc-400">Start with a pre-configured OS design</p>
            </div>
          </div>
          <button
            onClick={onClose}
            className="p-2 hover:bg-zinc-800 rounded transition-colors"
          >
            <X className="size-5" />
          </button>
        </div>

        <div className="p-6 grid grid-cols-2 gap-4">
          {templates.map(template => {
            const Icon = template.icon;
            const colorClasses = {
              blue: 'bg-blue-500/20 border-blue-500/40 text-blue-400',
              green: 'bg-green-500/20 border-green-500/40 text-green-400',
              purple: 'bg-purple-500/20 border-purple-500/40 text-purple-400',
              amber: 'bg-amber-500/20 border-amber-500/40 text-amber-400',
            };

            return (
              <button
                key={template.id}
                onClick={() => onSelectTemplate(template.id)}
                className="p-4 bg-zinc-800 border border-zinc-700 rounded-lg hover:bg-zinc-750 hover:border-zinc-600 transition-all text-left group"
              >
                <div className="flex items-start gap-3">
                  <div className={`p-3 rounded-lg border ${colorClasses[template.color as keyof typeof colorClasses]}`}>
                    <Icon className="size-6" />
                  </div>
                  <div className="flex-1">
                    <h3 className="font-medium mb-1 group-hover:text-blue-400 transition-colors">
                      {template.name}
                    </h3>
                    <p className="text-sm text-zinc-400 mb-3">
                      {template.description}
                    </p>
                    <div className="flex items-center gap-2 text-xs text-zinc-500">
                      <span>{template.modules} modules</span>
                      <span>•</span>
                      <span>Ready to build</span>
                    </div>
                  </div>
                </div>
              </button>
            );
          })}
        </div>

        <div className="px-6 py-4 border-t border-zinc-800 bg-zinc-900/50">
          <div className="flex items-center justify-between">
            <p className="text-sm text-zinc-500">
              You can customize any template after loading
            </p>
            <button
              onClick={onClose}
              className="px-4 py-2 text-sm bg-zinc-800 hover:bg-zinc-700 rounded-lg transition-colors"
            >
              Start from Scratch
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
