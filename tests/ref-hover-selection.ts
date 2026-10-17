// Reference previews support text selection and copy without selecting the main PDF.
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { assemblePdf, cmdId, runStandalone, tmpPath } from "./util.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";
import {
  getClientRect,
  getWindowRect,
  captureWindowToPng,
  getClipboardText,
  sendMessage,
  packCoords,
  MK_LBUTTON,
  MK_SHIFT,
  WM_LBUTTONDOWN,
  WM_LBUTTONUP,
  WM_LBUTTONDBLCLK,
  WM_MOUSEMOVE,
  WM_KEYDOWN,
  VK_ESCAPE,
} from "./winapi.ts";

const WM_COPY = 0x0301;
const WM_MOUSEWHEEL = 0x020a;
const MK_CONTROL = 0x0008;
const WHEEL_DELTA = 120;

function fixture(): string {
  const text = "BT /F1 14 Tf 72 740 Td (Reference preview selectable text) Tj 0 -24 Td (Second reference line) Tj ET";
  const source = "BT /F1 24 Tf 72 700 Td (See reference) Tj ET";
  return assemblePdf([
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> /Contents 8 0 R /Annots [9 0 R 7 0 R] >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> /Contents 6 0 R >>",
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    `<< /Length ${text.length} >>\nstream\n${text}\nendstream`,
    "<< /Type /Annot /Subtype /Link /Rect [0 0 612 792] /Border [0 0 0] /Dest [4 0 R /XYZ 0 792 1] >>",
    `<< /Length ${source.length} >>\nstream\n${source}\nendstream`,
    "<< /Type /Annot /Subtype /Link /Rect [306 0 612 792] /Border [0 0 0] /Dest [4 0 R /XYZ 72 716 1] >>",
  ]);
}

export async function testit(): Promise<void> {
  const dir = tmpPath("ref-hover-selection");
  mkdirSync(dir, { recursive: true });
  const pdf = join(dir, "reference.pdf");
  writeFileSync(pdf, fixture(), "latin1");
  writeFileSync(
    join(dir, "SumatraPDFEnhanced-settings.txt"),
    "UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nCitationHoverDelay = 100\n",
  );
  const { proc, client, frame } = await launchControlled(["-appdata", dir, pdf]);
  try {
    await client.waitForRenderIdle();
    await client.setNotificationsEnabled(false);
    sendCommandSync(frame, cmdId("CmdCopyLocationToClipboard"));
    const canvas = findCanvas(frame);
    const size = getClientRect(canvas);
    const shown = await client.request(ControlCommand.TestRefHover, [
      "show",
      ((size.right / 2) | 0) - 40,
      (size.bottom / 3) | 0,
    ]);
    if (shown[0] !== 0) throw new Error(`Reference selection: show failed: ${shown[1]}`);
    let popup = 0;
    const deadline = Date.now() + 10000;
    while (Date.now() < deadline) {
      const response = await client.request(ControlCommand.TestRefHover, []);
      const found = /visible=1 hwnd=(-?\d+)/.exec(String(response[1] ?? ""));
      if (found) {
        popup = Number(found[1]);
        break;
      }
      await Bun.sleep(50);
    }
    if (!popup) throw new Error("Reference selection: preview did not open");
    sendCommandSync(popup, cmdId("CmdSelectAll"));
    sendMessage(popup, WM_COPY, 0, 0);
    const copied = await getClipboardText();
    if (!copied.includes("Reference preview selectable text") || !copied.includes("Second reference line")) {
      throw new Error("Reference selection: preview did not copy its text");
    }
    const state = await client.request(ControlCommand.TestRefHover, []);
    const raw = String(state[1] ?? "");
    const mapped = /region=([\d.-]+),([\d.-]+),([\d.-]+),([\d.-]+) scale=([\d.-]+)/.exec(raw);
    if (!mapped) throw new Error(`Reference selection: missing preview geometry: ${raw}`);
    const regionX = Number(mapped[1]),
      regionY = Number(mapped[2]),
      zoom = Number(mapped[5]);
    const borderMatch = /border=(\d+)/.exec(raw);
    if (!borderMatch) throw new Error("Reference selection: missing preview border");
    const border = Number(borderMatch[1]);
    const point = (x: number, y: number) =>
      packCoords(Math.round(border + (x - regionX) * zoom), Math.round(border + (y - regionY) * zoom));
    sendMessage(popup, WM_LBUTTONDOWN, MK_LBUTTON, point(72, 46));
    const afterDown = await client.request(ControlCommand.TestRefHover, []);
    if (!String(afterDown[1] ?? "").includes("visible=1"))
      throw new Error("Reference selection: activating the preview dismissed it");
    const frozenWindow = getWindowRect(popup);
    await client.request(ControlCommand.TestRefHover, ["show", ((size.right / 2) | 0) + 40, (size.bottom / 3) | 0]);
    await Bun.sleep(100);
    for (let i = 0; i < 40; i++) {
      sendMessage(popup, WM_MOUSEMOVE, MK_LBUTTON, point(i % 2 ? 350 : 400, 46));
    }
    sendMessage(popup, WM_MOUSEMOVE, MK_LBUTTON, point(400, 46));
    if (JSON.stringify(getWindowRect(popup)) !== JSON.stringify(frozenWindow))
      throw new Error("Reference selection: preview moved during selection");
    sendMessage(popup, WM_LBUTTONUP, 0, point(400, 46));
    sendMessage(popup, WM_COPY, 0, 0);
    const first = (await getClipboardText()).trim();
    if (first !== "Reference preview selectable text")
      throw new Error(
        `Reference selection: drag copied ${JSON.stringify(first)}; ${raw}; client=${JSON.stringify(getClientRect(popup))}; afterDown=${afterDown[1]}`,
      );

    captureWindowToPng(popup, join(dir, "selected-preview.png"));

    sendMessage(popup, WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT, point(220, 70));
    sendMessage(popup, WM_LBUTTONUP, MK_SHIFT, point(220, 70));
    sendCommandSync(popup, cmdId("CmdCopySelection"));
    if (
      (await getClipboardText()).replace(/\s+/g, " ").trim() !==
      "Reference preview selectable text Second reference line"
    ) {
      throw new Error(`Reference selection: Shift-click copied ${JSON.stringify(await getClipboardText())}`);
    }

    sendMessage(popup, WM_LBUTTONDBLCLK, MK_LBUTTON, point(100, 46));
    sendMessage(popup, WM_LBUTTONUP, 0, point(100, 46));
    sendMessage(popup, WM_COPY, 0, 0);
    if ((await getClipboardText()).trim() !== "Reference")
      throw new Error("Reference selection: double-click did not select one word");
    const stableRect = getWindowRect(popup);
    const beforeWheel = await client.request(ControlCommand.TestRefHover, []);
    const beforeY = Number(/ y=(-?\d+)/.exec(String(beforeWheel[1]))?.[1]);
    const beforeZoom = Number(/ zoom=(\d+)/.exec(String(beforeWheel[1]))?.[1]);
    sendMessage(popup, WM_MOUSEWHEEL, (-WHEEL_DELTA & 0xffff) * 0x10000, 0);
    const scrolled = await client.request(ControlCommand.TestRefHover, []);
    if (Number(/ y=(-?\d+)/.exec(String(scrolled[1]))?.[1]) <= beforeY)
      throw new Error(`Reference selection: wheel did not scroll: ${beforeWheel[1]} -> ${scrolled[1]}`);
    sendMessage(popup, WM_MOUSEWHEEL, (WHEEL_DELTA * 0x10000) | MK_CONTROL, 0);
    const zoomed = await client.request(ControlCommand.TestRefHover, []);
    if (Number(/ zoom=(\d+)/.exec(String(zoomed[1]))?.[1]) <= beforeZoom)
      throw new Error("Reference selection: Ctrl+wheel did not zoom");
    if (JSON.stringify(getWindowRect(popup)) !== JSON.stringify(stableRect))
      throw new Error("Reference selection: wheel changed popup placement");
    sendMessage(popup, WM_COPY, 0, 0);
    if ((await getClipboardText()).trim() !== "Reference")
      throw new Error("Reference selection: wheel lost the selected text");
    sendMessage(popup, WM_KEYDOWN, VK_ESCAPE, 0);
    const hidden = await client.request(ControlCommand.TestRefHover, []);
    if (!String(hidden[1] ?? "").includes("visible=0"))
      throw new Error("Reference selection: Escape did not dismiss the preview");
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

if (import.meta.main) await runStandalone(testit);
