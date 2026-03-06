import { useState } from 'react';
import { Terminal, X, ChevronDown, ChevronRight, Download, Copy, Check } from 'lucide-react';

export function BuildOutputPanel() {
  const [expanded, setExpanded] = useState(false);
  const [copied, setCopied] = useState(false);

  const buildOutput = `
[1/8] Generating kernel configuration...
[2/8] Validating module dependencies...
[3/8] Compiling memory manager (paging_memory)...
[4/8] Compiling scheduler (realtime_scheduler)...
[5/8] Compiling filesystem (ext4_filesystem)...
[6/8] Linking kernel modules...
[7/8] Creating boot image (UEFI)...
[8/8] Build complete!

Output: ./build/kernelforge_os.iso
Size: 24.8 MB
Target: x86_64-unknown-uefi
Boot method: UEFI
`.trim();

  const handleCopy = () => {
    navigator.clipboard.writeText(buildOutput);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  if (!expanded) {
    return (
      <button
        onClick={() => setExpanded(true)}
        className="absolute bottom-4 left-1/2 -translate-x-1/2 px-4 py-2 bg-zinc-800 border border-zinc-700 rounded-lg text-sm flex items-center gap-2 hover:bg-zinc-750 transition-colors"
      >
        <Terminal className="size-4" />
        Build Output
        <ChevronRight className="size-4" />
      </button>
    );
  }

  return (
    <div className="absolute bottom-4 left-1/2 -translate-x-1/2 w-[600px] bg-zinc-800 border border-zinc-700 rounded-lg shadow-xl overflow-hidden">
      <div className="px-3 py-2 bg-zinc-900 border-b border-zinc-700 flex items-center justify-between">
        <div className="flex items-center gap-2">
          <Terminal className="size-4 text-green-500" />
          <span className="text-sm font-medium">Build Output</span>
        </div>
        <div className="flex items-center gap-1">
          <button
            onClick={handleCopy}
            className="p-1 hover:bg-zinc-800 rounded transition-colors"
            title="Copy to clipboard"
          >
            {copied ? <Check className="size-3 text-green-500" /> : <Copy className="size-3" />}
          </button>
          <button
            className="p-1 hover:bg-zinc-800 rounded transition-colors"
            title="Download build log"
          >
            <Download className="size-3" />
          </button>
          <button
            onClick={() => setExpanded(false)}
            className="p-1 hover:bg-zinc-800 rounded transition-colors"
          >
            <ChevronDown className="size-3" />
          </button>
        </div>
      </div>

      <div className="p-3 max-h-64 overflow-y-auto bg-zinc-950">
        <pre className="text-xs text-green-400 font-mono whitespace-pre-wrap">
          {buildOutput}
        </pre>
      </div>
    </div>
  );
}
