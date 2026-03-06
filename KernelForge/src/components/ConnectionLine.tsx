interface ConnectionLineProps {
  from: { x: number; y: number };
  to: { x: number; y: number };
  type: 'data' | 'control' | 'irq' | 'memory';
  active?: boolean;
}

export function ConnectionLine({ from, to, type, active }: ConnectionLineProps) {
  const colors = {
    data: '#3b82f6',
    control: '#10b981',
    irq: '#eab308',
    memory: '#a855f7',
  };

  const color = colors[type];
  
  // Calculate control points for bezier curve
  const midX = (from.x + to.x) / 2;
  const dx = to.x - from.x;
  const dy = to.y - from.y;
  const offset = Math.min(Math.abs(dx) * 0.5, 100);

  const path = `M ${from.x + 128} ${from.y + 40} 
                C ${from.x + 128 + offset} ${from.y + 40}, 
                  ${to.x + 128 - offset} ${to.y + 40}, 
                  ${to.x + 128} ${to.y + 40}`;

  return (
    <>
      <path
        d={path}
        fill="none"
        stroke={color}
        strokeWidth="2"
        strokeOpacity={active ? 1 : 0.5}
        className={active ? 'animate-pulse' : ''}
      />
      {active && (
        <circle r="4" fill={color} className="animate-flow">
          <animateMotion dur="2s" repeatCount="indefinite" path={path} />
        </circle>
      )}
    </>
  );
}
