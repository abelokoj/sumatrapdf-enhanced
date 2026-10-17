import { ptr } from "bun:ffi";
import { join } from "node:path";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { cmdId, pollUntil, runStandalone, writeAppdata, assemblePdf, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync, waitForExit, findChildByClass } from "./win-automation.ts";
import {
  enumWindows,
  enumChildWindows,
  getWindowPid,
  getWindowText,
  getClassName,
  isWindowVisible,
  sendMessage,
  postMessage,
  captureWindowToPng,
  captureWindowPixels,
} from "./winapi.ts";

function assert(ok: unknown, message: string): asserts ok {
  if (!ok) throw new Error(`active-tab-styles: ${message}`);
}
function item(hwnd: number, index: number): string {
  const data = new Uint16Array(256);
  const size = Number(sendMessage(hwnd, 0x148, index, ptr(data)));
  return size < 0 ? "" : String.fromCharCode(...data.subarray(0, size));
}
function read(hwnd: number): string {
  const data = new Uint16Array(256);
  const size = Number(sendMessage(hwnd, 0xd, data.length, ptr(data)));
  return String.fromCharCode(...data.subarray(0, size));
}
function text(hwnd: number, value: string): void {
  sendMessage(hwnd, 0x14e, -1, 0);
  const data = new Uint16Array([...value].map((c) => c.charCodeAt(0)).concat(0));
  sendMessage(hwnd, 0xc, 0, ptr(data));
}
function controls(pid: number) {
  const result = { window: 0, style: 0, accent: 0 };
  enumWindows((window) => {
    if (getWindowPid(window) !== pid || !isWindowVisible(window) || getWindowText(window) !== "Settings") return true;
    result.window = window;
    sendMessage(window, 0x3d, 0, -25);
    enumChildWindows(window, (combo) => {
      if (getClassName(combo) !== "ComboBox") return true;
      if (item(combo, 0) === "Outlined (3, default)") result.style = combo;
      if (item(combo, 0) === "Blue" && item(combo, 2) === "Teal") result.accent = combo;
      return true;
    });
    return false;
  });
  return result;
}
async function settings(app: Awaited<ReturnType<typeof launchControlled>>) {
  sendCommandSync(app.frame, cmdId("CmdOptions"));
  return pollUntil(
    () => controls(app.proc.pid!),
    (v) => !!v.style && !!v.accent,
    { error: "Active tab controls missing" },
  );
}
export async function testit(): Promise<void> {
  const pdf = tmpPath("active-tab-test.pdf");
  writeFileSync(
    pdf,
    assemblePdf([
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>",
    ]),
  );
  const captures = process.env.SUMATRA_TAB_CAPTURE_DIR || tmpPath("active-tab-captures");
  mkdirSync(captures, { recursive: true });
  for (const theme of ["Sumatra Light", "Sumatra Dark"]) {
    const label = theme.endsWith("Dark") ? "dark" : "light";
    const dir = writeAppdata(
      `active-tabs-${label}`,
      `UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nReuseInstance = false\nTheme = ${theme}\n`,
    );
    let app = await launchControlled(["-appdata", dir, pdf], { saveSettings: true });
    try {
      for (const choice of [
        { index: 0, id: 3, color: "#2675e8" },
        { index: 1, id: 1, color: "#ad344a" },
        { index: 2, id: 4, color: "#087969" },
      ]) {
        const ui = await settings(app);
        assert(Number(sendMessage(ui.style, 0x146, 0, 0)) === 3, "Expected the three approved styles");
        sendMessage(ui.style, 0x14e, choice.index, 0);
        text(ui.accent, choice.color);
        postMessage(ui.window, 0x100, 13, 0);
        await pollUntil(
          () => isWindowVisible(ui.window),
          (v) => !v,
          { error: "Tab settings did not apply" },
        );
        const reopened = await settings(app);
        assert(Number(sendMessage(reopened.style, 0x147, 0, 0)) === choice.index, "Style did not persist");
        assert(read(reopened.accent) === choice.color, "Accent did not persist");
        text(reopened.accent, "#ffaa00");
        sendMessage(reopened.style, 0x14e, (choice.index + 1) % 3, 0);
        sendMessage(reopened.window, 0x10, 0, 0);
        const cancelled = await settings(app);
        assert(
          Number(sendMessage(cancelled.style, 0x147, 0, 0)) === choice.index && read(cancelled.accent) === choice.color,
          "Cancel applied edits",
        );
        sendMessage(cancelled.window, 0x10, 0, 0);
        const bar = findChildByClass(app.frame, "SumatraTabsCtrlClass");
        assert(bar, "Tab bar missing");
        assert(captureWindowToPng(bar, join(captures, `style-${choice.id}-${label}.png`)), "Tab bar capture failed");
        if (choice.id !== 3) {
          const pixels = captureWindowPixels(bar);
          assert(pixels, "Tab pixels missing");
          const wanted = [1, 3, 5].map((i) => parseInt(choice.color.slice(i, i + 2), 16));
          let matches = 0;
          for (let i = 0; i < pixels.data.length; i += 4) {
            if (pixels.data[i] === wanted[2] && pixels.data[i + 1] === wanted[1] && pixels.data[i + 2] === wanted[0])
              matches++;
          }
          assert(matches > 100, "The running tab did not repaint with the selected accent");
        }
        console.log(`${theme}: style ${choice.id}, accent ${choice.color}; Cancel preserved settings`);
      }
      postMessage(app.frame, 0x10, 0, 0);
      assert(await waitForExit(app.proc), "App did not exit");
      app.client.close();
      const saved = readFileSync(join(dir, "SumatraPDFEnhanced-settings.txt"), "utf8");
      assert(
        /ActiveTabStyle = 4/.test(saved) && /ActiveTabAccent = #087969/i.test(saved),
        "Tab preferences not saved to disk",
      );
      app = await launchControlled(["-appdata", dir, pdf], { saveSettings: true });
      const ui = await settings(app);
      assert(
        Number(sendMessage(ui.style, 0x147, 0, 0)) === 2 && read(ui.accent) === "#087969",
        "Tab preferences did not survive restart",
      );
      sendMessage(ui.window, 0x10, 0, 0);
    } finally {
      app.client.close();
      await killAndWait(app.proc);
    }
  }
}
if (import.meta.main) await runStandalone(testit);
