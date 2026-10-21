import { ptr } from "bun:ffi";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { cmdId, pollUntil, runStandalone, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync, waitForExit } from "./win-automation.ts";
import {
  enumChildWindows,
  enumWindows,
  getClassName,
  getControlText,
  getWindowLong,
  getWindowPid,
  isWindowVisible,
  sendMessage,
  sendText,
  captureWindowToPng,
} from "./winapi.ts";
function check(ok: unknown, why: string): asserts ok {
  if (!ok) throw new Error("dictionary-all-sources: " + why);
}
function item(combo: number, index: number): string {
  const buffer = new Uint16Array(512);
  const size = Number(sendMessage(combo, 0x148, index, ptr(buffer)));
  return size < 0 ? "" : String.fromCharCode(...buffer.subarray(0, size));
}
function controls(pid: number) {
  const ui = { window: 0, source: 0, query: 0, lookup: 0, details: 0, status: 0 };
  enumWindows((hwnd) => {
    if (getWindowPid(hwnd) !== pid || getClassName(hwnd) !== "SumatraPDFEnhancedLearning" || !isWindowVisible(hwnd))
      return true;
    ui.window = hwnd;
    enumChildWindows(hwnd, (child) => {
      const klass = getClassName(child),
        id = getWindowLong(child, -12);
      if (klass === "ComboBox" && item(child, 0).startsWith("Offline")) ui.source = child;
      // Native LearningControl IDs are stable control identifiers, not command IDs.
      if (id === 102) ui.query = child;
      if (id === 103) ui.lookup = child;
      if (id === 117) ui.details = child;
      if (id === 130) ui.status = child;
      return true;
    });
    return false;
  });
  return ui;
}
export async function testit(): Promise<void> {
  const profile = tmpPath("dictionary-all-sources-" + Date.now());
  mkdirSync(join(profile, "dictionaries"), { recursive: true });
  writeFileSync(
    join(profile, "SumatraPDFEnhanced-settings.txt"),
    "UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nReuseInstance = false\n",
  );
  const layout = join(profile, "SumatraPDF-learning-layout.txt");
  writeFileSync(layout, "30 0 0 0"); // Existing four-field prefs; every online provider disabled.
  writeFileSync(
    join(profile, "dictionaries", "Downloaded-A.tsv"),
    "codexdictionaryfixture\tFirst downloaded definition\n",
  );
  writeFileSync(
    join(profile, "dictionaries", "Downloaded-B.tsv"),
    "codexdictionaryfixture\tSecond downloaded definition\n",
  );
  for (let run = 0; run < 2; run++) {
    const app = await launchControlled(["-appdata", profile], { saveSettings: true });
    try {
      sendCommandSync(app.frame, cmdId("CmdDictionaryLookup"));
      const ui = await pollUntil(
        () => controls(app.proc.pid!),
        (v) => !!v.source && !!v.query && !!v.lookup && !!v.details,
        { error: "Dictionary controls missing" },
      );
      const count = Number(sendMessage(ui.source, 0x146, 0, 0));
      const all = Array.from({ length: count }, (_, i) => item(ui.source, i)).findIndex((s) =>
        s.startsWith("All dictionaries"),
      );
      check(all >= 0, "All dictionaries option is missing");
      check(
        Number(sendMessage(ui.source, 0x147, 0, 0)) === (run ? all : 0),
        "source preference or legacy preference migration failed",
      );
      sendMessage(ui.source, 0x14e, all, 0);
      sendMessage(ui.window, 0x111, (1 << 16) | (getWindowLong(ui.source, -12) & 65535), ui.source);
      sendText(ui.query, "codexdictionaryfixture");
      check(
        !getControlText(ui.details).includes("First downloaded definition"),
        "typing started lookup without request",
      );
      sendMessage(ui.lookup, 0xf5, 0, 0);
      const details = await pollUntil(
        () => getControlText(ui.details),
        (t) => t.includes("First downloaded definition") && t.includes("Second downloaded definition"),
        { timeoutMs: 30000, error: (v) => "not all downloaded packs were returned: " + v },
      );
      check(details.includes("Downloaded-A") && details.includes("Downloaded-B"), "result source labels missing");
      check(details.includes("Online sources are disabled"), "disabled online sources were not reported");
      captureWindowToPng(ui.window, join(profile, "all-dictionaries.png"));
      check(readFileSync(layout, "utf8").trim() === "30 0 0 0 5", "all-source selection was not persisted");
      await app.client.quit();
      await waitForExit(app.proc);
    } finally {
      app.client.close();
      await killAndWait(app.proc);
    }
  }
  console.log(
    "All-source native lookup retained two downloaded packs, source labels, disabled-provider policy, explicit search, legacy preferences and selection persistence.",
  );
}
if (import.meta.main) await runStandalone(testit);
