import { type Module } from "../types";
import { useRef, useEffect, useState } from "react";
import { motion } from "motion/react";
import { getModuleCategories } from "../data/loader";

interface Point {
  x: number;
  y: number;
}

interface DependencyArrowProps {
  fromModule: Module;
  toModule: Module;
  depType: string;
  allModules: Module[];
  routingStyle: "straight" | "orthogonal";
  typeColors: Record<string, string>;
}

// Calculate connection points on the edge circles
function getConnectionPoints(
  fromModule: Module,
  toModule: Module,
  depType: string,
  allModules: Module[],
) {
  const fromNodeWidth = 256; // w-64 = 16rem = 256px

  // Need to get the module type data to find which dependency index this is
  // Import module categories to get the dependency list
  const moduleCategories = getModuleCategories();

  // Find the module type definition for the target module to get dependency count
  let toModuleDependencyIndex = 0;
  let toModuleDependencyCount = 0;
  Object.values(moduleCategories).forEach((category: any) => {
    category.forEach((modType: any) => {
      if (
        modType.name === toModule.name &&
        modType.dependencies
      ) {
        toModuleDependencyCount = modType.dependencies.length;
        // Find the index of this dependency type
        const depIndex = modType.dependencies.findIndex(
          (dep: any) => dep.type === depType,
        );
        if (depIndex >= 0) {
          toModuleDependencyIndex = depIndex;
        }
      }
    });
  });

  // INPUT CIRCLE POSITION (left edge of target module)
  // Body has p-3 (12px padding), input row has pl-2 (8px), circle is absolute left-0 with -translate-x-1/2 (-6px)
  // X position: 12 (body padding) + 0 (left-0) - 6 (translate) = 6px from node edge
  const toX = toModule.position.x + 0;

  // Y position calculation for inputs:
  // Header: ~40px, Body padding: 12px, Type section: ~44px, border: 1px
  // Input section pt-2: 8px, "Inputs:" label: ~20px, First input starts here
  // Each input: ~22px (16px text height + 6px spacing)
  const baseInputY = 131.5;
  const inputSpacing = 22;
  const toY =
    toModule.position.y +
    baseInputY +
    toModuleDependencyIndex * inputSpacing;

  // OUTPUT CIRCLE POSITION (right edge of source module)
  // Body has p-3 (12px padding), output row has pr-2 (8px), circle is absolute right-0 with translate-x-1/2 (+6px)
  // X position: 256 (node width) - 12 (body padding) + 0 (right-0) + 6 (translate) = 250px from node edge
  const fromX = fromModule.position.x + 255;

  // Y position for output depends on how many inputs the source module has
  let fromModuleDependencyCount = 0;
  Object.values(moduleCategories).forEach((category: any) => {
    category.forEach((modType: any) => {
      if (
        modType.name === fromModule.name &&
        modType.dependencies
      ) {
        fromModuleDependencyCount = modType.dependencies.length;
      }
    });
  });

  // Output Y: Header + body padding + type section + border + (inputs if any) + border + output section pt-2 + label + half of text
  let outputY = 95; // Base position same as first input
  if (fromModuleDependencyCount > 0) {
    outputY +=
      fromModuleDependencyCount * inputSpacing + 1 + 8 + 20 + 8; // inputs + border + pt-2 + label + half text
  } else {
    outputY += 1 + 8 + 20 + 8; // No inputs, just border + pt-2 + label + half text
  }
  const fromY = fromModule.position.y + outputY;

  return { fromX, fromY, toX, toY };
}

// Check if a point is inside a node's bounding box (with padding)
function isPointInNode(
  point: Point,
  module: Module,
  padding: number = 20,
): boolean {
  const nodeWidth = 256;
  const nodeHeight = 140;

  return (
    point.x >= module.position.x - padding &&
    point.x <= module.position.x + nodeWidth + padding &&
    point.y >= module.position.y - padding &&
    point.y <= module.position.y + nodeHeight + padding
  );
}

// Generate orthogonal path with collision avoidance
function generateOrthogonalPath(
  fromX: number,
  fromY: number,
  toX: number,
  toY: number,
  fromModule: Module,
  toModule: Module,
  allModules: Module[],
): string {
  const points: Point[] = [{ x: fromX, y: fromY }];

  // Simple orthogonal routing with 3 segments
  const midX = (fromX + toX) / 2;

  // Check if middle path intersects with any nodes
  let offset = 0;
  const otherModules = allModules.filter(
    (m) => m.id !== fromModule.id && m.id !== toModule.id,
  );

  // Try to find a clear vertical line
  for (const testOffset of [0, 50, -50, 100, -100, 150, -150]) {
    const testMidX = midX + testOffset;
    let collides = false;

    for (const module of otherModules) {
      const testPoint = { x: testMidX, y: (fromY + toY) / 2 };
      if (isPointInNode(testPoint, module, 30)) {
        collides = true;
        break;
      }
    }

    if (!collides) {
      offset = testOffset;
      break;
    }
  }

  const adjustedMidX = midX + offset;

  // Create path: right from source, down/up to midline, right/left to target height, left to target
  points.push({ x: adjustedMidX, y: fromY });
  points.push({ x: adjustedMidX, y: toY });
  points.push({ x: toX, y: toY });

  // Convert to SVG path
  let path = `M ${points[0].x} ${points[0].y}`;
  for (let i = 1; i < points.length; i++) {
    path += ` L ${points[i].x} ${points[i].y}`;
  }

  return path;
}

export function DependencyArrow({
  fromModule,
  toModule,
  depType,
  allModules,
  routingStyle,
  typeColors,
}: DependencyArrowProps) {
  const pathRef = useRef<SVGPathElement>(null);
  const [pathLength, setPathLength] = useState<number | null>(null);

  useEffect(() => {
    if (pathRef.current) {
      const length = pathRef.current.getTotalLength();
      setPathLength(length);
    }
  }, [fromModule.position, toModule.position, routingStyle]);

  const { fromX, fromY, toX, toY } = getConnectionPoints(
    fromModule,
    toModule,
    depType,
    allModules,
  );

  // Get color based on the dependency type (the module being depended upon)
  const color = typeColors[fromModule.type] || "#6b7280";

  let pathD: string;
  let arrowX: number;
  let arrowY: number;
  let angle: number;
  let sourceArrowAngle: number; // Angle for arrow at source

  if (routingStyle === "orthogonal") {
    pathD = generateOrthogonalPath(
      fromX,
      fromY,
      toX,
      toY,
      fromModule,
      toModule,
      allModules,
    );

    // Arrow at target points to the left (into the target node)
    arrowX = toX;
    arrowY = toY;
    angle = Math.PI; // Pointing left
    
    // Arrow at source also points to the left (into the source node from the right)
    sourceArrowAngle = Math.PI; // Pointing left
  } else {
    // Straight line
    pathD = `M ${fromX} ${fromY} L ${toX} ${toY}`;
    const dx = toX - fromX;
    const dy = toY - fromY;
    angle = Math.atan2(dy, dx);
    arrowX = toX;
    arrowY = toY;
    
    // Source arrow points in same direction as target arrow
    sourceArrowAngle = angle;
  }

  const arrowSize = 8;

  // Don't render until we have path length calculated
  if (pathLength === null) {
    return (
      <g>
        <path
          ref={pathRef}
          d={pathD}
          stroke="transparent"
          strokeWidth="2.5"
          fill="none"
        />
      </g>
    );
  }

  return (
    <motion.g
      initial={{ opacity: 1 }}
      exit={{ opacity: 1 }}
      transition={{ duration: 0 }}
    >
      {/* Main path with snake slithering animation */}
      <motion.path
        ref={pathRef}
        d={pathD}
        stroke={color}
        strokeWidth="2.5"
        fill="none"
        strokeDasharray={pathLength}
        initial={{ strokeDashoffset: pathLength }}
        animate={{ 
          strokeDashoffset: 0,
          transition: { duration: 0.6, ease: "easeInOut" } 
        }}
        exit={{ 
          strokeDashoffset: pathLength,
          transition: { duration: 0.35, ease: "easeIn" } 
        }}
      />

      {/* Arrow head at target (destination) - pointing INTO target */}
      <motion.path
        d={`M ${arrowX} ${arrowY} 
            L ${arrowX - arrowSize * Math.cos(angle - Math.PI / 6)} ${arrowY - arrowSize * Math.sin(angle - Math.PI / 6)} 
            L ${arrowX - arrowSize * Math.cos(angle + Math.PI / 6)} ${arrowY - arrowSize * Math.sin(angle + Math.PI / 6)} 
            Z`}
        fill={color}
        initial={{ opacity: 0 }}
        animate={{ opacity: 1 }}
        exit={{ opacity: 0 }}
        transition={{ 
          duration: 0.2,
          delay: 0.5,
          ease: "easeInOut"
        }}
      />
      
      {/* Arrow head at source - pointing OUT OF source */}
      <motion.path
        d={`M ${fromX} ${fromY} 
            L ${fromX + arrowSize * Math.cos(sourceArrowAngle - Math.PI / 6)} ${fromY + arrowSize * Math.sin(sourceArrowAngle - Math.PI / 6)} \n            L ${fromX + arrowSize * Math.cos(sourceArrowAngle + Math.PI / 6)} ${fromY + arrowSize * Math.sin(sourceArrowAngle + Math.PI / 6)} 
            Z`}
        fill={color}
        initial={{ opacity: 0 }}
        animate={{ opacity: 1 }}
        exit={{ opacity: 0 }}
        transition={{ 
          duration: 0.2,
          delay: 0.5,
          ease: "easeInOut"
        }}
      />
    </motion.g>
  );
}