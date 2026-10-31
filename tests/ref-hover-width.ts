import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { assemblePdf, runStandalone, tmpPath, writeAppdata } from "./util.ts";
import { ControlCommand } from "./control.ts";
import { findCanvas, killAndWait, launchControlled } from "./win-automation.ts";
import {
  captureWindowPixels,
  getWindowRect,
  packCoords,
  sendMessage,
  setProcessDpiAware,
  setWindowPos,
} from "./winapi.ts";

function fixture(linkZoom: number) {
  const source = "1 1 1 rg 0 0 612 792 re f";
  const target =
    "BT /F1 14 Tf 72 740 Td (A wide reference destination whose initial popup must fit the source page.) Tj ET";
  return assemblePdf([
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 8 0 R /Annots [7 0 R] >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 2000 792] /Resources << /Font << /F1 5 0 R >> >> /Contents 6 0 R >>",
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    `<< /Length ${target.length} >>\nstream\n${target}\nendstream`,
    `<< /Type /Annot /Subtype /Link /Rect [0 0 612 792] /Border [0 0 0] /Dest [4 0 R /XYZ 0 792 ${linkZoom}] >>`,
    `<< /Length ${source.length} >>\nstream\n${source}\nendstream`,
  ]);
}

export async function testit(): Promise<void> {
  setProcessDpiAware();
  const dir = tmpPath(`ref-hover-width-${process.pid}`);
  mkdirSync(dir, { recursive: true });
  const failures: string[] = [];
  for (const theme of ["Sumatra Light", "Sumatra Dark"]) {
    for (const [zoom, linkZoom] of [
      [10, 0],
      [50, 1],
      [100, 1],
    ] as const) {
      const pdf = join(dir, `reference-width-${zoom}.pdf`);
      writeFileSync(pdf, fixture(linkZoom), "latin1");
      const settings = writeAppdata(
        `ref-width-${process.pid}-${zoom}-${theme}`,
        `UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nShowToc = false\nMainWindowBackground = #808080\nTheme = ${theme}\nDocumentColorsFollowTheme = off\nCitationHoverDelay = 100\n`,
      );
      const app = await launchControlled(["-appdata", settings, "-view", "single page", "-zoom", String(zoom), pdf]);
      try {
        await app.client.waitForRenderIdle();
        await app.client.setNotificationsEnabled(false);
        const canvas = findCanvas(app.frame);
        const pixels = captureWindowPixels(canvas);
        if (!pixels) throw new Error("Reference width: canvas capture failed");
        let left = pixels.w,
          right = -1,
          top = pixels.h,
          bottom = -1;
        for (let y = 0; y < pixels.h; y++)
          for (let x = 0; x < pixels.w; x++) {
            const at = (y * pixels.w + x) * 4;
            if (pixels.data[at]! === 255 && pixels.data[at + 1]! === 255 && pixels.data[at + 2]! === 255) {
              left = Math.min(left, x);
              right = Math.max(right, x);
              top = Math.min(top, y);
              bottom = Math.max(bottom, y);
            }
          }
        const pageWidth = right - left + 1;
        if (pageWidth <= 0) throw new Error(`Reference width: missing white source page (${pageWidth})`);
        const shown = await app.client.request(ControlCommand.TestRefHover, [
          "show",
          Math.round((left + right) / 2),
          Math.round((top + bottom) / 2),
        ]);
        if (shown[0] !== 0) throw new Error(`Reference width: no source link (${shown[1]})`);
        let popup = 0;
        const deadline = Date.now() + 10000;
        while (Date.now() < deadline) {
          const state = String((await app.client.request(ControlCommand.TestRefHover, []))[1]);
          const found = /visible=1 hwnd=(-?\d+)/.exec(state);
          if (found) {
            popup = Number(found[1]);
            break;
          }
          await Bun.sleep(30);
        }
        if (!popup) throw new Error("Reference width: popup did not open");
        const initial = getWindowRect(popup);
        const initialWidth = initial.right - initial.left;
        const cap = Math.floor((pageWidth * 95) / 100);
        console.log(`${theme} zoom=${zoom}: page=${pageWidth}, cap=${cap}, popup=${initialWidth}`);
        if (initialWidth > cap)
          failures.push(
            `${theme} ${zoom}%: initial popup ${initialWidth}px exceeds 95% of source page ${pageWidth}px (${cap}px)`,
          );
        sendMessage(popup, 0x0231, 0, 0);
        const desiredWidth = Math.max(initialWidth + 160, cap + 160);
        setWindowPos(
          popup,
          initial.left - 40,
          initial.top - 30,
          desiredWidth,
          initial.bottom - initial.top + 80,
          0x0014,
        );
        sendMessage(popup, 0x0232, 0, 0);
        const expanded = getWindowRect(popup);
        if (expanded.right - expanded.left <= cap)
          failures.push(`${theme} ${zoom}%: manual enlargement was constrained to the initial-width cap`);
        const middleX = Math.round((expanded.left + expanded.right) / 2),
          middleY = Math.round((expanded.top + expanded.bottom) / 2);
        for (const [name, x, y, hit] of [
          ["left", expanded.left + 1, middleY, 10],
          ["right", expanded.right - 2, middleY, 11],
          ["top", middleX, expanded.top + 1, 12],
          ["bottom", middleX, expanded.bottom - 2, 15],
        ] as const) {
          const actual = Number(sendMessage(popup, 0x0084, 0, packCoords(x, y)));
          if (actual !== hit)
            failures.push(`${theme} ${zoom}%: ${name} resize edge returned ${actual}, expected ${hit}`);
        }
      } finally {
        app.client.close();
        await killAndWait(app.proc);
      }
    }
  }
  if (failures.length) throw new Error(failures.join("\n"));
  console.log("Reference popup default 95% page-width cap and unrestricted four-edge resizing passed");
}
if (import.meta.main) await runStandalone(testit);
