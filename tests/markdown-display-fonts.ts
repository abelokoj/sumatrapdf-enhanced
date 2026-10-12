import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { cmdId, runStandalone, SLOW_BUILD_FACTOR, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";

const TIMEOUT = 20_000 * SLOW_BUILD_FACTOR;
const FONT_REVISION = "3d7870d8a8cdcd891e352e08f5c4ecc3451680f0";
const FONT_SOURCE = join(import.meta.dir, "../src/fonts/markdown");
function cacheDir(profile: string): string {
  return join(profile, "markdown-fonts", FONT_REVISION);
}
function seedFontCache(profile: string): void {
  const source = JSON.parse(readFileSync(join(FONT_SOURCE, "manifest.json"), "utf8"));
  const browser = JSON.parse(readFileSync(join(FONT_SOURCE, "browser/manifest.json"), "utf8"));
  const files = source.packages.flatMap((p: { files: { file: string }[] }) => p.files.map((f) => f.file)) as string[];
  const paths = files.filter(
    (f) =>
      f.startsWith("lora/otf/") || f.startsWith("libertinus/") || f.endsWith("License.txt") || f.endsWith("OFL.txt"),
  );
  paths.push("new-computer-modern/GUST-FONT-LICENSE.txt");
  paths.push(...browser.files.map((f: { file: string }) => `browser/${f.file}`));
  for (const file of paths) {
    const dest = join(cacheDir(profile), file);
    mkdirSync(dirname(dest), { recursive: true });
    copyFileSync(join(FONT_SOURCE, file), dest);
  }
}
function cacheIsEmpty(profile: string): boolean {
  const cache = join(profile, "markdown-fonts");
  return !existsSync(cache) || readdirSync(cache).length === 0;
}
function assert(ok: unknown, message: string): asserts ok {
  if (!ok) throw new Error(`markdown-display-fonts: ${message}`);
}
async function browserPort(profile: string): Promise<number> {
  const deadline = Date.now() + TIMEOUT;
  const paths = [join(profile, "EBWebView", "DevToolsActivePort"), join(profile, "DevToolsActivePort")];
  while (Date.now() < deadline) {
    for (const path of paths) {
      if (!existsSync(path)) continue;
      const port = Number(readFileSync(path, "utf8").split(/\r?\n/)[0]);
      if (port > 0 && port <= 65535) return port;
    }
    await Bun.sleep(100);
  }
  throw new Error(`Markdown browser did not report its debug port: ${profile}`);
}

class Page {
  private id = 0;
  logs: any[] = [];
  private pending = new Map<number, { resolve: (value: any) => void; reject: (reason: Error) => void; timer: Timer }>();
  private constructor(private socket: WebSocket) {
    socket.onmessage = (message) => {
      const data = JSON.parse(String(message.data));
      if (data.method === "Log.entryAdded") {
        this.logs.push(data.params.entry);
      }
      const request = this.pending.get(data.id);
      if (!request) return;
      clearTimeout(request.timer);
      this.pending.delete(data.id);
      if (data.error) request.reject(new Error(JSON.stringify(data.error)));
      else request.resolve(data.result);
    };
  }
  static async connect(port: number): Promise<Page> {
    const deadline = Date.now() + TIMEOUT;
    let lastState = "No browser response";
    while (Date.now() < deadline) {
      try {
        const targets = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()) as any[];
        lastState = JSON.stringify(targets.map((t) => ({ type: t.type, url: t.url })));
        const target = targets.find((t) => t.url?.startsWith("https://sumatrapdf.markdown/"));
        if (target?.webSocketDebuggerUrl) {
          const socket = new WebSocket(target.webSocketDebuggerUrl);
          await new Promise<void>((resolve, reject) => {
            socket.onopen = () => resolve();
            socket.onerror = () => reject(new Error("CDP connection failed"));
          });
          return new Page(socket);
        }
      } catch (error) {
        lastState = String(error);
      }
      await Bun.sleep(100);
    }
    throw new Error(`Markdown WebView2 did not become ready: ${lastState}`);
  }
  command(method: string, params: object = {}): Promise<any> {
    const id = ++this.id;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`${method} timed out`));
      }, TIMEOUT);
      this.pending.set(id, { resolve, reject, timer });
      this.socket.send(JSON.stringify({ id, method, params }));
    });
  }
  async eval(expression: string): Promise<any> {
    const result = await this.command("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true });
    if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
  }
  async until(expression: string): Promise<any> {
    const deadline = Date.now() + TIMEOUT;
    let value;
    do {
      value = await this.eval(expression);
      if (value) return value;
      await Bun.sleep(100);
    } while (Date.now() < deadline);
    throw new Error(`Condition timed out: ${expression}; last=${JSON.stringify(value)}`);
  }
  async capture(path: string): Promise<void> {
    const result = await this.command("Page.captureScreenshot", { format: "png" });
    writeFileSync(path, Buffer.from(result.data, "base64"));
  }
  close(): void {
    this.socket.close();
  }
}

export async function testit(): Promise<void> {
  const dir = tmpPath("markdown-display-fonts");
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  const markdown = join(dir, "font-sample.md");
  writeFileSync(
    markdown,
    "# Markdown typography\n\nReadable text with **bold**, *italic*, and ***bold italic***.\n\n## Table and code\n\n| Font | Status |\n| --- | --- |\n| Downloaded fonts | Available offline |\n\n```cpp\nint answer = 42;\n```\n\n[Jump to notes](#notes)\n\n## Numbered steps\n\n1. **Copy two files.** Keep this text on the same line as its number.\n\n2. **Add includes.** Keep this text on the same line too.\n\n   ```cpp\n   int example = 1;\n   ```\n\n   - Nested step.\n\n3. **Build.** Complete the steps.\n\n## Notes\n\nLast paragraph.\n",
  );
  const prefs = join(dir, "SumatraPDFEnhanced-settings.txt");
  writeFileSync(
    prefs,
    "UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nReuseInstance = false\nUIFontSize = 16\nInterfaceScale = 100\nTheme = Sumatra Light\nDocumentColorsFollowTheme = smart\nMarkdownUI [\nUseFixedPageUI = false\nFontName = Lora\nMathFontName = Libertinus Math\n]\n",
  );
  const fontPrefs = readFileSync(prefs, "utf8");
  for (const requestedFont of ["", "Lora"]) {
    writeFileSync(
      prefs,
      fontPrefs
        .replace("FontName = Lora", `FontName = ${requestedFont}`)
        .replace("MathFontName = Libertinus Math", "MathFontName ="),
    );
    const cleanProfile = join(dir, requestedFont ? "browser-missing-font" : "browser-interface-font");
    const clean = await launchControlled(["-appdata", dir, markdown], {
      env: {
        WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: "--remote-debugging-port=0",
        WEBVIEW2_USER_DATA_FOLDER: cleanProfile,
      },
    });
    let cleanPage: Page | undefined;
    try {
      cleanPage = await Page.connect(await browserPort(cleanProfile));
      await cleanPage.until("document.querySelector('main p') && document.readyState === 'complete'");
      assert(
        await cleanPage.eval("!document.getElementById('enhanced-markdown-font')"),
        "document still contains a font dropdown",
      );
      assert(
        await cleanPage.eval(`(() => {
          const items = Array.from(document.querySelectorAll('main > ol > li'));
          return items.length === 3 && items.every(item => {
            const paragraph = item.querySelector(':scope > p');
            if (!paragraph) return false;
            const range = document.createRange();
            range.selectNodeContents(paragraph);
            const firstLine = range.getClientRects()[0];
            const fontSize = parseFloat(getComputedStyle(item).fontSize);
            return firstLine && firstLine.top - item.getBoundingClientRect().top < fontSize / 2;
          });
        })()`),
        "light-mode numbered list text starts below its number",
      );
      await cleanPage.eval("document.fonts.ready");
      assert(await cleanPage.eval("Array.from(document.fonts).length===0"), "opening Markdown loaded an optional font");
      assert(
        await cleanPage.eval("document.querySelector('main').textContent.includes('Last paragraph.')"),
        "missing fonts left the Markdown unreadable",
      );
      assert(cacheIsEmpty(dir), "opening Markdown downloaded font assets");
    } finally {
      cleanPage?.close();
      clean.client.close();
      await killAndWait(clean.proc);
    }
  }
  // Only the isolated test profile is seeded; production installs contain no font assets.
  seedFontCache(dir);
  writeFileSync(prefs, fontPrefs);
  const firstProfile = join(dir, "browser-cached-fonts");
  const app = await launchControlled(["-appdata", dir, markdown], {
    saveSettings: true,
    env: {
      WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: "--remote-debugging-port=0",
      WEBVIEW2_USER_DATA_FOLDER: firstProfile,
    },
  });
  let page: Page | undefined;
  try {
    page = await Page.connect(await browserPort(firstProfile));
    await page.command("Log.enable");
    await page.until("document.querySelector('main p') && document.readyState === 'complete'");
    assert(
      await page.eval("!document.getElementById('enhanced-markdown-font')"),
      "font controls should be in Advanced Settings",
    );
    await page.until("getComputedStyle(document.querySelector('main p')).fontFamily.includes('EnhancedMDLora')");
    const families = [
      "EnhancedMDLora",
      "EnhancedMDLibertinus",
      "EnhancedMDNewCM10",
      "EnhancedMDNewCM10Book",
      "EnhancedMDNewCMSans10",
      "EnhancedMDNewCMSans10Book",
      "EnhancedMDNewCMMono10",
      "EnhancedMDNewCMMono10Book",
      "EnhancedMDNewCMUncial10",
    ];
    for (let index = 1; index <= families.length; index++) {
      await page.eval(
        `document.getElementById('enhanced-markdown-font-css').href='https://sumatrapdf.markdown/_enhanced_fonts/style-${index}.css?v='+Date.now()`,
      );
      const family = families[index - 1];
      console.log(`Loading ${family}`);
      await page.until(`getComputedStyle(document.querySelector('main p')).fontFamily.includes('${family}')`);
      const loaded = await page.eval(
        `(async function(){await document.fonts.load('16px ${family}');await document.fonts.load('bold 16px ${family}');await document.fonts.ready;return Array.from(document.fonts).some(f=>f.family==='${family}'&&f.status==='loaded');})();`,
      );
      assert(loaded, `${family} was not loaded from the downloaded font cache`);
    }
    await page.eval(
      "document.getElementById('enhanced-markdown-font-css').href='https://sumatrapdf.markdown/_enhanced_fonts/style-2.css?v='+Date.now()",
    );
    await page.until("getComputedStyle(document.querySelector('main p')).fontFamily.includes('EnhancedMDLibertinus')");
    assert(
      await page.eval("getComputedStyle(document.body).backgroundColor==='rgb(255, 255, 255)'"),
      "light stylesheet did not apply",
    );
    await page.eval("document.fonts.load('16px EnhancedMDLibertinusMath')");
    assert(
      await page.eval("Array.from(document.fonts).some(f=>f.family==='EnhancedMDLibertinusMath'&&f.status==='loaded')"),
      "Libertinus Math resource did not load",
    );
    for (const name of ["NewCMMath-Regular", "NewCMMath-Book", "NewCMMath-Bold", "NewCMSansMath-Regular"]) {
      const loaded = await page.eval(
        `(async function(){var f=new FontFace('MathAsset','url(https://sumatrapdf.markdown/_enhanced_fonts/EnhancedWeb${name}.woff2)');await f.load();return f.status;})();`,
      );
      assert(loaded === "loaded", `${name} did not load`);
    }
    sendCommandSync(app.frame, cmdId("CmdSelectAll"));
    await page.until("window.getSelection().toString().includes('Last paragraph.')");
    await page.eval("window.getSelection().removeAllRanges()");
    await page.capture(join(dir, "light.png"));
    await page.command("Emulation.setDeviceMetricsOverride", {
      width: 320,
      height: 640,
      deviceScaleFactor: 1,
      mobile: false,
    });
    assert(
      await page.eval("document.querySelector('main').getBoundingClientRect().right<=innerWidth"),
      "Markdown content overflows a narrow window",
    );
    await page.capture(join(dir, "narrow.png"));
  } finally {
    if (page?.logs.some((entry) => entry.level === "warning")) console.log(JSON.stringify(page.logs));
    page?.close();
    await app.client.quit().catch(() => {});
    app.client.close();
    await killAndWait(app.proc);
  }
  const settings = readFileSync(prefs, "utf8");
  assert(settings.includes("FontName = Lora"), "rendering changed the saved font choice");
  writeFileSync(
    prefs,
    settings
      .replace("FontName = Lora", "FontName = Libertinus Serif")
      .replace(/^Theme\s*=.*$/m, "Theme = Sumatra Dark")
      .replace(/^InterfaceScale\s*=.*$/m, "InterfaceScale = 200"),
  );
  const secondProfile = join(dir, "browser-dark-fonts");
  const reopened = await launchControlled(["-appdata", dir, markdown], {
    env: {
      WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: "--remote-debugging-port=0",
      WEBVIEW2_USER_DATA_FOLDER: secondProfile,
    },
  });
  page = undefined;
  try {
    page = await Page.connect(await browserPort(secondProfile));
    await page.until("document.querySelector('main p') && document.readyState === 'complete'");
    await page.until("getComputedStyle(document.querySelector('main p')).fontFamily.includes('EnhancedMDLibertinus')");
    assert(
      await page.eval("getComputedStyle(document.body).backgroundColor==='rgb(30, 30, 30)'"),
      "dark stylesheet did not apply",
    );
    assert(
      await page.eval("parseFloat(getComputedStyle(document.querySelector('main p')).fontSize)===32"),
      "Markdown text does not follow interface scaling",
    );
    await page.eval("document.fonts.ready");
    await page.capture(join(dir, "dark-scaled.png"));
  } finally {
    page?.close();
    reopened.client.close();
    await killAndWait(reopened.proc);
  }
  console.log(
    "markdown-display-fonts: optional cache, no implicit downloads, saved fonts, scaling, themes, selection, and narrow layout passed",
  );
}
if (import.meta.main) await runStandalone(testit);
