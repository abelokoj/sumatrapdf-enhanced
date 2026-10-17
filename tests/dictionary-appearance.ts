import { ptr } from "bun:ffi";
import { copyFileSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { ControlCommand, type ControlClient } from "./control.ts";
import { ROOT, cmdId, pollUntil, runStandalone, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync, waitForExit } from "./win-automation.ts";
import {
  enumChildWindows,
  enumWindows,
  getClassName,
  getWindowPid,
  getWindowText,
  isWindowVisible,
  sendMessage,
  postMessage,
} from "./winapi.ts";

const REVISION = "3d7870d8a8cdcd891e352e08f5c4ecc3451680f0";
function assert(ok: unknown, msg: string): asserts ok {
  if (!ok) throw new Error(`dictionary-appearance: ${msg}`);
}
function item(combo: number, index: number): string {
  const buffer = new Uint16Array(256);
  const size = Number(sendMessage(combo, 0x148, index, ptr(buffer)));
  return size >= 0 ? String.fromCharCode(...buffer.subarray(0, size)) : "";
}
function controls(pid: number): {
  window: number;
  font: number;
  spacing: number;
  dictionaryScale: number;
  learningScale: number;
} {
  const result = { window: 0, font: 0, spacing: 0, dictionaryScale: 0, learningScale: 0 };
  enumWindows((window) => {
    if (getWindowPid(window) !== pid || !isWindowVisible(window) || !getWindowText(window).includes("Settings"))
      return true;
    sendMessage(window, 0x3d, 0, -25);
    result.window = window;
    enumChildWindows(window, (combo) => {
      if (getClassName(combo) !== "ComboBox") return true;
      if (item(combo, 0) === "Follow interface font" && item(combo, 1) === "App fonts") result.font = combo;
      if (item(combo, 0) === "75" && item(combo, 5) === "300") {
        if (!result.dictionaryScale) result.dictionaryScale = combo;
        else result.learningScale = combo;
      }
      if (item(combo, 0) === "0.8") result.spacing = combo;
      return true;
    });
    return !result.font || !result.spacing;
  });
  return result;
}
function readText(hwnd: number): string {
  const buffer = new Uint16Array(256);
  const count = Number(sendMessage(hwnd, 0x000d, buffer.length, ptr(buffer)));
  return String.fromCharCode(...buffer.subarray(0, count));
}
function text(hwnd: number, value: string): void {
  sendMessage(hwnd, 0x14e, -1, 0);
  const buffer = new Uint16Array([...value].map((c) => c.charCodeAt(0)).concat(0));
  sendMessage(hwnd, 0x000c, 0, ptr(buffer));
}
async function state(client: ControlClient): Promise<string> {
  const result = await client.request(ControlCommand.TestInvokeCommand, ["settings-font-state"]);
  assert(result[0] === 0, "settings state unavailable");
  return String(result[1]);
}
export async function testit(): Promise<void> {
  const root = tmpPath("dictionary-appearance");
  rmSync(root, { recursive: true, force: true });
  mkdirSync(root, { recursive: true });
  for (const selected of ["Consolas", "New Computer Modern Book", "Lora"]) {
    const dir = join(root, selected.replaceAll(" ", "-"));
    mkdirSync(dir);
    const prefs = join(dir, "SumatraPDFEnhanced-settings.txt");
    writeFileSync(
      prefs,
      "UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nReuseInstance = false\nTheme = Sumatra Light\n",
    );
    if (selected === "New Computer Modern Book") {
      for (const file of [
        "NewCM10-Book.otf",
        "NewCM10-BookItalic.otf",
        "NewCM10-Bold.otf",
        "NewCM10-BoldItalic.otf",
        "License.txt",
        "GUST-FONT-LICENSE.txt",
      ]) {
        const path = join("new-computer-modern", file),
          dest = join(dir, "markdown-fonts", REVISION, path);
        mkdirSync(dirname(dest), { recursive: true });
        copyFileSync(join(ROOT, "src/fonts/markdown", path), dest);
      }
    }
    if (selected === "Lora" && process.env.SUMATRA_DICTIONARY_FONT_NETWORK !== "1") {
      for (const path of [
        "lora/otf/Lora-Regular.otf",
        "lora/otf/Lora-Italic.otf",
        "lora/otf/Lora-Bold.otf",
        "lora/otf/Lora-BoldItalic.otf",
        "lora/OFL.txt",
      ]) {
        const dest = join(dir, "markdown-fonts", REVISION, path);
        mkdirSync(dirname(dest), { recursive: true });
        copyFileSync(join(ROOT, "src/fonts/markdown", path), dest);
      }
    }
    const app = await launchControlled(["-appdata", dir], { saveSettings: true });
    try {
      sendCommandSync(app.frame, cmdId("CmdOptions"));
      const ui = await pollUntil(
        () => controls(app.proc.pid!),
        (v) => !!v.font && !!v.spacing && !!v.dictionaryScale && !!v.learningScale,
        { error: "dictionary controls missing" },
      );
      assert(Number(sendMessage(ui.font, 0x146, 0, 0)) > 17, "installed system fonts were omitted");
      text(ui.font, selected);
      text(ui.spacing, "0.875");
      text(ui.dictionaryScale, "137.5%");
      text(ui.learningScale, "85%");
      const labels = Array.from({ length: Number(sendMessage(ui.font, 0x146, 0, 0)) }, (_, i) => item(ui.font, i));
      assert(
        labels.includes("System fonts") && labels.indexOf("System fonts") > labels.indexOf("Lora"),
        "font groups are missing",
      );
      postMessage(ui.window, 0x100, 13, 0);
      const updated = await pollUntil(
        () => state(app.client),
        (s) => /busy=0/.test(s) && s.includes(`dictionary=${selected}`),
        { timeoutMs: 60000, error: (s) => `selection did not apply: ${s}` },
      );
      assert(updated.includes("spacing=0.875"), "custom spacing was not applied");
      assert(!isWindowVisible(ui.window), "Settings did not close after applying");
      sendCommandSync(app.frame, cmdId("CmdDictionaryLookup"));
      sendCommandSync(app.frame, cmdId("CmdOptions"));
      const reopened = await pollUntil(
        () => controls(app.proc.pid!),
        (v) => !!v.font,
        { error: "Settings did not reopen" },
      );
      assert(
        readText(reopened.font) === selected &&
          readText(reopened.spacing) === "0.875" &&
          Number(readText(reopened.dictionaryScale).replace("%", "")) === 137.5 &&
          Number(readText(reopened.learningScale).replace("%", "")) === 85,
        `Settings reopened with stale values: ${readText(reopened.font)} / ${readText(reopened.spacing)}`,
      );
      sendMessage(reopened.window, 0x10, 0, 0);
      postMessage(app.frame, 0x10, 0, 0);
      assert(await waitForExit(app.proc), "app did not close");
      const saved = readFileSync(prefs, "utf8");
      assert(
        new RegExp(`DictionaryUI \\[\\s*FontName = ${selected}\\s*LineSpacing = 0\\.875`).test(saved),
        "dictionary preferences were not saved",
      );
      assert(
        /FontScale = 137\.5/.test(saved) && /LearningHubScale = 85/.test(saved),
        "independent text scales were not saved",
      );
      console.log(`Saved dictionary font ${selected}, spacing 0.875, dictionary scale 137.5%, Learning Hub scale 85%`);
    } finally {
      app.client.close();
      await killAndWait(app.proc);
    }
  }
}
if (import.meta.main) await runStandalone(testit);
