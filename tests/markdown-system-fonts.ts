import { ptr } from "bun:ffi";
import { existsSync, mkdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { cmdId, pollUntil, runStandalone, writeAppdata } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";
import {
  captureWindowToPng,
  enumChildWindows,
  enumWindows,
  getClassName,
  getWindowLong,
  getWindowPid,
  getWindowText,
  isWindowVisible,
  postMessage,
  sendMessage,
} from "./winapi.ts";

function check(ok: unknown, message: string): asserts ok {
  if (!ok) throw new Error(`markdown-system-fonts: ${message}`);
}
function item(combo: number, index: number): string {
  const buffer = new Uint16Array(256);
  const count = Number(sendMessage(combo, 0x148, index, ptr(buffer)));
  return count < 0 ? "" : String.fromCharCode(...buffer.subarray(0, count));
}
function fields(pid: number) {
  const found = { window: 0, text: 0, math: 0 };
  enumWindows((window) => {
    if (getWindowPid(window) !== pid || !isWindowVisible(window) || getWindowText(window) !== "Settings") return true;
    found.window = window;
    sendMessage(window, 0x3d, 0, -25);
    enumChildWindows(window, (combo) => {
      if (getClassName(combo) !== "ComboBox") return true;
      if (item(combo, 0) === "Follow interface font" && item(combo, 1) !== "App fonts") found.text = combo;
      if (item(combo, 0) === "Match text font") found.math = combo;
      return true;
    });
    return false;
  });
  return found;
}
function labels(combo: number) {
  return Array.from({ length: Number(sendMessage(combo, 0x146, 0, 0)) }, (_, i) => item(combo, i));
}
function selected(combo: number) {
  return item(combo, Number(sendMessage(combo, 0x147, 0, 0)));
}
function choose(window: number, combo: number, index: number) {
  sendMessage(combo, 0x14e, index, 0);
  sendMessage(window, 0x111, (1 << 16) | (getWindowLong(combo, -12) & 0xffff), combo);
}
export async function testit() {
  for (const [theme, scale] of [
    ["Light", 100],
    ["Dark", 200],
  ] as const) {
    const profile = writeAppdata(
      `markdown-system-${theme}`,
      `UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nReuseInstance = false\nTheme = Sumatra ${theme}\nInterfaceScale = ${scale}\n`,
    );
    const args = ["-appdata", profile];
    let app = await launchControlled(args, { saveSettings: true });
    async function open() {
      sendCommandSync(app.frame, cmdId("CmdOptions"));
      return pollUntil(
        () => fields(app.proc.pid!),
        (v) => !!v.text && !!v.math,
        { error: "Markdown font controls missing" },
      );
    }
    try {
      let ui = await open();
      for (const [combo, name] of [
        [ui.text, "Consolas"],
        [ui.math, "Cambria Math"],
      ] as const) {
        const options = labels(combo);
        check(options.includes(name), `${name} is missing from the font picker`);
        const downloaded = options.indexOf("Downloadable fonts"),
          system = options.indexOf("System fonts");
        check(
          downloaded === 1 && system > downloaded && options.indexOf(name) > system,
          "font sections are missing or misplaced",
        );
        const captures = process.env.SUMATRA_FONT_CAPTURE_DIR;
        if (captures) {
          mkdirSync(captures, { recursive: true });
          sendMessage(combo, 0x7, 0, 0);
          sendMessage(combo, 0x14f, 1, 0);
          sendMessage(combo, 0x15c, Math.max(0, system - 2), 0);
          const info = new Uint8Array(64);
          new DataView(info.buffer).setUint32(0, info.length, true);
          check(sendMessage(combo, 0x164, 0, ptr(info)), "font dropdown geometry unavailable");
          const list = Number(new DataView(info.buffer).getBigUint64(56, true));
          check(
            captureWindowToPng(
              list,
              join(captures, `${theme.toLowerCase()}-${combo === ui.text ? "text" : "math"}-groups.png`),
            ),
            "font group capture failed",
          );
          sendMessage(combo, 0x14f, 0, 0);
        }
        for (const heading of [downloaded, system]) {
          choose(ui.window, combo, 0);
          choose(ui.window, combo, heading);
          check(Number(sendMessage(combo, 0x147, 0, 0)) !== heading, "a section heading was selected as a font");
        }
        choose(ui.window, combo, options.indexOf(name));
      }
      postMessage(ui.window, 0x100, 13, 0);
      await pollUntil(
        () => isWindowVisible(ui.window),
        (visible) => !visible,
        { error: "system fonts did not apply offline" },
      );
      ui = await open();
      check(
        selected(ui.text) === "Consolas" && selected(ui.math) === "Cambria Math",
        "saved choices were not restored",
      );
      choose(ui.window, ui.text, 0);
      sendMessage(ui.window, 0x10, 0, 0);
      app.client.close();
      await killAndWait(app.proc);
      app = await launchControlled(args, { saveSettings: true });
      ui = await open();
      check(
        selected(ui.text) === "Consolas" && selected(ui.math) === "Cambria Math",
        "choices did not survive restart or Cancel changed them",
      );
      sendMessage(ui.window, 0x10, 0, 0);
      check(!existsSync(join(profile, "markdown-fonts")), "system fonts created an optional font download cache");
      const saved = readFileSync(join(profile, "SumatraPDFEnhanced-settings.txt"), "utf8");
      check(
        /MarkdownUI\s*\[[\s\S]*?FontName = Consolas\s+MathFontName = Cambria Math/.test(saved),
        "font names did not persist",
      );
      console.log(
        `PASS ${theme} ${scale}%: separated groups, system fonts, nonselectable headings, Save, Cancel, restart, no downloads`,
      );
    } finally {
      app.client.close();
      await killAndWait(app.proc);
    }
  }
}
if (import.meta.main) await runStandalone(testit);
