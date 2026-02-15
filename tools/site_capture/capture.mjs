import fs from "node:fs";
import path from "node:path";
import { chromium } from "playwright";

function parseArgs(argv) {
  const args = {};
  for (let i = 2; i < argv.length; i++) {
    const a = argv[i];
    if (!a.startsWith("--")) continue;
    const k = a.slice(2);
    const v = argv[i + 1] && !argv[i + 1].startsWith("--") ? argv[++i] : "true";
    args[k] = v;
  }
  return args;
}

function ensureDir(p) {
  fs.mkdirSync(p, { recursive: true });
}

function pngSize(buf) {
  // PNG signature (8) + IHDR length/type (8) + width/height (8)
  if (buf.length < 24) return { width: 0, height: 0 };
  // width/height are big-endian uint32 at offset 16
  const width = buf.readUInt32BE(16);
  const height = buf.readUInt32BE(20);
  return { width, height };
}

function nowStamp() {
  const d = new Date();
  const pad = (n) => String(n).padStart(2, "0");
  return (
    d.getFullYear() +
    pad(d.getMonth() + 1) +
    pad(d.getDate()) +
    "_" +
    pad(d.getHours()) +
    pad(d.getMinutes()) +
    pad(d.getSeconds())
  );
}

const args = parseArgs(process.argv);
const baseUrl = (args.base || process.env.METASIBERIA_BASE_URL || "https://vr.metasiberia.com").replace(/\/+$/, "");
const outDir = args.out || process.env.METASIBERIA_CAPTURE_OUT || path.join(process.cwd(), `out_${nowStamp()}`);
const fullPage = String(args.fullPage || "true") === "true";
const viewportW = Number(args.width || 1440);
const viewportH = Number(args.height || 900);
const cookieHeader = args.cookie || process.env.METASIBERIA_COOKIE || "";
const browserPathArg = args.browserPath || process.env.METASIBERIA_BROWSER_PATH || "";
const browserChannelArg = args.channel || process.env.METASIBERIA_BROWSER_CHANNEL || "";

const adminUser = process.env.METASIBERIA_ADMIN_USER || "";
const adminPass = process.env.METASIBERIA_ADMIN_PASS || "";
const storageStatePath = args.storageState || process.env.METASIBERIA_STORAGE_STATE || "";

const pages = [
  { category: "Public", name: "Root", path: "/" },
  { category: "Public", name: "Map", path: "/map" },
  { category: "Public", name: "News", path: "/news" },
  { category: "Public", name: "Photos", path: "/photos" },
  { category: "Auth", name: "Signup", path: "/signup" },
  { category: "Auth", name: "Login", path: "/login" },
  { category: "User", name: "Account", path: "/account", requiresAuth: true },
  { category: "Admin", name: "Admin", path: "/admin", requiresAuth: true },
  { category: "Admin", name: "Admin Users", path: "/admin_users", requiresAuth: true },
  { category: "Admin", name: "Admin Parcels", path: "/admin_parcels", requiresAuth: true },
  { category: "Admin", name: "Admin News", path: "/admin_news_posts", requiresAuth: true }
];

console.log("Base:", baseUrl);
console.log("Out:", outDir);
ensureDir(outDir);

function firstExistingPath(candidates) {
  for (const p of candidates) {
    if (p && fs.existsSync(p)) return p;
  }
  return "";
}

const defaultBrowserPath = firstExistingPath([
  "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
  "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
  "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
  "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe"
]);

const launchOpts = { headless: true };
if (browserPathArg) launchOpts.executablePath = browserPathArg;
else if (browserChannelArg) launchOpts.channel = browserChannelArg;
else if (defaultBrowserPath) launchOpts.executablePath = defaultBrowserPath;

console.log("Browser:", launchOpts.executablePath || launchOpts.channel || "playwright-managed");

const browser = await chromium.launch(launchOpts);
const contextOpts = {
  viewport: { width: viewportW, height: viewportH }
};

if (storageStatePath) {
  contextOpts.storageState = storageStatePath;
  console.log("Using storageState:", storageStatePath);
}
if (cookieHeader) {
  contextOpts.extraHTTPHeaders = { Cookie: cookieHeader };
  console.log("Using explicit Cookie header.");
}

const context = await browser.newContext(contextOpts);

async function ensureLoggedIn() {
  // If already logged in (storageState), this should no-op.
  if (!adminUser || !adminPass) return;

  const p = await context.newPage();
  await p.goto(`${baseUrl}/login`, { waitUntil: "domcontentloaded" });

  // Substrata-style login form.
  await p.fill("#username", adminUser);
  await p.fill("#current-password", adminPass);
  await Promise.all([
    p.waitForNavigation({ waitUntil: "domcontentloaded" }),
    p.click('input[type="submit"][value="Log in"]')
  ]);
  await p.close();
}

// If we have credentials, log in once so /admin pages work.
await ensureLoggedIn();

const items = [];
for (const pg of pages) {
  if (pg.requiresAuth && !(storageStatePath || (adminUser && adminPass))) {
    console.log("SKIP (auth required):", pg.path);
    continue;
  }

  const url = baseUrl + pg.path;
  const fileSafe = pg.path === "/" ? "root" : pg.path.replace(/^\//, "").replace(/[\/?&#=]/g, "_");
  const file = `${fileSafe}.png`;
  const outPath = path.join(outDir, file);

  console.log("GET", url);
  const p = await context.newPage();
  await p.goto(url, { waitUntil: "networkidle" });
  await p.waitForTimeout(250);

  await p.screenshot({ path: outPath, fullPage });
  await p.close();

  const buf = fs.readFileSync(outPath);
  const { width, height } = pngSize(buf);

  items.push({
    category: pg.category,
    name: pg.name,
    path: pg.path,
    url,
    file,
    width,
    height
  });
}

if (adminUser && adminPass && !storageStatePath) {
  const st = path.join(outDir, "storageState.json");
  await context.storageState({ path: st });
  console.log("Saved storageState:", st);
}

await context.close();
await browser.close();

const manifest = {
  baseUrl,
  viewport: { width: viewportW, height: viewportH },
  fullPage,
  createdUtc: new Date().toISOString(),
  items
};

const manifestPath = path.join(outDir, "manifest.json");
fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2), "utf8");

console.log("Wrote:", manifestPath);
