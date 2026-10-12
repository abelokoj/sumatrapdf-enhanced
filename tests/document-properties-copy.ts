// Copying properties must work through the visible button and command route.
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand, type ControlClient } from "./control.ts";
import { cmdId, ROOT, runStandalone, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";
import {
  clientToScreen,
  enumWindows,
  findChildWindow,
  getClientRect,
  getClipboardText,
  getControlText,
  getWindowPid,
  getWindowRect,
  getWindowText,
  packCoords,
  sendMessage,
  MK_LBUTTON,
  WM_LBUTTONDOWN,
  WM_LBUTTONUP,
  WM_MOUSEMOVE,
} from "./winapi.ts";

function findProperties(pid: number): number {
  let found = 0;
  enumWindows((hwnd) => {
    if (getWindowPid(hwnd) === pid && getWindowText(hwnd) === "Document Properties") {
      found = hwnd;
      return false;
    }
    return true;
  });
  return found;
}

function normalized(text: string): string {
  return text.replace(/\s+/g, " ").trim();
}

async function expectFeedback(client: ControlClient, copied: boolean): Promise<void> {
  const deadline = Date.now() + 4000;
  let raw = "";
  while (Date.now() < deadline) {
    const response = await client.request(ControlCommand.TestDocumentProperties, ["buttons"]);
    raw = String(response[1] ?? "");
    if (raw.includes(`copied=${copied ? 1 : 0}`)) return;
    await Bun.sleep(50);
  }
  throw new Error(`Properties copy: missing ${copied ? "Copied! feedback" : "restored button label"}: ${raw}`);
}

async function checkCopy(theme: string, scale: number): Promise<void> {
  const dir = tmpPath(`document-properties-copy-${scale}`);
  mkdirSync(dir, { recursive: true });
  writeFileSync(
    join(dir, "SumatraPDFEnhanced-settings.txt"),
    `UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nTheme = ${theme}\nInterfaceScale = ${scale}\n`,
  );
  const pdf = join(ROOT, "tests", "xmp-properties-info.pdf");
  const { proc, client, frame } = await launchControlled(["-appdata", dir, pdf]);
  try {
    await client.waitForRenderIdle();
    sendCommandSync(frame, cmdId("CmdCopyLocationToClipboard"));
    const before = await getClipboardText();
    if (!before.includes("-page")) throw new Error("Properties copy: clipboard fixture not set");
    sendCommandSync(frame, cmdId("CmdProperties"));

    let properties = 0;
    let edit = 0;
    let text = "";
    const deadline = Date.now() + 10000;
    while (Date.now() < deadline) {
      properties = findProperties(proc.pid!);
      edit = properties ? findChildWindow(properties, "Edit") : 0;
      text = edit ? getControlText(edit) : "";
      if (text.includes("InfoTitle") && !text.includes("Getting font information")) break;
      await Bun.sleep(50);
    }
    if (!text.includes("InfoTitle") || text.includes("Getting font information")) {
      throw new Error("Properties copy: completed properties window not ready");
    }
    const clientRect = getClientRect(properties);
    const origin = clientToScreen(properties, 0, 0);
    const editRect = getWindowRect(edit);
    const footerTop = editRect.bottom - origin.y;
    const point = packCoords(
      clientRect.right - Math.round((60 * scale) / 100),
      Math.round((footerTop + clientRect.bottom) / 2),
    );
    sendMessage(properties, WM_MOUSEMOVE, 0, point);
    sendMessage(properties, WM_LBUTTONDOWN, MK_LBUTTON, point);
    sendMessage(properties, WM_LBUTTONUP, 0, point);
    const copied = await getClipboardText();
    if (normalized(copied) !== normalized(text)) {
      throw new Error(
        `Properties copy: button did not copy the displayed properties (${theme}, ${scale}%); changed=${copied !== before}`,
      );
    }

    await expectFeedback(client, true);
    await expectFeedback(client, false);
    sendCommandSync(frame, cmdId("CmdCopyLocationToClipboard"));
    sendCommandSync(properties, cmdId("CmdCopySelection"));
    if (normalized(await getClipboardText()) !== normalized(text)) {
      throw new Error("Properties copy: copy command did not copy all properties");
    }
    await expectFeedback(client, true);
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

export async function testit(): Promise<void> {
  await checkCopy("Sumatra Light", 100);
  await checkCopy("Sumatra Dark", 200);
}

if (import.meta.main) await runStandalone(testit);
