import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ptr } from "bun:ffi";
import { cmdId, pollUntil, runStandalone, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommand } from "./win-automation.ts";
import { Page, browserEnv, browserPort, restoreBrowserPolicy } from "./markdown-display-fonts.ts";
import {
  enumWindows,
  enumChildWindows,
  getWindowText,
  getWindowPid,
  getClassName,
  getControlText,
  getWindowLong,
  isWindowVisible,
  postMessage,
  sendMessage,
} from "./winapi.ts";
function check(ok: unknown, message: string): asserts ok {
  if (!ok) throw new Error("markdown-reading-layout: " + message);
}
function settings(pid: number): number {
  let result = 0;
  enumWindows((hwnd) => {
    if (getWindowPid(hwnd) === pid && isWindowVisible(hwnd) && getWindowText(hwnd) === "Settings") {
      result = hwnd;
      return false;
    }
    return true;
  });
  return result;
}
function item(combo: number, index: number): string {
  const buffer = new Uint16Array(128);
  const n = Number(sendMessage(combo, 0x148, index, ptr(buffer)));
  return n < 0 ? "" : String.fromCharCode(...buffer.subarray(0, n));
}
function fields(window: number) {
  sendMessage(window, 0x3d, 0, -25);
  let width = 0;
  const margins: number[] = [];
  enumChildWindows(window, (hwnd) => {
    if (getClassName(hwnd) !== "ComboBox") return true;
    const count = Number(sendMessage(hwnd, 0x146, 0, 0));
    const values = Array.from({ length: count }, (_, i) => item(hwnd, i)).join("|");
    if (values === "0|50|60|70|80|90|100") width = hwnd;
    if (values === "0|5|10|15|20") margins.push(hwnd);
    return true;
  });
  check(width && margins.length === 2, "Markdown layout controls missing/ambiguous");
  return { width, left: margins[0], right: margins[1] };
}
function value(window: number, field: number, text: string) {
  const bytes = new Uint16Array([...text].map((c) => c.charCodeAt(0)).concat(0));
  sendMessage(field, 0x0c, 0, ptr(bytes));
  sendMessage(window, 0x111, (5 << 16) | (getWindowLong(field, -12) & 65535), field);
}
const geometry =
  "(()=>{const b=document.body.getBoundingClientRect(),w=document.documentElement.clientWidth;return {width:b.width,left:b.left,right:w-b.right,viewport:w,overflow:document.documentElement.scrollWidth>w};})()";
async function expectGeometry(page: Page, width: number, left: number, right: number) {
  const g = await page.eval(geometry);
  check(Math.abs(g.width - (g.viewport * width) / 100) < 1.1, "wrong reading width: " + JSON.stringify(g));
  check(
    Math.abs(g.left - (g.viewport * left) / 100) < 1.1 && Math.abs(g.right - (g.viewport * right) / 100) < 1.1,
    "wrong margins: " + JSON.stringify(g),
  );
}
export async function testit() {
  const directory = tmpPath("markdown-reading-layout");
  mkdirSync(directory, { recursive: true });
  for (const [theme, scale, width, left, right] of [
    ["Light", 100, 80, 10, 10],
    ["Dark", 200, 85, 10, 5],
  ] as const) {
    const profile = join(directory, theme);
    mkdirSync(profile, { recursive: true });
    const source = join(profile, "Layout.md");
    writeFileSync(
      source,
      "# Reading layout\n\n" +
        "Ordinary wrapping paragraph with readable words. ".repeat(20) +
        "\n\n| A | B |\n|---|---|\n|1|2|\n",
    );
    const prefs = join(profile, "SumatraPDFEnhanced-settings.txt");
    writeFileSync(
      prefs,
      "UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nTheme = Sumatra " +
        theme +
        "\nInterfaceScale = " +
        scale +
        "\nMarkdownUI [\nReadingWidth = " +
        width +
        "\nLeftMargin = " +
        left +
        "\nRightMargin = " +
        right +
        "\nUseFixedPageUI = false\n]\n",
    );
    const browser = join(profile, "browser");
    mkdirSync(browser, { recursive: true });
    const app = await launchControlled(["-appdata", profile, source], { saveSettings: true, env: browserEnv(browser) });
    let page: Page | undefined;
    try {
      page = await Page.connect(await browserPort(browser));
      await page.until("document.querySelector('main p') && document.readyState==='complete'");
      await expectGeometry(page, width, left, right);
      sendCommand(app.frame, cmdId("CmdOptions"));
      const window = await pollUntil(
        () => settings(app.proc.pid!),
        (v) => !!v,
        { error: "Settings did not open" },
      );
      let f = fields(window);
      value(window, f.width, "70");
      check(
        Math.abs(parseFloat(getControlText(f.left)) + parseFloat(getControlText(f.right)) - 30) < 0.01,
        "width did not derive margins",
      );
      value(window, f.left, "20");
      value(window, f.right, "15");
      check(parseFloat(getControlText(f.width)) === 65, "margins did not derive width");
      postMessage(window, 0x100, 13, 0);
      await pollUntil(
        () => isWindowVisible(window),
        (v) => !v,
        { error: "Settings Save did not close" },
      );
      await page.until("document.querySelector('main p') && document.readyState==='complete'");
      await pollUntil(
        async () => await page!.eval(geometry),
        (g) => Math.abs(g.width - g.viewport * 0.65) < 1.1,
        { error: "saved layout did not refresh" },
      );
      await expectGeometry(page, 65, 20, 15);
      sendCommand(app.frame, cmdId("CmdOptions"));
      const reopened = await pollUntil(
        () => settings(app.proc.pid!),
        (v) => !!v,
        { error: "Settings did not reopen" },
      );
      f = fields(reopened);
      check(
        parseFloat(getControlText(f.width)) === 65 &&
          parseFloat(getControlText(f.left)) === 20 &&
          parseFloat(getControlText(f.right)) === 15,
        "settings did not retain derived values",
      );
      value(reopened, f.left, "5");
      postMessage(reopened, 0x10, 0, 0);
      await pollUntil(
        () => isWindowVisible(reopened),
        (v) => !v,
        { error: "Settings Cancel did not close" },
      );
      await expectGeometry(page, 65, 20, 15);
      check(/ReadingWidth = 65/.test(readFileSync(prefs, "utf8")), "saved width did not persist");
      console.log(
        "PASS",
        theme,
        scale,
        ": actual DOM width/margins, dependent fields, Save refresh, reopen and Cancel",
      );
    } finally {
      page?.close();
      app.client.close();
      await killAndWait(app.proc);
      restoreBrowserPolicy();
    }
  }
}
if (import.meta.main) await runStandalone(testit);
