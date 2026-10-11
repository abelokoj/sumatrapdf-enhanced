import { mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { createServer } from "node:net";
import { cmdId, runStandalone, SLOW_BUILD_FACTOR, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";

const TIMEOUT = 20_000 * SLOW_BUILD_FACTOR;
function assert(ok: unknown, message: string): asserts ok {
  if (!ok) throw new Error(`markdown-display-fonts: ${message}`);
}
async function port(): Promise<number> {
  const server = createServer();
  await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));
  const result = (server.address() as { port: number }).port;
  await new Promise<void>((resolve) => server.close(() => resolve()));
  return result;
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
    while (Date.now() < deadline) {
      try {
        const targets = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()) as any[];
        const target = targets.find((t) => t.url?.startsWith("https://sumatrapdf.markdown/"));
        if (target?.webSocketDebuggerUrl) {
          const socket = new WebSocket(target.webSocketDebuggerUrl);
          await new Promise<void>((resolve, reject) => {
            socket.onopen = () => resolve();
            socket.onerror = () => reject(new Error("CDP connection failed"));
          });
          return new Page(socket);
        }
      } catch {}
      await Bun.sleep(100);
    }
    throw new Error("Markdown WebView2 did not become ready");
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
    "# Markdown typography\n\nReadable text with **bold**, *italic*, and ***bold italic***.\n\n## Table and code\n\n| Font | Status |\n| --- | --- |\n| Bundled fonts | Available offline |\n\n```cpp\nint answer = 42;\n```\n\n[Jump to notes](#notes)\n\n## Notes\n\nLast paragraph.\n",
  );
  const prefs = join(dir, "SumatraPDFEnhanced-settings.txt");
  writeFileSync(
    prefs,
    "UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nReuseInstance = false\nUIFontSize = 16\nInterfaceScale = 100\nTheme = Sumatra Light\nDocumentColorsFollowTheme = smart\nMarkdownUI [\nUseFixedPageUI = false\nFontName = Lora\nMathFontName = Libertinus Math\n]\n",
  );
  const firstPort = await port();
  const app = await launchControlled(["-appdata", dir, markdown], {
    saveSettings: true,
    env: { WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: `--remote-debugging-port=${firstPort}` },
  });
  let page: Page | undefined;
  try {
    page = await Page.connect(firstPort);
    await page.command("Log.enable");
    await page.until("document.getElementById('enhanced-markdown-font') && document.readyState === 'complete'");
    const names = await page.eval(
      "Array.from(document.getElementById('enhanced-markdown-font').options).map(o=>o.text)",
    );
    assert(
      names.length === 10 &&
        names.includes("Lora") &&
        names.includes("Libertinus Serif") &&
        names.includes("New Computer Modern"),
      `missing font choices: ${names}`,
    );
    assert(
      await page.eval("document.getElementById('enhanced-markdown-font').value==='1'"),
      "Settings default was not used",
    );
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
        `(function(){var s=document.getElementById('enhanced-markdown-font');s.value='${index}';s.dispatchEvent(new Event('change'));})();`,
      );
      const family = families[index - 1];
      console.log(`Loading ${family}`);
      await page.until(`getComputedStyle(document.querySelector('main p')).fontFamily.includes('${family}')`);
      const loaded = await page.eval(
        `(async function(){await document.fonts.load('16px ${family}');await document.fonts.load('bold 16px ${family}');await document.fonts.ready;return Array.from(document.fonts).some(f=>f.family==='${family}'&&f.status==='loaded');})();`,
      );
      assert(loaded, `${family} was not loaded from the embedded font resource`);
    }
    await page.eval(
      "(function(){var s=document.getElementById('enhanced-markdown-font');s.value='2';s.dispatchEvent(new Event('change'));})();",
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
    assert(
      await page.eval("!window.getSelection().toString().includes('Follow interface font')"),
      "Select all copied the font selector",
    );
    await page.eval("window.getSelection().removeAllRanges()");
    await page.capture(join(dir, "light.png"));
    await page.command("Emulation.setDeviceMetricsOverride", {
      width: 320,
      height: 640,
      deviceScaleFactor: 1,
      mobile: false,
    });
    assert(
      await page.eval("document.getElementById('enhanced-markdown-font').getBoundingClientRect().right<=innerWidth"),
      "font dropdown overflows a narrow window",
    );
    await page.command("Emulation.setFocusEmulationEnabled", { enabled: true });
    await page.command("Input.dispatchKeyEvent", {
      type: "keyDown",
      key: "Tab",
      code: "Tab",
      windowsVirtualKeyCode: 9,
    });
    await page.command("Input.dispatchKeyEvent", { type: "keyUp", key: "Tab", code: "Tab", windowsVirtualKeyCode: 9 });
    await page.eval("document.getElementById('enhanced-markdown-font').focus()");
    assert(
      await page.eval("getComputedStyle(document.getElementById('enhanced-markdown-font')).outlineStyle!=='none'"),
      "font dropdown has no keyboard focus indicator",
    );
    await page.capture(join(dir, "narrow.png"));
    const deadline = Date.now() + TIMEOUT;
    while (!readFileSync(prefs, "utf8").includes("FontName = Libertinus Serif")) {
      assert(Date.now() < deadline, "page font selection was not persisted");
      await Bun.sleep(100);
    }
  } finally {
    if (page?.logs.some((entry) => entry.level === "warning")) console.log(JSON.stringify(page.logs));
    page?.close();
    await app.client.quit().catch(() => {});
    app.client.close();
    await killAndWait(app.proc);
  }
  const settings = readFileSync(prefs, "utf8");
  assert(settings.includes("FontName = Libertinus Serif"), "saved font choice was lost on exit");
  writeFileSync(
    prefs,
    settings
      .replace(/^Theme\s*=.*$/m, "Theme = Sumatra Dark")
      .replace(/^InterfaceScale\s*=.*$/m, "InterfaceScale = 200"),
  );
  const secondPort = await port();
  const reopened = await launchControlled(["-appdata", dir, markdown], {
    env: { WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: `--remote-debugging-port=${secondPort}` },
  });
  page = undefined;
  try {
    page = await Page.connect(secondPort);
    await page.until("document.getElementById('enhanced-markdown-font') && document.readyState === 'complete'");
    assert(
      await page.eval("document.getElementById('enhanced-markdown-font').value==='2'"),
      "last font choice was not restored",
    );
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
    "markdown-display-fonts: bundled fonts, page dropdown, persistence, scaling, themes, selection, and narrow layout passed",
  );
}
if (import.meta.main) await runStandalone(testit);
