import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { assemblePdf, cmdId, runStandalone, tmpPath, writeAppdata } from "./util.ts";
import { ControlCommand } from "./control.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";
import {
  captureWindowPixels,
  captureWindowToPng,
  clientToScreen,
  getClientRect,
  getCursorPos,
  packCoords,
  postMessage,
  sendMessage,
  setProcessDpiAware,
  setCursorPos,
  setForegroundWindow,
  setWindowPos,
  windowFromPoint,
} from "./winapi.ts";

function fixture() {
  const text = "BT /F1 14 Tf 72 740 Td (Reference preview text) Tj ET";
  return assemblePdf([
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots [7 0 R] >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> /Contents 6 0 R >>",
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    `<< /Length ${text.length} >>\nstream\n${text}\nendstream`,
    "<< /Type /Annot /Subtype /Link /Rect [0 0 612 792] /Border [0 0 0] /Dest [4 0 R /XYZ 72 780 1] >>",
  ]);
}
export async function testit(): Promise<void> {
  setProcessDpiAware();
  const dir = tmpPath(`ref-hover-live-${process.pid}`);
  mkdirSync(dir, { recursive: true });
  const pdf = join(dir, "reference.pdf");
  writeFileSync(pdf, fixture(), "latin1");
  const failures: string[] = [];
  const oldCursor = getCursorPos();
  try {
    for (const theme of ["Sumatra Light", "Sumatra Dark"]) {
      const settings = writeAppdata(
        `ref-hover-live-${process.pid}-${theme}`,
        `UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nShowToc = false\nTheme = ${theme}\nCitationHoverDelay = 100\n`,
      );
      const app = await launchControlled(["-appdata", settings, "-view", "single page", "-zoom", "fit page", pdf]);
      try {
        await app.client.waitForRenderIdle();
        await app.client.setNotificationsEnabled(false);
        const canvas = findCanvas(app.frame);
        const rect = getClientRect(canvas);
        const x = Math.floor(rect.right / 2),
          y = Math.floor(rect.bottom / 3);
        const screen = clientToScreen(canvas, x, y);
        setWindowPos(app.frame, 0, 0, 0, 0, 0x0013, -1);
        setForegroundWindow(app.frame);
        if (!setCursorPos(screen.x, screen.y)) throw new Error("ref-hover-live requires an interactive desktop");
        let previousPopup = 0;
        for (const hand of [false, true]) {
          if (hand) {
            sendCommandSync(app.frame, cmdId("CmdHandTool"));
            if (previousPopup) sendMessage(previousPopup, 0x0100, 27, 0);
          }
          let appearedWhileMoving = false;
          for (let step = 0; step < 30; step++) {
            const moving = clientToScreen(canvas, x + (step % 2), y);
            setCursorPos(moving.x, moving.y);
            postMessage(canvas, 0x0200, 0, packCoords(x + (step % 2), y));
            await Bun.sleep(40);
            const state = String((await app.client.request(ControlCommand.TestRefHover, []))[1]);
            if (state.includes("visible=1")) {
              appearedWhileMoving = true;
              break;
            }
          }
          if (!appearedWhileMoving)
            failures.push(`${theme}: movement within one reference keeps postponing its preview`);
          let raw = "",
            popup = 0;
          const deadline = Date.now() + 10000;
          while (Date.now() < deadline) {
            const response = await app.client.request(ControlCommand.TestRefHover, []);
            raw = String(response[1]);
            const found = /visible=1 hwnd=(-?\d+)/.exec(raw);
            if (found) {
              popup = Number(found[1]);
              break;
            }
            await Bun.sleep(30);
          }
          if (!popup) {
            failures.push(
              `${theme} ${hand ? "hand" : "reading"}: actual mouse hover did not open preview (${raw}); pointerWindow=${windowFromPoint(screen.x, screen.y)} canvas=${canvas}`,
            );
            continue;
          }
          const gen = /gen=(\d+)/.exec(raw)?.[1];
          const kept = await app.client.request(ControlCommand.TestRefHover, ["move-null", x, y]);
          if (!gen || !String(kept[1]).includes(`gen=${gen} `))
            failures.push(`${theme}: temporary hit-test miss cancelled the reference`);
          previousPopup = popup;
          if (!raw.includes("page=2 ")) failures.push(`${theme}: wrong reference destination: ${raw}`);
          await Bun.sleep(100);
          const pixels = captureWindowPixels(popup);
          if (!pixels) throw new Error("Cannot capture reference preview");
          let red = 0;
          for (let py = 0; py < pixels.h; py++) {
            for (let px = 0; px < pixels.w; px++) {
              if (px >= 4 && py >= 4 && px < pixels.w - 4 && py < pixels.h - 4) continue;
              const at = (py * pixels.w + px) * 4;
              if (pixels.data[at + 2]! > 180 && pixels.data[at + 1]! < 120 && pixels.data[at]! < 130) red++;
            }
          }
          if (red < pixels.w / 2) failures.push(`${theme}: thin red preview border missing (${red} red edge pixels)`);
          const capture = process.env.SUMATRA_REF_HOVER_CAPTURES;
          if (capture && !hand) {
            mkdirSync(capture, { recursive: true });
            await captureWindowToPng(popup, join(capture, theme.endsWith("Dark") ? "dark.png" : "light.png"));
          }
          const outside = await app.client.request(ControlCommand.TestRefHover, ["move-null", -10, -10]);
          if (!String(outside[1]).includes("pending=-1"))
            failures.push(`${theme}: leaving source did not cancel the reference`);
          postMessage(popup, 0x0100, 27, 0);
          await Bun.sleep(50);
        }
      } finally {
        app.client.close();
        await killAndWait(app.proc);
      }
    }
  } finally {
    setCursorPos(oldCursor.x, oldCursor.y);
  }
  if (failures.length) throw new Error(failures.join("\n"));
  console.log("Actual mouse hover in reading/hand tools and red borders passed in both themes");
}
if (import.meta.main) await runStandalone(testit);
