export function Logo() {
  return (
    <div className="flex items-center gap-3">
      {/* Animated Logo Icon */}
      <div className="relative w-10 h-10">
        {/* Outer ring */}
        <svg className="absolute inset-0 w-10 h-10" viewBox="0 0 40 40" fill="none">
          <circle 
            cx="20" 
            cy="20" 
            r="18" 
            stroke="url(#gradient1)" 
            strokeWidth="2" 
            strokeLinecap="round"
            strokeDasharray="60 30"
          />
          <defs>
            <linearGradient id="gradient1" x1="0%" y1="0%" x2="100%" y2="100%">
              <stop offset="0%" stopColor="#3b82f6" />
              <stop offset="100%" stopColor="#8b5cf6" />
            </linearGradient>
          </defs>
        </svg>
        
        {/* Inner ring */}
        <svg className="absolute inset-0 w-10 h-10" viewBox="0 0 40 40" fill="none">
          <circle 
            cx="20" 
            cy="20" 
            r="12" 
            stroke="url(#gradient2)" 
            strokeWidth="2" 
            strokeLinecap="round"
            strokeDasharray="40 20"
          />
          <defs>
            <linearGradient id="gradient2" x1="100%" y1="0%" x2="0%" y2="100%">
              <stop offset="0%" stopColor="#10b981" />
              <stop offset="100%" stopColor="#3b82f6" />
            </linearGradient>
          </defs>
        </svg>
        
        {/* Center core */}
        <div className="absolute inset-0 flex items-center justify-center">
          <div className="w-4 h-4 bg-gradient-to-br from-blue-400 to-purple-600 rounded-sm shadow-lg shadow-blue-500/50" />
        </div>
        
        {/* Corner accents */}
        <div className="absolute top-0 right-0 w-1.5 h-1.5 bg-blue-400 rounded-full opacity-75" />
        <div className="absolute bottom-0 left-0 w-1.5 h-1.5 bg-purple-400 rounded-full opacity-75" />
      </div>
      
      {/* Text */}
      <div className="flex flex-col">
        <h1 className="font-bold text-lg leading-none text-zinc-100">
          KernelForge
        </h1>
        <span className="text-xs text-zinc-500 font-medium tracking-wider">STUDIO</span>
      </div>
    </div>
  );
}