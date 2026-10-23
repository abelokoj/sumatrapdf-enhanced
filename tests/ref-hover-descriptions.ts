import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { assemblePdf, runStandalone, tmpPath, writeAppdata } from "./util.ts";
import { ControlCommand } from "./control.ts";
import { launchControlled, findCanvas, killAndWait } from "./win-automation.ts";
import {
  captureWindowPixels,
  clientToScreen,
  enumWindows,
  getClassName,
  getCursorPos,
  getWindowPid,
  isWindowVisible,
  packCoords,
  postMessage,
  sendMessage,
  setCursorPos,
  setProcessDpiAware,
  setWindowPos,
} from "./winapi.ts";

function fixture() {
  const source =
    "BT /F1 14 Tf 72 700 Td 0 0 0 rg (Black [107]) Tj 0 -50 Td (Black Equation 3.8) Tj 0 -50 Td 0 0 1 rg (Blue [107]) Tj 0 -50 Td 0 0 0 rg (Plain [6]) Tj ET";
  const bibliography =
    "BT /F1 12 Tf 72 740 Td ([107] Example paper reference, author and publication details.) Tj 0 -40 Td ([6] Another reference with author and publication details.) Tj ET";
  const equation =
    "BT /F1 12 Tf 72 740 Td (Equation destination with sufficient context for the preview.) Tj 0 -30 Td (x = y + z) Tj 300 0 Td (3.8) Tj ET";
  return assemblePdf([
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 6 0 R >> >> /Contents 7 0 R /Annots [10 0 R 11 0 R 12 0 R] >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 6 0 R >> >> /Contents 8 0 R >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 6 0 R >> >> /Contents 9 0 R >>",
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    `<< /Length ${source.length} >>\nstream\n${source}\nendstream`,
    `<< /Length ${bibliography.length} >>\nstream\n${bibliography}\nendstream`,
    `<< /Length ${equation.length} >>\nstream\n${equation}\nendstream`,
    "<< /Type /Annot /Subtype /Link /Rect [72 695 170 715] /Border [0 0 0] /Dest 13 0 R /Contents ([107]) >>",
    "<< /Type /Annot /Subtype /Link /Rect [72 645 220 665] /Border [0 0 0] /Dest 14 0 R /Contents (Equation 3.8) >>",
    "<< /Type /Annot /Subtype /Link /Rect [72 595 170 615] /Border [0 0 0] /Dest 13 0 R /Contents ([107]) >>",
    "[4 0 R /XYZ 72 740 0]",
    "[5 0 R /XYZ 72 740 0]",
  ]);
}
export async function testit(): Promise<void> {
  setProcessDpiAware();
  const dir = tmpPath(`ref-hover-descriptions-${process.pid}`);
  mkdirSync(dir, { recursive: true });
  const pdf = join(dir, "black-and-blue-references.pdf");
  writeFileSync(pdf, fixture(), "latin1");
  const old = getCursorPos(),
    failures: string[] = [];
  try {
    for (const theme of ["Sumatra Light", "Sumatra Dark"]) {
      const settings = writeAppdata(
        `ref-description-${process.pid}-${theme}`,
        `UiLanguage = en\nRestoreSession = false\nCheckForUpdates = false\nShowToc = false\nTheme = ${theme}\nCitationHoverDelay = 100\n`,
      );
      const app = await launchControlled(["-appdata", settings, "-view", "single page", "-zoom", "fit page", pdf]);
      try {
        await app.client.waitForRenderIdle();
        await app.client.setNotificationsEnabled(false);
        const canvas = findCanvas(app.frame),
          pixels = captureWindowPixels(canvas);
        if (!pixels) throw Error("Missing page capture");
        let l = pixels.w,
          r = 0,
          t = pixels.h,
          b = 0;
        for (let y = 0; y < pixels.h; y++)
          for (let x = 0; x < pixels.w; x++) {
            const i = (y * pixels.w + x) * 4;
            if (pixels.data[i]! > 250 && pixels.data[i + 1]! > 250 && pixels.data[i + 2]! > 250) {
              l = Math.min(l, x);
              r = Math.max(r, x);
              t = Math.min(t, y);
              b = Math.max(b, y);
            }
          }
        setWindowPos(app.frame, 0, 0, 0, 0, 0x13, -1);
        let previous = 0;
        for (const [name, px, py, target] of [
          ["black paper", 123, 87, 2],
          ["black equation", 130, 137, 3],
          ["blue paper", 123, 187, 2],
          ["unlinked black paper", 120, 237, 2],
        ] as const) {
          if (previous) sendMessage(previous, 0x100, 27, 0);
          const x = Math.round(l + (px * (r - l + 1)) / 612),
            y = Math.round(t + (py * (b - t + 1)) / 792),
            screen = clientToScreen(canvas, x, y);
          if (!setCursorPos(screen.x, screen.y))
            throw Error("Reference descriptions test requires an interactive desktop");
          await app.client.request(ControlCommand.TestRefHover, ["hide"]);
          sendMessage(canvas, 0x20, canvas, 0x02000001);
          if (name === "unlinked black paper") postMessage(canvas, 0x200, 0, packCoords(x, y));
          let raw = "",
            popup = 0;
          const end = Date.now() + 10000;
          while (Date.now() < end) {
            raw = String((await app.client.request(ControlCommand.TestRefHover, []))[1]);
            const m = /visible=1 hwnd=(-?\d+)/.exec(raw);
            if (m) {
              popup = +m[1]!;
              break;
            }
            await Bun.sleep(30);
          }
          if (!popup || !raw.includes(`page=${target} `))
            failures.push(`${theme} ${name}: wrong/missing preview: ${raw}`);
          previous = popup;
          if (!raw.includes("tip=0"))
            failures.push(`${theme} ${name}: competing one-line tooltip still registered: ${raw}`);
        }
      } finally {
        app.client.close();
        await killAndWait(app.proc);
      }
    }
  } finally {
    setCursorPos(old.x, old.y);
  }
  if (failures.length) throw Error(failures.join("\n"));
  console.log(
    "Black paper/equation, blue paper, and unlinked black references passed without competing tooltips in both themes",
  );
}
if (import.meta.main) await runStandalone(testit);
