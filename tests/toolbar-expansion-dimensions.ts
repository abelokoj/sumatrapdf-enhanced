// Expanding annotation controls changes scroll content, not dock thickness.
import { mkdirSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { cmdId, ROOT, runStandalone, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";
import { findChildWindow, getClientRect, getWindowRect, sleep } from "./winapi.ts";

export async function testit(): Promise<void> {
  const dir = tmpPath("toolbar-expansion-dimensions");
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  const results: unknown[] = [];
  const failures: string[] = [];
  const presets = Array.from(
    { length: 48 },
    (_, i) =>
      ` [\n Tool = ballpoint\n Color = #${(0x204080 + i * 977).toString(16).padStart(6, "0")}\n Width = ${(0.5 + i / 10).toFixed(1)}\n ]`,
  ).join("\n");
  const cases = [];
  for (const scale of [100, 200]) {
    cases.push({ scale, columns: 1, style: 1, dock: "top" });
    for (const columns of [1, 2, 3])
      for (const style of [1, 2])
        for (const dock of ["left", "right"]) {
          if (columns === 1 && style === 2) continue;
          cases.push({ scale, columns, style, dock });
        }
  }
  for (const c of cases) {
    const name = `${c.dock}-${c.columns}-${c.style}-${c.scale}`;
    if (process.env.SUMATRA_TOOLBAR_DIMENSIONS_CASE && !name.includes(process.env.SUMATRA_TOOLBAR_DIMENSIONS_CASE))
      continue;
    const profile = join(dir, name);
    mkdirSync(profile, { recursive: true });
    writeFileSync(
      join(profile, "SumatraPDFEnhanced-settings.txt"),
      `UiLanguage = en\nRestoreSession = false\nCheckForUpdates = false\nRememberOpenedFiles = false\nInterfaceScale = ${c.scale}\nUIFontSize = ${c.scale === 100 ? 18 : 22}\nAnnotationToolbarColumns = ${c.columns}\nAnnotationToolbarStyle = ${c.style}\nPinnedAnnotationTools [\n${presets}\n]\n`,
    );
    const s = await launchControlled([
      "-appdata",
      profile,
      "-window-pos",
      "1300x850@20x20",
      join(ROOT, "ext", "a-zlib", "zlib.3.pdf"),
    ]);
    let lastRaw = "";
    try {
      await s.client.setNotificationsEnabled(false);
      await s.client.waitForRenderIdle();
      if (c.dock !== "top") {
        sendCommandSync(s.frame, cmdId("CmdAnnotationMode"));
        for (let ready = 0; ready < 40; ready++) {
          const [, state] = await s.client.request(ControlCommand.TestToolbarButtons, []);
          if (/modeButton .*hidden=0/.test(String(state))) break;
          await sleep(50);
        }
        // Annotation Mode schedules a deferred layout; dock only after the new grip is placed.
        await s.client.waitForRenderIdle();
        await sleep(250);
        await s.client.request(ControlCommand.TestToolbarButtons, [`dock-${c.dock}`]);
      }
      const toolbar = findChildWindow(s.frame, "SUMATRA_VIRT_TOOLBAR");
      const snapshots = [];
      for (let n = 0; n < 4; n++) {
        await sleep(100);
        const [, raw] = await s.client.request(ControlCommand.TestToolbarButtons, []);
        lastRaw = String(raw);
        const rect = getWindowRect(toolbar),
          client = getClientRect(toolbar);
        snapshots.push({
          expanded: /annotationButtons=\d+ visible=1/.test(String(raw)),
          rect,
          client,
          raw: String(raw),
        });
        sendCommandSync(s.frame, cmdId("CmdToggleEditPDF"));
      }
      const first = snapshots[0]!;
      if (!snapshots.some((s) => s.expanded) || !snapshots.some((s) => !s.expanded))
        throw new Error("annotation expansion did not toggle");
      const thickness = (s: typeof first) => (c.dock === "top" ? s.client.bottom : s.client.right);
      for (const snapshot of snapshots) {
        if (thickness(snapshot) !== thickness(first))
          throw new Error(`dock thickness changed ${thickness(first)} to ${thickness(snapshot)}`);
        if (c.dock !== "top" && !snapshot.raw.includes(`annotationColumns=${c.columns}`))
          throw new Error("wrong column configuration");
        if (c.dock !== "top" && !/annotationMode=1 dock=[23] vertical=1/.test(snapshot.raw))
          throw new Error("synthetic dock action did not reach requested vertical configuration");
      }
      const scroll = snapshots.map((s) => Number(/annotationColumns=\d+ scroll=\d+,(\d+)/.exec(s.raw)?.[1] ?? 0));
      if (c.dock !== "top" && !scroll.some((n) => n > 0))
        throw new Error("48 pinned tools did not exercise vertical overflow");
      results.push({ name, status: "passed", thickness: thickness(first), scroll, snapshots });
      console.log(`toolbar-expansion-dimensions: ${name} passed thickness=${thickness(first)}`);
    } catch (e) {
      failures.push(`${name}: ${e}`);
      results.push({ name, status: "failed", error: String(e), lastRaw });
    } finally {
      s.client.close();
      await killAndWait(s.proc);
    }
  }
  const output = process.env.SUMATRA_TOOLBAR_DIMENSIONS_OUTPUT || join(dir, "results.json");
  writeFileSync(output, JSON.stringify(results, null, 2));
  if (failures.length) throw new Error(failures.join("\n"));
  console.log(`toolbar-expansion-dimensions: ${results.length} fixed-configuration expansion checks passed`);
}

if (import.meta.main) await runStandalone(testit);
