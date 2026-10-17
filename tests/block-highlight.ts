import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { assemblePdf, cmdId, pollUntil, runStandalone, tmpPath } from "./util.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";
import { captureWindowToPng, MK_CONTROL, MK_LBUTTON, packCoords, postMessage, sendMessage } from "./winapi.ts";

export async function testit(): Promise<void> {
  const dir = tmpPath("block-highlight");
  mkdirSync(dir, { recursive: true });
  const pdf = join(dir, "block.pdf");
  const text = "BT /F1 32 Tf 72 420 Td (Linear) Tj ET";
  writeFileSync(
    pdf,
    assemblePdf([
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>",
      `<< /Length ${text.length} >>\nstream\n${text}\nendstream`,
      "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    ]),
    "latin1",
  );
  writeFileSync(
    join(dir, "SumatraPDFEnhanced-settings.txt"),
    "UiLanguage = en\nRestoreSession = false\nCheckForUpdates = false\nAnnotations [\n HighlightColor = #87cefa\n]\n",
  );
  const { proc, client, frame } = await launchControlled([
    "-appdata",
    dir,
    "-view",
    "single page",
    "-zoom",
    "fit page",
    pdf,
  ]);
  try {
    await client.waitForRenderIdle();
    const canvas = findCanvas(frame);
    const layout = await client.layout();
    const page = /^page n=1 shown=1 .*screen=(-?\d+),(-?\d+),(\d+),(\d+)/m.exec(layout.raw);
    if (!canvas || !page) throw new Error(`block-highlight: page missing\n${layout.raw}`);
    const [px, py, width, height] = page.slice(1).map(Number) as [number, number, number, number];
    const start = { x: Math.round(px + width * 0.12), y: Math.round(py + height * 0.42) };
    const end = { x: Math.round(px + width * 0.5), y: Math.round(py + height * 0.51) };
    sendMessage(canvas, 0x0201, MK_CONTROL | MK_LBUTTON, packCoords(start.x, start.y));
    sendMessage(canvas, 0x0200, MK_CONTROL | MK_LBUTTON, packCoords(end.x, end.y));
    sendMessage(canvas, 0x0202, MK_CONTROL, packCoords(end.x, end.y));
    const selected = String((await client.request(ControlCommand.TestSelectionVars, []))[1]);
    const rect = /^rect=([^\n]+) page=1/m.exec(selected)?.[1];
    if (!rect || !/nrects=1/.test(selected)) throw new Error(`block-highlight: Ctrl-drag failed\n${selected}`);
    postMessage(frame, 0x0100, 0x41, 0);
    postMessage(frame, 0x0101, 0x41, 0);
    let raw = await pollUntil(
      async () => String((await client.request(ControlCommand.TestMarkupAnnots, []))[1]),
      (value) => /type=Highlight page=1 quads=1/.test(value),
      { error: (value) => `block-highlight: A did not highlight the Ctrl-drag region\n${value}` },
    );
    const quad = /^rect=([^\n]+)$/m.exec(raw)?.[1];
    if (quad !== rect) throw new Error(`block-highlight: region changed (${rect} -> ${quad})\n${raw}`);
    if (!/color=#?(?:ff)?87cefa/i.test(raw)) throw new Error(`block-highlight: configured color missing\n${raw}`);
    const bounds = /^bounds=([^\n]+)/m.exec(raw)?.[1];
    if (bounds !== quad) throw new Error(`block-highlight: appearance extends outside its quad (${bounds} -> ${quad})`);
    sendCommandSync(frame, cmdId("CmdUndo"));
    raw = await pollUntil(
      async () => String((await client.request(ControlCommand.TestMarkupAnnots, []))[1]),
      (value) => /annotations=0/.test(value),
      { error: "block-highlight: one Undo did not remove the block highlight" },
    );
    sendCommandSync(frame, cmdId("CmdRedo"));
    await pollUntil(
      async () => String((await client.request(ControlCommand.TestMarkupAnnots, []))[1]),
      (value) => /type=Highlight page=1 quads=1/.test(value),
      { error: "block-highlight: Redo did not restore the block highlight" },
    );
    if (process.env.SUMATRA_HIGHLIGHT_CAPTURE_DIR) {
      const output = process.env.SUMATRA_HIGHLIGHT_CAPTURE_DIR;
      mkdirSync(output, { recursive: true });
      sendMessage(frame, 0x0100, 0x1b, 0);
      sendCommandSync(frame, cmdId("CmdSaveAnnotations"));
      await client.waitForRenderIdle();
      if (!captureWindowToPng(frame, join(output, "block-highlight.png"))) throw new Error("Highlight capture failed");
    }
    sendCommandSync(frame, cmdId("CmdUndo"));
    await client.seedTextSelection(1);
    sendCommandSync(frame, cmdId("CmdCreateAnnotHighlight"));
    raw = await pollUntil(
      async () => String((await client.request(ControlCommand.TestMarkupAnnots, []))[1]),
      (value) => /type=Highlight page=1 quads=1/.test(value),
      { error: "block-highlight: normal text highlight stopped working" },
    );
    await client.waitForRenderIdle();
    raw = String((await client.request(ControlCommand.TestMarkupAnnots, []))[1]);
    const textHighlight = raw.split("type=Highlight").at(-1)!;
    const textBounds = /^bounds=([^\n]+)/m.exec(textHighlight)?.[1];
    const textQuad = /^rect=([^\n]+)$/m.exec(textHighlight)?.[1];
    if (!textBounds || textBounds !== textQuad)
      throw new Error(`block-highlight: normal highlight bulges beyond its text\n${raw}`);
    if (process.env.SUMATRA_HIGHLIGHT_CAPTURE_DIR) {
      sendMessage(frame, 0x0100, 0x1b, 0);
      sendCommandSync(frame, cmdId("CmdSaveAnnotations"));
      await client.waitForRenderIdle();
      if (!captureWindowToPng(frame, join(process.env.SUMATRA_HIGHLIGHT_CAPTURE_DIR, "text-highlight.png")))
        throw new Error("Text highlight capture failed");
    }
    console.log("Block highlight: Ctrl-drag, A, exact region, configured color, one-step Undo and Redo passed");
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

if (import.meta.main) await runStandalone(testit);
