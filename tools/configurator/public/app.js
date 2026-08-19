"use strict";

// --- Constants --------------------------------------------------------

const NODE_WIDTH = 210;
const HEADER_H = 28;
const ROW_H = 20;
const PAD_TOP = 10;
const PAD_BOTTOM = 10;
const PIN_R = 5;

// --- State --------------------------------------------------------------

const state = {
  catalog: [], // [{name, dir, provides, requires, config}]
  catalogByName: new Map(),
  nodes: new Map(), // name -> {name, catalog, enabled, config: {key:val}, x, y}
  edges: [], // [{consumer, type, provider}]
  selected: null,
  placeCount: 0,
};

const view = { x: 0, y: 0, scale: 1 }; // pan/zoom applied to the #viewport group

let moving = null; // {node, dx, dy} while dragging a node
let panning = null; // {startClientX, startClientY, startViewX, startViewY} while panning

// --- DOM refs -----------------------------------------------------------

const $ = (sel) => document.querySelector(sel);
const svg = $("#canvas");
const moduleListEl = $("#module-list");
const searchEl = $("#search");
const statusEl = $("#status");
const canvasHint = $("#canvas-hint");
const inspectorEmpty = $("#inspector-empty");
const inspectorBody = $("#inspector-body");

const SVG_NS = "http://www.w3.org/2000/svg";
function svgEl(tag, attrs) {
  const el = document.createElementNS(SVG_NS, tag);
  if (attrs) for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, v);
  return el;
}

function setStatus(msg, kind) {
  statusEl.textContent = msg;
  statusEl.className = "status" + (kind ? " " + kind : "");
}

// Screen (client) coordinates -> world coordinates, i.e. the inverse of
// the #viewport group's `translate(view.x, view.y) scale(view.scale)`
// transform. Node x/y and every other canvas-space coordinate in this file
// is in world space; only pointer events arrive in screen space.
function screenToWorld(clientX, clientY) {
  const rect = svg.getBoundingClientRect();
  return {
    x: (clientX - rect.left - view.x) / view.scale,
    y: (clientY - rect.top - view.y) / view.scale,
  };
}

// --- Data loading ---------------------------------------------------------

async function loadCatalog() {
  const res = await fetch("/api/modules");
  state.catalog = await res.json();
  state.catalogByName = new Map(state.catalog.map((m) => [m.name, m]));
  renderSidebar();
}

async function getLayout() {
  try {
    const res = await fetch("/api/layout");
    return await res.json();
  } catch {
    return {};
  }
}

async function saveLayout() {
  const layout = {};
  for (const n of state.nodes.values()) layout[n.name] = { x: n.x, y: n.y };
  try {
    await fetch("/api/layout", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(layout) });
  } catch {
    // Layout persistence is best-effort UX sugar; ignore failures.
  }
}

// Lays modules out left-to-right by dependency depth: a module with no
// wired dependencies sits in column 0, and anything that depends on it
// sits at least one column to its right -- so the columns read the same
// order the bootloader actually initializes modules in (dependencies
// before dependents). Modules sharing a column are stacked top-to-bottom
// in kernel.ini's original order.
function computeTreeLayout(modules, edges) {
  const depthCache = new Map();

  function depthOf(name, guard) {
    if (depthCache.has(name)) return depthCache.get(name);
    if (guard.has(name)) return 0; // defensive cycle guard; a real kernel.ini is a DAG
    guard.add(name);
    let depth = 0;
    for (const edge of edges) {
      if (edge.consumer !== name) continue;
      depth = Math.max(depth, 1 + depthOf(edge.provider, guard));
    }
    guard.delete(name);
    depthCache.set(name, depth);
    return depth;
  }

  const COL_W = 260;
  const ROW_H_ = 130;
  const nextRowForDepth = new Map();
  const positions = {};

  for (const mod of modules) {
    const depth = depthOf(mod.name, new Set());
    const row = nextRowForDepth.get(depth) || 0;
    nextRowForDepth.set(depth, row + 1);
    positions[mod.name] = { x: 60 + depth * COL_W, y: 60 + row * ROW_H_ };
  }

  return positions;
}

function nextAutoPosition() {
  const i = state.placeCount++;
  // Cascade around the current viewport's center (in world space) rather
  // than a fixed world coordinate -- otherwise, once panned/zoomed away
  // from the origin, newly added nodes land off-screen.
  const rect = svg.getBoundingClientRect();
  const center = screenToWorld(rect.left + rect.width / 2, rect.top + rect.height / 2);
  return {
    x: center.x - NODE_WIDTH / 2 + (i % 4) * 250,
    y: center.y - 100 + Math.floor(i / 4) * 190,
  };
}

// --- Sidebar --------------------------------------------------------------

function renderSidebar() {
  const filter = searchEl.value.trim().toLowerCase();
  moduleListEl.innerHTML = "";
  for (const mod of state.catalog) {
    if (filter && !mod.name.toLowerCase().includes(filter)) continue;
    const li = document.createElement("li");
    li.className = "module-item";
    li.draggable = true;
    li.dataset.name = mod.name;

    const nameEl = document.createElement("span");
    nameEl.className = "name";
    nameEl.textContent = mod.name;

    const metaBits = [];
    if (mod.provides.length) metaBits.push("provides " + mod.provides.map((p) => p.type).join(", "));
    if (mod.requires.length) metaBits.push("needs " + mod.requires.map((r) => r.type).join(", "));
    if (!metaBits.length) metaBits.push("no interfaces");

    const metaEl = document.createElement("span");
    metaEl.className = "meta";
    metaEl.textContent = metaBits.join(" • ");

    li.appendChild(nameEl);
    li.appendChild(metaEl);

    li.addEventListener("click", () => {
      const pos = nextAutoPosition();
      addNode(mod, pos.x, pos.y);
    });
    li.addEventListener("dragstart", (e) => {
      e.dataTransfer.setData("text/plain", mod.name);
      e.dataTransfer.effectAllowed = "copy";
    });

    moduleListEl.appendChild(li);
  }
}

searchEl.addEventListener("input", renderSidebar);

const canvasWrap = $("#canvas-wrap");
canvasWrap.addEventListener("dragover", (e) => {
  e.preventDefault();
  e.dataTransfer.dropEffect = "copy";
});
canvasWrap.addEventListener("drop", (e) => {
  e.preventDefault();
  const name = e.dataTransfer.getData("text/plain");
  const mod = state.catalogByName.get(name);
  if (!mod) return;
  const pos = screenToWorld(e.clientX, e.clientY);
  addNode(mod, pos.x - NODE_WIDTH / 2, pos.y - HEADER_H / 2);
});

// --- Node model -----------------------------------------------------------

function defaultConfigFor(catalogMod) {
  const cfg = {};
  for (const field of catalogMod.config) cfg[field.key] = field.default ?? "";
  return cfg;
}

function nodeHeight(node) {
  const rows = Math.max(node.catalog.requires.length, node.catalog.provides.length, 1);
  return HEADER_H + PAD_TOP + rows * ROW_H + PAD_BOTTOM;
}

function addNode(catalogMod, x, y, opts) {
  opts = opts || {};
  if (state.nodes.has(catalogMod.name)) {
    state.selected = catalogMod.name;
    render();
    return state.nodes.get(catalogMod.name);
  }
  const node = {
    name: catalogMod.name,
    catalog: catalogMod,
    enabled: opts.enabled !== undefined ? opts.enabled : true,
    config: opts.config || defaultConfigFor(catalogMod),
    x: Math.max(10, x),
    y: Math.max(10, y),
  };
  state.nodes.set(node.name, node);
  state.selected = node.name;
  render();
  saveLayout();
  return node;
}

function removeNode(name) {
  state.nodes.delete(name);
  state.edges = state.edges.filter((e) => e.consumer !== name && e.provider !== name);
  if (state.selected === name) state.selected = null;
  render();
  saveLayout();
}

// --- Geometry ---------------------------------------------------------

function pinPos(node, kind, key) {
  const list = kind === "in" ? node.catalog.requires : node.catalog.provides;
  const idx = list.findIndex((e) => (kind === "in" ? e.key : e.instance) === key);
  const y = node.y + HEADER_H + PAD_TOP + Math.max(idx, 0) * ROW_H + ROW_H / 2;
  const x = kind === "in" ? node.x : node.x + NODE_WIDTH;
  return { x, y };
}

function typesCompatible(a, b) {
  return a && b && a.toLowerCase() === b.toLowerCase();
}

function edgeForInput(consumerName, typeKey) {
  return state.edges.find((e) => e.consumer === consumerName && e.type === typeKey);
}

// The dropdown only records a provider *module*, not a specific exported
// instance/pin (kernel.ini's [module.dependencies] wiring works the same
// way -- one provider module per required type, never a specific
// instance). So the line's provider-side endpoint is picked by matching
// the required type against that module's provides list; if it doesn't
// export anything of that type at all (a stale/mismatched wiring), anchor
// to its top-center instead of not drawing anything.
function providerOutputPos(providerName, requiredType) {
  const node = state.nodes.get(providerName);
  if (!node) return null;
  const match = node.catalog.provides.find((p) => typesCompatible(p.type, requiredType)) || node.catalog.provides[0];
  if (match) return pinPos(node, "out", match.instance);
  return { x: node.x + NODE_WIDTH / 2, y: node.y };
}

function bezier(p1, p2) {
  // Control points extend outward from each endpoint in its own natural
  // direction (rightward from an output, leftward from an input), so the
  // curve loops cleanly even when the target node sits to the left of, or
  // above, the source one. `dx` is capped rather than scaled unboundedly
  // with distance -- uncapped, a connection between far-apart/"backward"
  // nodes produces a huge control-point offset that sweeps the curve
  // across (and visually through) unrelated nodes on the way.
  const dx = Math.min(140, Math.max(40, Math.abs(p2.x - p1.x) / 2));
  return `M ${p1.x} ${p1.y} C ${p1.x + dx} ${p1.y}, ${p2.x - dx} ${p2.y}, ${p2.x} ${p2.y}`;
}

// --- Rendering --------------------------------------------------------
//
// Dependencies are wired via a dropdown in the inspector panel (see
// renderDependencyField), not by dragging on the canvas -- these lines are
// a read-only reflection of state.edges, redrawn on every render(). The
// only interaction they support is a click to disconnect (equivalent to
// picking "-- none --" for that requirement in the inspector).

function render() {
  svg.innerHTML = "";
  canvasHint.classList.toggle("hidden", state.nodes.size > 0);

  const viewport = svgEl("g", {
    class: "viewport",
    transform: `translate(${view.x}, ${view.y}) scale(${view.scale})`,
  });
  svg.appendChild(viewport);

  const nodeLayer = svgEl("g", { class: "nodes" });
  const edgeLayer = svgEl("g", { class: "edges" });
  // Nodes first, edges second: SVG paints in document order, so this puts
  // edges on top of the (opaque) node boxes. Drawn the other way around, a
  // line between two nearby nodes is almost entirely painted over, leaving
  // only whatever sliver falls in the gap between them visible.
  viewport.appendChild(nodeLayer);
  viewport.appendChild(edgeLayer);

  for (const node of state.nodes.values()) drawNode(nodeLayer, node);

  for (const edge of state.edges) {
    const consumer = state.nodes.get(edge.consumer);
    if (!consumer) continue;
    const p1 = pinPos(consumer, "in", edge.type);
    const p2 = providerOutputPos(edge.provider, edge.type) || p1;
    const d = bezier(p2, p1);

    const g = svgEl("g", { class: "edge" });
    const hit = svgEl("path", { class: "edge-hit", d });
    const visible = svgEl("path", { class: "edge-path", d });
    hit.addEventListener("click", (e) => {
      e.stopPropagation();
      state.edges = state.edges.filter((x) => x !== edge);
      render();
    });
    g.appendChild(hit);
    g.appendChild(visible);
    edgeLayer.appendChild(g);
  }

  renderInspector();
}

function drawNode(layer, node) {
  const h = nodeHeight(node);
  const g = svgEl("g", { class: "node", "data-node": node.name });
  g.setAttribute("transform", `translate(${node.x}, ${node.y})`);

  const box = svgEl("rect", {
    class: "node-box" + (state.selected === node.name ? " selected" : "") + (node.enabled ? "" : " disabled"),
    x: 0, y: 0, width: NODE_WIDTH, height: h, rx: 8,
  });
  const header = svgEl("path", {
    class: "node-header",
    d: `M0,8 a8,8 0 0 1 8,-8 h${NODE_WIDTH - 16} a8,8 0 0 1 8,8 v${HEADER_H - 8} h-${NODE_WIDTH} z`,
  });
  const title = svgEl("text", { class: "node-title", x: 10, y: HEADER_H / 2 + 1 });
  title.textContent = node.name;

  g.appendChild(box);
  g.appendChild(header);
  g.appendChild(title);

  node.catalog.requires.forEach((req, i) => {
    const y = HEADER_H + PAD_TOP + i * ROW_H + ROW_H / 2;
    const wired = !!edgeForInput(node.name, req.key);
    const dot = svgEl("circle", { class: "pin-dot require" + (wired ? " wired" : ""), cx: 0, cy: y, r: PIN_R });
    const label = svgEl("text", { class: "pin-label", x: 10, y });
    label.textContent = req.type;
    g.appendChild(dot);
    g.appendChild(label);
  });

  node.catalog.provides.forEach((prov, i) => {
    const y = HEADER_H + PAD_TOP + i * ROW_H + ROW_H / 2;
    const dot = svgEl("circle", { class: "pin-dot provide", cx: NODE_WIDTH, cy: y, r: PIN_R });
    const label = svgEl("text", { class: "pin-label", x: NODE_WIDTH - 10, y, "text-anchor": "end" });
    label.textContent = prov.type;
    g.appendChild(dot);
    g.appendChild(label);
  });

  layer.appendChild(g);
}

// --- Inspector --------------------------------------------------------

function renderInspector() {
  const node = state.selected ? state.nodes.get(state.selected) : null;
  inspectorEmpty.hidden = !!node;
  inspectorBody.hidden = !node;
  inspectorBody.innerHTML = "";
  if (!node) return;

  const title = document.createElement("div");
  title.className = "inspector-title";
  title.textContent = node.name;
  inspectorBody.appendChild(title);

  const sub = document.createElement("div");
  sub.className = "inspector-sub";
  sub.textContent = node.catalog.dir ? "modules/" + node.catalog.dir : "";
  inspectorBody.appendChild(sub);

  // Enable toggle
  const enableField = document.createElement("div");
  enableField.className = "field checkbox-row";
  const enableInput = document.createElement("input");
  enableInput.type = "checkbox";
  enableInput.id = "f-enable";
  enableInput.checked = node.enabled;
  enableInput.addEventListener("change", () => {
    node.enabled = enableInput.checked;
    render();
  });
  const enableLabel = document.createElement("label");
  enableLabel.htmlFor = "f-enable";
  enableLabel.textContent = "Enabled";
  enableField.appendChild(enableInput);
  enableField.appendChild(enableLabel);
  inspectorBody.appendChild(enableField);

  // Required interfaces -- each is wired via a dropdown listing every
  // compatible provider currently on the canvas.
  if (node.catalog.requires.length) {
    const h = document.createElement("h3");
    h.textContent = "Dependencies";
    inspectorBody.appendChild(h);
    for (const req of node.catalog.requires) renderDependencyField(inspectorBody, node, req);
  }

  // Config fields
  if (node.catalog.config.length) {
    const h = document.createElement("h3");
    h.textContent = "Configuration";
    inspectorBody.appendChild(h);
    for (const field of node.catalog.config) renderConfigField(inspectorBody, node, field);
  }

  const removeBtn = document.createElement("button");
  removeBtn.className = "remove-node-btn";
  removeBtn.textContent = "Remove node";
  removeBtn.addEventListener("click", () => removeNode(node.name));
  inspectorBody.appendChild(removeBtn);
}

function renderConfigField(container, node, field) {
  const wrap = document.createElement("div");
  wrap.className = "field";
  const label = document.createElement("label");
  label.textContent = field.label || field.key;
  wrap.appendChild(label);

  const current = node.config[field.key] !== undefined ? node.config[field.key] : field.default || "";
  let input;

  if (field.type === "bool") {
    wrap.classList.add("checkbox-row");
    input = document.createElement("input");
    input.type = "checkbox";
    input.checked = String(current).toLowerCase() === "true" || current === "1";
    input.addEventListener("change", () => {
      node.config[field.key] = input.checked ? "true" : "false";
    });
    wrap.innerHTML = "";
    wrap.appendChild(input);
    wrap.appendChild(label);
  } else if (field.type === "enum") {
    input = document.createElement("select");
    const options = (field.options || "").split(",").map((s) => s.trim()).filter(Boolean);
    for (const opt of options) {
      const o = document.createElement("option");
      o.value = opt;
      o.textContent = opt;
      if (opt === current) o.selected = true;
      input.appendChild(o);
    }
    input.addEventListener("change", () => {
      node.config[field.key] = input.value;
    });
    wrap.appendChild(input);
  } else if (field.type === "int") {
    input = document.createElement("input");
    input.type = "number";
    input.value = current;
    if (field.min !== undefined) input.min = field.min;
    if (field.max !== undefined) input.max = field.max;
    input.addEventListener("input", () => {
      node.config[field.key] = input.value;
    });
    wrap.appendChild(input);
  } else {
    input = document.createElement("input");
    input.type = "text";
    input.value = current;
    if (field.max_len !== undefined) input.maxLength = Number(field.max_len);
    input.addEventListener("input", () => {
      node.config[field.key] = input.value;
    });
    wrap.appendChild(input);
  }

  if (field.description) {
    const desc = document.createElement("div");
    desc.className = "desc";
    desc.textContent = field.description;
    wrap.appendChild(desc);
  }

  container.appendChild(wrap);
}

function renderDependencyField(container, node, req) {
  const wrap = document.createElement("div");
  wrap.className = "field";
  const label = document.createElement("label");
  label.textContent = `${req.type} (${req.key})`;
  wrap.appendChild(label);

  const select = document.createElement("select");
  const noneOpt = document.createElement("option");
  noneOpt.value = "";
  noneOpt.textContent = "— none —";
  select.appendChild(noneOpt);

  const candidates = [...state.nodes.values()].filter(
    (n) => n.name !== node.name && n.catalog.provides.some((p) => typesCompatible(p.type, req.type))
  );
  for (const cand of candidates) {
    const opt = document.createElement("option");
    opt.value = cand.name;
    opt.textContent = cand.name;
    select.appendChild(opt);
  }

  const current = edgeForInput(node.name, req.key);
  if (current) {
    if (!candidates.some((c) => c.name === current.provider)) {
      // Wired to a module that isn't on the canvas right now (e.g. loaded
      // from kernel.ini but not added back in yet) -- keep it visible
      // instead of silently dropping the wiring.
      const opt = document.createElement("option");
      opt.value = current.provider;
      opt.textContent = `${current.provider} (not on canvas)`;
      select.appendChild(opt);
    }
    select.value = current.provider;
  }

  select.addEventListener("change", () => {
    state.edges = state.edges.filter((e) => !(e.consumer === node.name && e.type === req.key));
    if (select.value) state.edges.push({ consumer: node.name, type: req.key, provider: select.value });
    render();
  });

  wrap.appendChild(select);

  if (!candidates.length && !current) {
    const desc = document.createElement("div");
    desc.className = "desc";
    desc.textContent = `No module providing ${req.type} is on the canvas yet.`;
    wrap.appendChild(desc);
  }

  container.appendChild(wrap);
}

// --- Canvas interaction: drag-to-move, drag-to-pan, wheel-to-zoom -------

function applyView() {
  const g = svg.querySelector(".viewport");
  if (g) g.setAttribute("transform", `translate(${view.x}, ${view.y}) scale(${view.scale})`);
}

svg.addEventListener("pointerdown", (e) => {
  const nodeEl = e.target.closest(".node");

  if (nodeEl) {
    e.preventDefault();
    const name = nodeEl.dataset.node;
    const node = state.nodes.get(name);
    state.selected = name;
    const pos = screenToWorld(e.clientX, e.clientY);
    moving = { node, dx: pos.x - node.x, dy: pos.y - node.y };
    render();
    return;
  }

  // Empty background: pan instead of moving a node. Deselect immediately
  // (clicking empty space to deselect is expected even if this turns into
  // a pan drag) but don't re-render mid-pan -- only the transform needs to
  // change, not the whole node/edge tree.
  e.preventDefault();
  state.selected = null;
  panning = { startClientX: e.clientX, startClientY: e.clientY, startViewX: view.x, startViewY: view.y };
  canvasWrap.classList.add("panning");
  render();
});

window.addEventListener("pointermove", (e) => {
  if (moving) {
    const pos = screenToWorld(e.clientX, e.clientY);
    moving.node.x = Math.max(0, pos.x - moving.dx);
    moving.node.y = Math.max(0, pos.y - moving.dy);
    render();
    return;
  }
  if (panning) {
    view.x = panning.startViewX + (e.clientX - panning.startClientX);
    view.y = panning.startViewY + (e.clientY - panning.startClientY);
    applyView();
  }
});

window.addEventListener("pointerup", () => {
  if (moving) {
    moving = null;
    saveLayout();
  }
  if (panning) {
    panning = null;
    canvasWrap.classList.remove("panning");
  }
});

svg.addEventListener(
  "wheel",
  (e) => {
    e.preventDefault();
    const rect = svg.getBoundingClientRect();
    const mx = e.clientX - rect.left;
    const my = e.clientY - rect.top;
    // Keep the point currently under the cursor fixed in place while the
    // scale changes, rather than always zooming toward the top-left.
    const worldX = (mx - view.x) / view.scale;
    const worldY = (my - view.y) / view.scale;
    const factor = e.deltaY < 0 ? 1.1 : 1 / 1.1;
    view.scale = Math.min(2.5, Math.max(0.2, view.scale * factor));
    view.x = mx - worldX * view.scale;
    view.y = my - worldY * view.scale;
    applyView();
  },
  { passive: false }
);

window.addEventListener("keydown", (e) => {
  if (e.key !== "Delete" && e.key !== "Backspace") return;
  // Don't hijack Backspace/Delete while the user is typing in an inspector
  // field (editing a config value, etc).
  const tag = document.activeElement && document.activeElement.tagName;
  if (tag === "INPUT" || tag === "SELECT" || tag === "TEXTAREA") return;
  if (!state.selected) return;
  e.preventDefault();
  removeNode(state.selected);
});

// --- Toolbar: clear / load / generate -----------------------------------

$("#clear-btn").addEventListener("click", () => {
  if (state.nodes.size === 0) return;
  if (!confirm("Clear all nodes from the canvas? This only affects the canvas -- kernel.ini on disk is untouched until you hit Generate.")) return;
  state.nodes.clear();
  state.edges = [];
  state.selected = null;
  state.placeCount = 0;
  view.x = 0;
  view.y = 0;
  view.scale = 1;
  render();
  setStatus("Canvas cleared", "ok");
});

$("#load-btn").addEventListener("click", async () => {
  try {
    const [graph, layout] = await Promise.all([fetch("/api/kernel-ini").then((r) => r.json()), getLayout()]);
    state.nodes.clear();
    state.edges = [];
    state.selected = null;
    state.placeCount = 0;

    const treePositions = computeTreeLayout(graph.modules, graph.edges);

    for (const mod of graph.modules) {
      const catalogMod = state.catalogByName.get(mod.name) || {
        name: mod.name, dir: "", provides: [], requires: [], config: [],
      };
      // A manually-dragged-and-saved position wins if there is one;
      // otherwise lay it out by dependency depth rather than an arbitrary
      // grid, so the import reads as the actual dependency tree.
      const pos = layout[mod.name] || treePositions[mod.name];
      const node = {
        name: mod.name,
        catalog: catalogMod,
        enabled: mod.enabled,
        config: Object.assign(defaultConfigFor(catalogMod), mod.config),
        x: pos.x,
        y: pos.y,
      };
      state.nodes.set(node.name, node);
    }
    state.edges = graph.edges.filter((e) => state.nodes.has(e.consumer));

    render();
    setStatus(`Loaded ${state.nodes.size} module(s) from kernel.ini`, "ok");
  } catch (err) {
    setStatus("Failed to load kernel.ini: " + err.message, "err");
  }
});

$("#generate-btn").addEventListener("click", async () => {
  const graph = {
    modules: [...state.nodes.values()].map((n) => ({ name: n.name, enabled: n.enabled, config: n.config })),
    edges: state.edges.map((e) => ({ consumer: e.consumer, type: e.type, provider: e.provider })),
  };
  try {
    const res = await fetch("/api/kernel-ini", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(graph),
    });
    const body = await res.json();
    if (!res.ok) throw new Error(body.error || res.statusText);
    setStatus(`Wrote ${body.path}`, "ok");
    saveLayout();
  } catch (err) {
    setStatus("Failed to generate kernel.ini: " + err.message, "err");
  }
});

// --- Boot -----------------------------------------------------------

loadCatalog().then(() => setStatus("Ready"));
render();
