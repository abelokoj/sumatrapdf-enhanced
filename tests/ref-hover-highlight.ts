// Popup markup keeps the main selection and saves a standard PDF Highlight.
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { assemblePdf, cmdId, runStandalone, tmpPath } from "./util.ts";
import { ControlCommand } from "./control.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";
import {
  captureWindowPixels,
  clientToScreen,
  getClientRect,
  getClipboardText,
  packCoords,
  sendMessage,
  setProcessDpiAware,
} from "./winapi.ts";

function fixture() {
  const source = "BT /F1 18 Tf 72 700 Td (Main document selection stays unchanged.) Tj ET";
  const destination = "BT /F1 14 Tf 72 740 Td (Popup highlight target text.) Tj 0 -24 Td (Second popup line.) Tj ET";
  return assemblePdf([
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> /Contents 6 0 R /Annots [8 0 R] >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> /Contents 7 0 R >>",
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    `<< /Length ${source.length} >>\nstream\n${source}\nendstream`,
    `<< /Length ${destination.length} >>\nstream\n${destination}\nendstream`,
    "<< /Type /Annot /Subtype /Link /Rect [0 0 612 792] /Border [0 0 0] /Dest [4 0 R /XYZ 72 740 0] >>",
  ]);
}
async function markup(client: any) {
  return String((await client.request(ControlCommand.TestMarkupAnnots, []))[1]);
}
export async function testit(): Promise<void> {
  setProcessDpiAware();
  const dir = tmpPath(`ref-hover-highlight-${process.pid}`);
  mkdirSync(dir, { recursive: true });
  const pdf = join(dir, "popup-highlight.pdf");
  writeFileSync(pdf, fixture(), "latin1");
  writeFileSync(
    join(dir, "SumatraPDFEnhanced-settings.txt"),
    "RestoreSession = false\nCheckForUpdates = false\nShowToc = false\nCitationHoverDelay = 300\nAnnotations [\nHighlightColor = #99defa\n]\n",
  );
  const failures: string[] = [];
  const app = await launchControlled(["-appdata", dir, "-zoom", "fit page", pdf]);
  let saved = false;
  try {
    await app.client.waitForRenderIdle();
    await app.client.setNotificationsEnabled(false);
    await app.client.seedTextSelection(1);
    sendCommandSync(app.frame, cmdId("CmdCopySelection"));
    const originalSelection = await getClipboardText();
    if (!originalSelection.includes("Main document")) throw Error("Missing main selection setup");
    const canvas = findCanvas(app.frame),
      bounds = getClientRect(canvas);
    await app.client.request(ControlCommand.TestRefHover, [
      "show",
      Math.floor(bounds.right / 2),
      Math.floor(bounds.bottom / 3),
    ]);
    let state = "",
      popup = 0;
    for (let i = 0; i < 150; i++) {
      state = String((await app.client.request(ControlCommand.TestRefHover, []))[1]);
      const match = /visible=1 hwnd=(-?\d+)/.exec(state);
      if (match) {
        popup = +match[1]!;
        break;
      }
      await Bun.sleep(30);
    }
    if (!popup) throw Error("Popup did not open");
    await Bun.sleep(100);
    const border = +(/border=(\d+)/.exec(state)?.[1] || 0),
      pixels = captureWindowPixels(popup);
    if (!pixels || !border) throw Error("Missing popup geometry");
    let firstInk: { x: number; y: number } | undefined;
    for (let y = border; y < pixels.h - border && !firstInk; y++)
      for (let x = border; x < pixels.w - border; x++) {
        const at = (y * pixels.w + x) * 4;
        if (pixels.data[at]! < 80 && pixels.data[at + 1]! < 80 && pixels.data[at + 2]! < 80) {
          firstInk = { x, y };
          break;
        }
      }
    if (!firstInk) throw Error("Popup contains no rendered text");
    const screen = clientToScreen(popup, firstInk.x, firstInk.y);
    const hit = Number(sendMessage(popup, 0x84, 0, packCoords(screen.x, screen.y)));
    if (hit !== 1)
      failures.push(
        `Rendered text is consumed by resize hit-testing (hit=${hit}, pixel=${firstInk.x},${firstInk.y}, border=${border})`,
      );
    const activation = Number(sendMessage(popup, 0x21, app.frame, 0x02010001));
    if (activation !== 1) failures.push(`Unexpected mouse activation result ${activation}`);
    sendMessage(app.frame, 0x06, 0, popup);
    sendMessage(popup, 0x06, 2, app.frame);
    sendCommandSync(popup, cmdId("CmdSelectAll"));
    sendMessage(popup, 0x0301, 0, 0);
    if (!(await getClipboardText()).includes("Popup highlight target text."))
      failures.push("Popup selection/copy failed after activation");
    sendCommandSync(popup, cmdId("CmdCreateAnnotHighlight"));
    let annotations = "";
    for (let i = 0; i < 40; i++) {
      annotations = await markup(app.client);
      if (/type=Highlight page=2 quads=[1-9]/.test(annotations)) break;
      await Bun.sleep(30);
    }
    if (!/type=Highlight page=2 quads=[1-9]/.test(annotations))
      failures.push(`Popup Highlight action did not create destination-page markup: ${annotations}`);
    else {
      if (!/color=#99defa/i.test(annotations)) failures.push(`Highlight did not use configured color: ${annotations}`);
      sendCommandSync(app.frame, cmdId("CmdCopySelection"));
      if ((await getClipboardText()) !== originalSelection)
        failures.push("Highlight changed the main document selection");
      sendCommandSync(app.frame, cmdId("CmdUndo"));
      if (!/annotations=0/.test(await markup(app.client))) failures.push("One Undo did not remove popup highlight");
      sendCommandSync(app.frame, cmdId("CmdRedo"));
      if (!/type=Highlight page=2 quads=[1-9]/.test(await markup(app.client)))
        failures.push("Redo did not restore popup highlight");
      sendCommandSync(app.frame, cmdId("CmdSaveAnnotations"));
      await app.client.waitForRenderIdle();
      saved = true;
    }
  } finally {
    app.client.close();
    await killAndWait(app.proc);
  }
  if (saved) {
    const reopened = await launchControlled(["-appdata", dir, pdf]);
    try {
      await reopened.client.waitForRenderIdle();
      const state = await markup(reopened.client);
      if (!/type=Highlight page=2 quads=[1-9]/.test(state) || !/color=#99defa/i.test(state))
        failures.push(`Saved highlight did not survive reopen: ${state}`);
    } finally {
      reopened.client.close();
      await killAndWait(reopened.proc);
    }
  }
  if (failures.length) throw Error(failures.join("\n"));
  console.log(
    "Popup text hit-testing, activation, preserved main selection, color, Undo/Redo and save/reopen passed:",
    pdf,
  );
}
if (import.meta.main) await runStandalone(testit);
