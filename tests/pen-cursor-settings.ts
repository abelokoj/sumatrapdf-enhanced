import { ptr } from "bun:ffi";
import { join } from "node:path";
import { readFileSync } from "node:fs";
import { cmdId, pollUntil, runStandalone, writeAppdata } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync, waitForExit } from "./win-automation.ts";
import {
  enumWindows,
  enumChildWindows,
  getWindowPid,
  getWindowText,
  getClassName,
  isWindowVisible,
  sendMessage,
  postMessage,
} from "./winapi.ts";

function assert(ok: unknown, message: string): asserts ok {
  if (!ok) throw new Error(`pen-cursor-settings: ${message}`);
}
function item(hwnd: number, index: number): string {
  const data = new Uint16Array(256);
  const size = Number(sendMessage(hwnd, 0x148, index, ptr(data)));
  return size < 0 ? "" : String.fromCharCode(...data.subarray(0, size));
}
function read(hwnd: number): string {
  const data = new Uint16Array(256);
  const size = Number(sendMessage(hwnd, 0x000d, data.length, ptr(data)));
  return String.fromCharCode(...data.subarray(0, size));
}
function text(hwnd: number, value: string): void {
  sendMessage(hwnd, 0x14e, -1, 0);
  const data = new Uint16Array([...value].map((c) => c.charCodeAt(0)).concat(0));
  sendMessage(hwnd, 0x000c, 0, ptr(data));
}
function controls(pid: number) {
  const result = { window: 0, style: 0, accent: 0, size: 0 };
  enumWindows((window) => {
    if (getWindowPid(window) !== pid || !isWindowVisible(window) || getWindowText(window) !== "Settings") return true;
    result.window = window;
    sendMessage(window, 0x3d, 0, -25);
    enumChildWindows(window, (combo) => {
      if (getClassName(combo) !== "ComboBox") return true;
      if (item(combo, 0) === "Rollerball (11)") result.style = combo;
      if (item(combo, 0) === "Blue" && item(combo, 1) === "Red") result.accent = combo;
      if (item(combo, 0) === "0" && item(combo, 1) === "1" && item(combo, 2) === "4") result.size = combo;
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
    (v) => !!v.style && !!v.accent && !!v.size,
    { error: "Drawing cursor controls missing" },
  );
}
export async function testit(): Promise<void> {
  for (const theme of ["Sumatra Light", "Sumatra Dark"]) {
    const dir = writeAppdata(
      `pen-cursor-${theme.replaceAll(" ", "-")}`,
      `UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nReuseInstance = false\nTheme = ${theme}\n`,
    );
    let app = await launchControlled(["-appdata", dir], { saveSettings: true });
    try {
      for (const choice of [
        { style: 0, id: 11, accent: "Blue", saved: "#2675e8", size: 100 },
        { style: 1, id: 12, accent: "Red", saved: "#e43d46", size: 1 },
        { style: 2, id: 9, accent: "#00a879", saved: "#00a879", size: 0 },
      ]) {
        const ui = await settings(app);
        assert(Number(sendMessage(ui.style, 0x146, 0, 0)) === 3, "All three approved pen types must be available");
        sendMessage(ui.style, 0x14e, choice.style, 0);
        text(ui.accent, choice.accent);
        text(ui.size, `${choice.size} px`);
        postMessage(ui.window, 0x100, 13, 0);
        await pollUntil(
          () => isWindowVisible(ui.window),
          (visible) => !visible,
          { error: "Cursor settings did not apply" },
        );
        const reopened = await settings(app);
        assert(Number(sendMessage(reopened.style, 0x147, 0, 0)) === choice.style, "Pen type did not persist");
        assert(read(reopened.size) === String(choice.size), "Custom cursor size did not persist");
        assert(
          read(reopened.accent) === choice.saved || read(reopened.accent) === choice.accent,
          "Accent did not persist",
        );
        text(reopened.accent, "#ffaa00");
        text(reopened.size, "48");
        sendMessage(reopened.window, 0x10, 0, 0);
        const cancelled = await settings(app);
        assert(read(cancelled.size) === String(choice.size), "Cancelled size was applied");
        assert(read(cancelled.accent) !== "#ffaa00", "Cancelled accent was applied");
        sendMessage(cancelled.window, 0x10, 0, 0);
        console.log(`${theme}: pen ${choice.id}, ${choice.saved}, ${choice.size}px applied; Cancel preserved settings`);
      }
      postMessage(app.frame, 0x10, 0, 0);
      assert(await waitForExit(app.proc), "App did not close");
      app.client.close();
      const saved = readFileSync(join(dir, "SumatraPDFEnhanced-settings.txt"), "utf8");
      assert(
        /PenCursorStyle = 9/.test(saved) && /PenCursorSize = 0/.test(saved) && /PenCursorAccent = #00a879/i.test(saved),
        "Cursor preferences were not saved to disk",
      );
      app = await launchControlled(["-appdata", dir], { saveSettings: true });
      const ui = await settings(app);
      assert(
        Number(sendMessage(ui.style, 0x147, 0, 0)) === 2 && read(ui.size) === "0" && read(ui.accent) === "#00a879",
        "Cursor preferences did not survive restart",
      );
      sendMessage(ui.window, 0x10, 0, 0);
    } finally {
      app.client.close();
      await killAndWait(app.proc);
    }
  }
}
if (import.meta.main) await runStandalone(testit);
