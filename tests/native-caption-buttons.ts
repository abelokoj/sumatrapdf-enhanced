// Rectangular titlebar buttons retain separate minimize/maximize hit zones.
import { dlopen, FFIType } from "bun:ffi";
import { mkdirSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { cmdId, runStandalone, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";
import {
  clientToScreen,
  enumWindows,
  getWindowLong,
  getWindowPid,
  getWindowRect,
  getWindowText,
  GWL_STYLE,
  packCoords,
  sendMessage,
  setWindowLong,
  setWindowPos,
  sleep,
  SWP_FRAMECHANGED,
  SWP_NOACTIVATE,
  SWP_NOZORDER,
} from "./winapi.ts";

const metrics = dlopen("user32.dll", {
  GetDpiForWindow: { args: [FFIType.ptr], returns: FFIType.u32 },
  GetSystemMetricsForDpi: { args: [FFIType.i32, FFIType.u32], returns: FFIType.i32 },
});
const SWP_NOSIZE = 0x0001;
const SWP_NOMOVE = 0x0002;

export async function testit(): Promise<void> {
  const dir = tmpPath("native-caption-buttons");
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  writeFileSync(
    join(dir, "SumatraPDFEnhanced-settings.txt"),
    "UiLanguage = en\nRestoreSession = false\nCheckForUpdates = false\nRememberOpenedFiles = false\nInterfaceScale = 100\n",
  );
  const { proc, client, frame } = await launchControlled(["-appdata", dir]);
  try {
    sendCommandSync(frame, cmdId("CmdToggleKeyboardHelp"));
    let help = 0;
    for (let i = 0; i < 80 && !help; i++) {
      enumWindows((h) => {
        if (getWindowPid(h) === proc.pid && getWindowText(h) === "Keyboard Shortcuts") help = h;
        return true;
      });
      if (!help) await sleep(50);
    }
    if (!help) throw new Error("native-caption-buttons: help did not open");
    // Change only this disposable window to exercise the shared caption path
    // with all three buttons, independently of its normal close-only style.
    const old = getWindowLong(help, GWL_STYLE);
    setWindowLong(help, GWL_STYLE, old | 0x00030000);
    setWindowPos(help, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
    const dpi = metrics.symbols.GetDpiForWindow(help);
    const width = Math.max(metrics.symbols.GetSystemMetricsForDpi(30, dpi), Math.round((36 * dpi) / 96));
    const rect = getWindowRect(help),
      origin = clientToScreen(help, 0, 0);
    const y = origin.y - 3;
    console.log(
      `caption metrics frame=${JSON.stringify(rect)} clientOrigin=${JSON.stringify(origin)} dpi=${dpi} width=${width} y=${y}`,
    );
    for (const [offset, expected] of [
      [0, 20],
      [1, 9],
      [2, 8],
    ]) {
      // The outer frame pixels intentionally retain native corner resizing.
      for (const fraction of [0.3, 0.5, 0.7]) {
        const x = Math.round(rect.right - (offset! + fraction) * width);
        const hit = Number(sendMessage(help, 0x84, 0, packCoords(x, y)));
        if (hit !== expected)
          throw new Error(`native-caption-buttons: button ${offset} at ${fraction} hit ${hit}, expected ${expected}`);
      }
    }
    setWindowLong(help, GWL_STYLE, old);
    console.log("native-caption-buttons: rectangular close/maximize/minimize hit zones passed");
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

if (import.meta.main) await runStandalone(testit);
