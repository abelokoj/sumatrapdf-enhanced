import { ptr } from "bun:ffi";
import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { cmdId, pollUntil, runStandalone, writeAppdata } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync, waitForExit } from "./win-automation.ts";
import {
  enumWindows,
  enumChildWindows,
  getWindowPid,
  getWindowText,
  getClassName,
  isWindowVisible,
  sendMessage,
  postMessage,
} from "./winapi.ts";

function assert(value: unknown, message: string): asserts value {
  if (!value) throw new Error(`file-type-icons: ${message}`);
}
function item(hwnd: number, index: number): string {
  const text = new Uint16Array(256);
  const size = Number(sendMessage(hwnd, 0x148, index, ptr(text)));
  return size < 0 ? "" : String.fromCharCode(...text.subarray(0, size));
}
function find(pid: number, title: string): number {
  let found = 0;
  enumWindows((hwnd) => {
    if (getWindowPid(hwnd) === pid && isWindowVisible(hwnd) && getWindowText(hwnd) === title) {
      found = hwnd;
      return false;
    }
    return true;
  });
  return found;
}
function iconControl(window: number): number {
  sendMessage(window, 0x3d, 0, -25);
  let found = 0;
  enumChildWindows(window, (hwnd) => {
    if (getClassName(hwnd) === "ComboBox" && item(hwnd, 0) === "Outlined Pages (included, default)") {
      found = hwnd;
      return false;
    }
    return true;
  });
  return found;
}
async function settings(app: Awaited<ReturnType<typeof launchControlled>>) {
  sendCommandSync(app.frame, cmdId("CmdOptions"));
  const window = await pollUntil(() => find(app.proc.pid!, "Settings"), Boolean, { error: "Settings window missing" });
  const drop = await pollUntil(() => iconControl(window), Boolean, { error: "File-icon style dropdown missing" });
  return { window, drop };
}
function cachePack(dir: string, style: string) {
  const bytes = readFileSync(join("docs", "file-icons", "packs", style + ".sfeicons"));
  assert(bytes.subarray(0, 8).toString() === "SFEICON1", "Unexpected pack version");
  const root = join(dir, "file-icons", createHash("sha256").update(bytes).digest("hex"));
  mkdirSync(root, { recursive: true });
  let offset = 12;
  assert(bytes.readUInt32LE(8) === 39, "Pack must contain all 39 registered formats");
  for (let i = 0; i < 39; i++) {
    const nameSize = bytes.readUInt16LE(offset),
      iconSize = bytes.readUInt32LE(offset + 2);
    offset += 6;
    const name = bytes.subarray(offset, offset + nameSize).toString();
    assert(/^[a-z0-9]{1,8}$/.test(name), "Unsafe pack entry");
    offset += nameSize;
    writeFileSync(join(root, name + ".ico"), bytes.subarray(offset, offset + iconSize));
    offset += iconSize;
  }
  assert(offset === bytes.length, "Unexpected trailing pack data");
}
export async function testit() {
  for (const theme of ["Sumatra Light", "Sumatra Dark"]) {
    const dir = writeAppdata(
      "file-icons-" + theme.replaceAll(" ", "-"),
      `UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nReuseInstance = false\nTheme = ${theme}\n`,
    );
    let app = await launchControlled(["-appdata", dir], { saveSettings: true });
    try {
      let ui = await settings(app);
      assert(Number(sendMessage(ui.drop, 0x147, 0, 0)) === 0, "Outlined Pages must be the default");
      assert(Number(sendMessage(ui.drop, 0x146, 0, 0)) === 3, "All three styles must be selectable");
      sendMessage(ui.drop, 0x14e, 1, 0);
      postMessage(ui.window, 0x100, 13, 0);
      const prompt = await pollUntil(() => find(app.proc.pid!, "Download file icons"), Boolean, {
        error: "Download must require confirmation",
      });
      let body = "",
        no = 0;
      enumChildWindows(prompt, (hwnd) => {
        const text = getWindowText(hwnd);
        if (getClassName(hwnd) === "Static") body += text;
        if (text.replaceAll("&", "") === "No") no = hwnd;
        return true;
      });
      assert(
        body.includes("github.com/abelokoj/sumatrapdf-enhanced") &&
          body.includes("MiB") &&
          body.includes("offline") &&
          body.includes("default apps"),
        "Confirmation must explain source, size, storage, and unchanged defaults",
      );
      assert(no, "Confirmation must offer No");
      postMessage(no, 0xf5, 0, 0);
      await pollUntil(
        () => isWindowVisible(prompt),
        (v) => !v,
        { error: "Declining download did not close confirmation" },
      );
      assert(isWindowVisible(ui.window), "Declining download must leave Settings open");
      sendMessage(ui.window, 0x10, 0, 0);
      ui = await settings(app);
      assert(Number(sendMessage(ui.drop, 0x147, 0, 0)) === 0, "Cancel must preserve the previous style");
      sendMessage(ui.window, 0x10, 0, 0);
      cachePack(dir, "folded-pages");
      cachePack(dir, "color-covers");
      for (const index of [1, 2, 0, 2]) {
        ui = await settings(app);
        sendMessage(ui.drop, 0x14e, index, 0);
        postMessage(ui.window, 0x100, 13, 0);
        await pollUntil(
          () => isWindowVisible(ui.window),
          (v) => !v,
          { error: "Cached icon style did not apply" },
        );
        assert(!find(app.proc.pid!, "Download file icons"), "Cached styles must not download again");
        ui = await settings(app);
        assert(Number(sendMessage(ui.drop, 0x147, 0, 0)) === index, "Applied style did not persist on reopening");
        sendMessage(ui.window, 0x10, 0, 0);
      }
      postMessage(app.frame, 0x10, 0, 0);
      assert(await waitForExit(app.proc), "App did not close");
      app.client.close();
      assert(
        /FileTypeIconStyle = 2/.test(readFileSync(join(dir, "SumatraPDFEnhanced-settings.txt"), "utf8")),
        "Style was not saved",
      );
      app = await launchControlled(["-appdata", dir], { saveSettings: true });
      ui = await settings(app);
      assert(Number(sendMessage(ui.drop, 0x147, 0, 0)) === 2, "Style did not survive restart");
      sendMessage(ui.window, 0x10, 0, 0);
      console.log(`${theme}: default, download confirmation/decline, cached switches, Cancel, and restart passed`);
    } finally {
      app.client.close();
      await killAndWait(app.proc);
    }
  }
}
if (import.meta.main) await runStandalone(testit);
