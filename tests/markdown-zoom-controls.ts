import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { cmdId, pollUntil, runStandalone, tmpPath } from "./util.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";
import { clientToScreen, enumChildWindows, getClassName, getWindowRect, packCoords, sendMessage } from "./winapi.ts";
import { Page, browserEnv, browserPort, restoreBrowserPolicy } from "./markdown-display-fonts.ts";
function check(ok: unknown, message: string): asserts ok {
  if (!ok) throw Error("markdown-zoom-controls: " + message);
}
const geometry = `(()=>{const m=document.getElementById('enhanced-markdown-content'),r=m.getBoundingClientRect(),probe=document.getElementById('zoom-chrome-probe');return {zoom:Number(getComputedStyle(m).zoom)||1,width:r.width,height:r.height,dpr:devicePixelRatio,probe:probe?probe.getBoundingClientRect().width:0};})()`;
async function expectZoom(page: Page, expected: number) {
  await pollUntil(
    () => page.eval(geometry),
    (g) => Math.abs(g.zoom - expected) < 0.002,
    { timeoutMs: 4000, error: `document-only zoom did not become ${expected}` },
  );
}
export async function testit() {
  const root = tmpPath("markdown-zoom-controls");
  mkdirSync(root, { recursive: true });
  for (const initial of ["100", "125", "fit page", "saved"]) {
    const dir = join(root, initial.replaceAll(" ", "-"));
    mkdirSync(dir, { recursive: true });
    const md = join(dir, "Zoom.md");
    writeFileSync(
      md,
      "# Zoom sample\n\n## Section\n\n" + ("A paragraph for document zoom and scroll. ".repeat(15) + "\n\n").repeat(25),
    );
    writeFileSync(
      join(dir, "SumatraPDFEnhanced-settings.txt"),
      `UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nShowToc = true\nSidebarDx = 220\nDefaultZoom = ${initial === "saved" ? "100" : initial}\nRememberStatePerDocument = true\n${initial === "saved" ? `FileStates [\n [\n FilePath = ${md}\n Zoom = 150\n PageNo = 1\n ]\n]\n` : ""}ZoomIncrement = 0\nZoomLevels = 100 125 150 200\nMarkdownUI [\nUseFixedPageUI = false\n]\n`,
    );
    const browser = join(dir, `browser-${process.pid}`);
    mkdirSync(browser, { recursive: true });
    let page: Page | undefined;
    let run: Awaited<ReturnType<typeof launchControlled>> | undefined;
    try {
      run = await launchControlled(["-appdata", dir, "-html-backend", "webview2", md], { env: browserEnv(browser) });
      page = await Page.connect(await browserPort(browser));
      await page.until("document.getElementById('enhanced-markdown-content') && document.readyState==='complete'");
      const expected = initial === "125" ? 1.25 : initial === "saved" ? 1.5 : 1;
      await expectZoom(page, expected);
      if (initial !== "100") {
        console.log(`Markdown default ${initial} -> document zoom ${expected}`);
        continue;
      }
      await page.eval(
        "(()=>{let p=document.createElement('div');p.id='zoom-chrome-probe';p.style='position:fixed;right:0;top:0;width:80px;height:20px';document.body.appendChild(p);})()",
      );
      const baseline = await page.eval(geometry);
      let sidebar = 0;
      enumChildWindows(run.frame, (h) => {
        if (getClassName(h) === "SysTreeView32") sidebar = h;
        return true;
      });
      const sideRect = sidebar ? getWindowRect(sidebar) : null;
      const sideFont = sidebar ? sendMessage(sidebar, 0x31, 0, 0) : 0;
      sendCommandSync(run.frame, cmdId("CmdZoom125"));
      await expectZoom(page, 1.25);
      const zoomed = await page.eval(geometry);
      check(zoomed.dpr === baseline.dpr, "browser zoom scaled all interface content");
      check(Math.abs(zoomed.probe - baseline.probe) < 0.01, "content zoom resized browser chrome: " + JSON.stringify({baseline,zoomed}));
      sendCommandSync(run.frame, cmdId("CmdZoomIn"));
      await expectZoom(page, 1.5);
      const canvas = findCanvas(run.frame);
      check(canvas, "missing canvas");
      const pt = clientToScreen(canvas, 100, 100);
      sendMessage(canvas, 0x20a, (120 << 16) | 8, packCoords(pt.x, pt.y));
      await expectZoom(page, 2);
      sendMessage(run.frame, 0x20a, (-120 << 16) | 8, packCoords(pt.x, pt.y));
      await expectZoom(page, 1.5);
      await page.command("Input.dispatchMouseEvent", {
        type: "mouseWheel",
        x: 200,
        y: 150,
        deltaX: 0,
        deltaY: -120,
        modifiers: 2,
      });
      await expectZoom(page, 2);
      const result = await run.client.request(ControlCommand.TestDisplayMode, ["zoom-real"]);
      check(String(result[1]).includes("zoomReal=200"), "main controller disagrees with wheel zoom: " + result);
      if (sidebar) {
        check(JSON.stringify(getWindowRect(sidebar)) === JSON.stringify(sideRect), "sidebar rectangle changed");
        check(sendMessage(sidebar, 0x31, 0, 0) === sideFont, "sidebar font changed");
      }
      await page.command("Page.reload");
      await page.until("document.readyState==='complete' && document.getElementById('enhanced-markdown-content')");
      await expectZoom(page, 2);
      await page.capture(join(dir, "native-browser-after.png"));
      console.log("Markdown controls, canvas/frame/browser wheel, unchanged sidebar and reload zoom passed");
    } finally {
      page?.close();
      if (run) {
        run.client.close();
        await killAndWait(run.proc);
      }
      restoreBrowserPolicy();
    }
  }
}
if (import.meta.main) await runStandalone(testit);
