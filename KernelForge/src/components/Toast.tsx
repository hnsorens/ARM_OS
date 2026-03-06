import { useEffect, useState } from 'react';
import { CheckCircle, AlertCircle, X } from 'lucide-react';

interface ToastProps {
  message: string;
  description?: string;
  type: 'success' | 'error';
  duration?: number;
  onClose: () => void;
}

export function Toast({ message, description, type, duration = 4000, onClose }: ToastProps) {
  const [isExiting, setIsExiting] = useState(false);

  useEffect(() => {
    const timer = setTimeout(() => {
      setIsExiting(true);
      setTimeout(onClose, 300);
    }, duration);

    return () => clearTimeout(timer);
  }, [duration, onClose]);

  const handleClose = () => {
    setIsExiting(true);
    setTimeout(onClose, 300);
  };

  return (
    <div
      className={`
        bg-zinc-900 border rounded-lg shadow-lg
        min-w-[300px] max-w-[400px] p-4
        transition-all duration-300
        ${isExiting ? 'opacity-0 translate-x-4' : 'opacity-100 translate-x-0'}
        ${type === 'success' ? 'border-green-600' : 'border-red-600'}
      `}
    >
      <div className="flex items-start gap-3">
        {type === 'success' ? (
          <CheckCircle className="size-5 text-green-500 flex-shrink-0 mt-0.5" />
        ) : (
          <AlertCircle className="size-5 text-red-500 flex-shrink-0 mt-0.5" />
        )}
        
        <div className="flex-1 min-w-0">
          <div className="font-semibold text-sm text-zinc-100">{message}</div>
          {description && (
            <div className="text-xs text-zinc-400 mt-1 whitespace-pre-wrap break-words">
              {description}
            </div>
          )}
        </div>

        <button
          onClick={handleClose}
          className="text-zinc-400 hover:text-zinc-100 transition-colors flex-shrink-0"
        >
          <X className="size-4" />
        </button>
      </div>
    </div>
  );
}

// Toast manager for global toast notifications
export class ToastManager {
  private static listeners: ((toast: ToastData | null) => void)[] = [];
  private static currentId = 0;

  static subscribe(listener: (toast: ToastData | null) => void) {
    this.listeners.push(listener);
    return () => {
      this.listeners = this.listeners.filter(l => l !== listener);
    };
  }

  static show(toast: Omit<ToastData, 'id'>) {
    const toastWithId = { ...toast, id: this.currentId++ };
    this.listeners.forEach(listener => listener(toastWithId));
  }

  static success(message: string, options?: { description?: string; duration?: number }) {
    this.show({ type: 'success', message, ...options });
  }

  static error(message: string, options?: { description?: string; duration?: number }) {
    this.show({ type: 'error', message, ...options });
  }
}

interface ToastData {
  id: number;
  type: 'success' | 'error';
  message: string;
  description?: string;
  duration?: number;
}

export function ToastContainer() {
  const [toasts, setToasts] = useState<ToastData[]>([]);

  useEffect(() => {
    const unsubscribe = ToastManager.subscribe((toast) => {
      if (toast) {
        setToasts(prev => [...prev, toast]);
      }
    });

    return unsubscribe;
  }, []);

  const removeToast = (id: number) => {
    setToasts(prev => prev.filter(t => t.id !== id));
  };

  return (
    <div className="fixed bottom-0 right-0 z-[100] pointer-events-none">
      <div className="flex flex-col-reverse gap-2 p-4 pointer-events-auto">
        {toasts.map((toast, index) => (
          <div key={toast.id} style={{ marginBottom: index > 0 ? '8px' : '0' }}>
            <Toast
              message={toast.message}
              description={toast.description}
              type={toast.type}
              duration={toast.duration}
              onClose={() => removeToast(toast.id)}
            />
          </div>
        ))}
      </div>
    </div>
  );
}