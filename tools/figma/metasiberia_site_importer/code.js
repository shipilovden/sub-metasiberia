// Metasiberia Site Importer (Figma plugin)
// Imports a set of PNG screenshots into the current Figma file.
// UI sends: { type: "import", payload: { items: [{category,name,url,width,height,bytes}] } }

figma.showUI(__html__, { width: 420, height: 520 });

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function ensurePage(name) {
  for (const p of figma.root.children) {
    if (p.type === "PAGE" && p.name === name) return p;
  }
  const p = figma.createPage();
  p.name = name;
  figma.root.appendChild(p);
  return p;
}

function createText(str, fontSize, fontName, fontWeight) {
  const t = figma.createText();
  t.characters = str;
  t.fontSize = fontSize;
  t.fontName = fontName;
  t.fills = [{ type: "SOLID", color: { r: 0, g: 0, b: 0 } }];
  t.fontWeight = fontWeight;
  return t;
}

function layoutFrames(frames, maxRowWidth) {
  let x = 0;
  let y = 0;
  let rowH = 0;
  const gap = 80;
  for (const f of frames) {
    if (x > 0 && x + f.width > maxRowWidth) {
      x = 0;
      y += rowH + gap;
      rowH = 0;
    }
    f.x = x;
    f.y = y;
    x += f.width + gap;
    rowH = Math.max(rowH, f.height);
  }
}

async function importItems(items) {
  assert(Array.isArray(items) && items.length > 0, "No items to import.");

  // Fonts: use Inter if available; otherwise fallback to default.
  let fontName = { family: "Inter", style: "Bold" };
  try {
    await figma.loadFontAsync(fontName);
  } catch (e) {
    fontName = { family: "Roboto", style: "Bold" };
    await figma.loadFontAsync(fontName);
  }

  const byCategory = new Map();
  for (const it of items) {
    const cat = it.category || "Website";
    if (!byCategory.has(cat)) byCategory.set(cat, []);
    byCategory.get(cat).push(it);
  }

  for (const [cat, list] of byCategory.entries()) {
    const page = ensurePage("Site / " + cat);
    figma.currentPage = page;

    const createdFrames = [];
    for (const it of list) {
      const name = it.name || it.url || "page";
      const url = it.url || "";

      assert(it.bytes && it.bytes.length > 0, "Missing bytes for " + name);
      const w = Number(it.width || 1440);
      const h = Number(it.height || 900);

      const image = figma.createImage(new Uint8Array(it.bytes));
      const rect = figma.createRectangle();
      rect.resize(w, h);
      rect.fills = [{ type: "IMAGE", imageHash: image.hash, scaleMode: "FILL" }];

      const frame = figma.createFrame();
      frame.name = name;
      frame.clipsContent = false;
      frame.fills = [{ type: "SOLID", color: { r: 0.96, g: 0.96, b: 0.96 } }];

      // Top label area
      const label = figma.createText();
      label.fontName = fontName;
      label.fontSize = 14;
      label.characters = url;
      label.fills = [{ type: "SOLID", color: { r: 0, g: 0, b: 0 } }];
      label.x = 24;
      label.y = 16;

      rect.x = 0;
      rect.y = 48;

      frame.appendChild(label);
      frame.appendChild(rect);
      frame.resize(w, h + 64);

      createdFrames.push(frame);
    }

    // Basic tiling on canvas for convenience.
    layoutFrames(createdFrames, 3200);
  }
}

figma.ui.onmessage = async (msg) => {
  try {
    if (!msg || !msg.type) return;
    if (msg.type === "import") {
      const items = msg.payload && msg.payload.items;
      await importItems(items);
      figma.notify("Imported " + items.length + " page(s).");
    } else if (msg.type === "close") {
      figma.closePlugin();
    }
  } catch (e) {
    figma.notify("Import failed: " + (e && e.message ? e.message : String(e)), { error: true });
  }
};

