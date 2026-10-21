// Selection highlight presets use the existing annotation palette, apply the
// chosen color, and save/reopen a standard PDF Highlight without altering text.
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlClient, ControlCommand, withControlledSumatra } from "./control.ts";
import { assemblePdf, cmdId, EXE, runStandalone, tmpPath } from "./util.ts";
import { sendCommandSync } from "./win-automation.ts";
import { sleep } from "./winapi.ts";

const LINE = "Highlight palette regression sample.";
function samplePdf(): string {
  const stream = `BT /F1 18 Tf 72 700 Td (${LINE}) Tj ET`;
  return assemblePdf([
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    `<< /Length ${stream.length} >>\nstream\n${stream}\nendstream`,
  ]);
}
async function expectState(client: ControlClient, pattern: RegExp): Promise<string> {
  const deadline = Date.now() + 5000;
  let state = "";
  do {
    state = String((await client.request(ControlCommand.TestMarkupAnnots, []))[1] ?? "");
    if (pattern.test(state)) return state;
    await sleep(25);
  } while (Date.now() < deadline);
  throw new Error(`highlight-presets: missing ${pattern}\n${state}`);
}
export async function testit(): Promise<void> {
  const dir = tmpPath("highlight-presets-profile");
  mkdirSync(dir, { recursive: true });
  const pdf = tmpPath("highlight-presets.pdf");
  writeFileSync(pdf, samplePdf(), "latin1");
  writeFileSync(
    join(dir, "SumatraPDFEnhanced-settings.txt"),
    [
      "RestoreSession = false",
      "CheckForUpdates = false",
      "SelectionToolbar = true",
      "Annotations [",
      "PresetColors = #ffff00 #14a05a #99defa #f199d2 #ff9a40 #c680ff",
      "]",
      "",
    ].join("\n"),
  );
  let originalText = "";
  await withControlledSumatra(
    EXE,
    async (client, proc) => {
      await client.waitForRenderIdle();
      await client.seedTextSelection(1);
      const before = String((await client.request(ControlCommand.TestMarkupAnnots, []))[1] ?? "");
      originalText = /^page1text=(.*)$/m.exec(before)?.[1] ?? "";
      const dump = String((await client.request(ControlCommand.TestSelectionToolbar, []))[1] ?? "");
      if (!/^highlightPresets=6$/m.test(dump) || !/highlightPreset=2 color=#(?:ff)?14a05a/i.test(dump))
        throw new Error(`highlight-presets: configured colors were not retained\n${dump}`);
      await client.request(ControlCommand.TestSelectionToolbar, ["Show"]);
      const picked = await client.request(ControlCommand.TestSelectionToolbar, ["HighlightPreset3"]);
      if (picked[0] !== 0) throw new Error(`highlight-presets: swatch click failed ${picked}`);
      await expectState(client, /^type=Highlight page=1 quads=[1-9]/m);
      const { findTopWindow } = await import("./winapi.ts");
      sendCommandSync(findTopWindow(proc.pid, "SUMATRA_PDF_FRAME"), cmdId("CmdSaveAnnotations"));
      await client.waitForRenderIdle();
    },
    ["-appdata", dir, pdf],
  );
  await withControlledSumatra(
    EXE,
    async (client) => {
      await client.waitForRenderIdle();
      const reopened = await expectState(client, /^type=Highlight page=1 quads=[1-9]/m);
      const color = /^color=([^\n]*)/m.exec(reopened)?.[1] ?? "";
      if (color.toLowerCase() !== "#99defa")
        throw new Error(`highlight-presets: reopened Highlight has wrong color ${color}\n${reopened}`);
      if (!originalText || /^page1text=(.*)$/m.exec(reopened)?.[1] !== originalText)
        throw new Error("highlight-presets: document text changed");
      await client.seedTextSelection(1);
      const dump = String((await client.request(ControlCommand.TestSelectionToolbar, []))[1] ?? "");
      if (!/highlightPreset=2 color=#(?:ff)?14a05a/i.test(dump))
        throw new Error("highlight-presets: palette lost on reopen");
    },
    ["-appdata", dir, pdf],
  );
  console.log("highlight-presets: saved color, unchanged text, and reopened palette passed");
}
if (import.meta.main) await runStandalone(testit);
