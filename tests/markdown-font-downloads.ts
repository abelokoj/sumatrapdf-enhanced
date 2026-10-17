import { createHash } from "node:crypto";
import { ptr } from "bun:ffi";
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { ControlCommand, withControlledSumatra, type ControlClient } from "./control.ts";
import { EXE, ROOT, cmdId, pollUntil, runStandalone, tmpPath } from "./util.ts";
import { sendCommandSync, waitForFrame } from "./win-automation.ts";
import {
  enumChildWindows,
  enumWindows,
  getClassName,
  getWindowPid,
  getWindowText,
  isWindowEnabled,
  isWindowVisible,
  sendMessage,
  postMessage,
  VK_RETURN,
  WM_KEYDOWN,
} from "./winapi.ts";

const REVISION = "3d7870d8a8cdcd891e352e08f5c4ecc3451680f0";
const CB_GETCOUNT = 0x0146;
const CB_GETLBTEXT = 0x0148;
const CB_SETCURSEL = 0x014e;
function assert(ok: unknown, message: string): asserts ok {
  if (!ok) throw new Error(`markdown-font-downloads: ${message}`);
}
function fontCombo(pid: number): { window: number; combo: number } {
  let result = { window: 0, combo: 0 };
  enumWindows((window) => {
    if (getWindowPid(window) !== pid || !isWindowVisible(window) || !getWindowText(window).includes("Settings"))
      return true;
    // Materialize lazy native fields through their normal accessibility entry point.
    sendMessage(window, 0x003d, 0, -25);
    enumChildWindows(window, (combo) => {
      if (getClassName(combo) !== "ComboBox") return true;
      const text = new Uint16Array(256);
      const count = Number(sendMessage(combo, CB_GETLBTEXT, 0, ptr(text)));
      if (String.fromCharCode(...text.subarray(0, count)) !== "Follow interface font") return true;
      const sectionCount = Number(sendMessage(combo, CB_GETLBTEXT, 1, ptr(text)));
      if (String.fromCharCode(...text.subarray(0, sectionCount)) !== "Downloadable fonts") return true;
      result = { window, combo };
      return false;
    });
    return !result.combo;
  });
  return result;
}
async function state(client: ControlClient): Promise<string> {
  const result = await client.request(ControlCommand.TestInvokeCommand, ["settings-font-state"]);
  assert(result[0] === 0, `font state unavailable: ${result[1]}`);
  return String(result[1] ?? "");
}
function loraAssets(): string[] {
  const source = join(ROOT, "src/fonts/markdown");
  const files = readdirSync(join(source, "lora/otf")).map((file) => `lora/otf/${file}`);
  files.push("lora/OFL.txt", "libertinus/LibertinusMath-Regular.otf", "libertinus/OFL.txt");
  return files;
}
function seedLora(profile: string): void {
  const source = join(ROOT, "src/fonts/markdown");
  const cache = join(profile, "markdown-fonts", REVISION);
  const files = loraAssets();
  for (const file of files) {
    const dest = join(cache, file);
    mkdirSync(dirname(dest), { recursive: true });
    copyFileSync(join(source, file), dest);
  }
}
async function chooseFont(pid: number, frame: number): Promise<{ window: number; combo: number }> {
  sendCommandSync(frame, cmdId("CmdOptions"));
  const controls = await pollUntil(
    () => fontCombo(pid),
    (result) => !!result.combo,
    { error: "font dropdown did not appear" },
  );
  sendMessage(controls.combo, CB_SETCURSEL, 2, 0);
  postMessage(controls.window, WM_KEYDOWN, VK_RETURN, 0);
  return controls;
}

export async function testit(): Promise<void> {
  const dir = tmpPath("markdown-font-downloads");
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  const privateExe = join(dir, "SumatraPDF.exe");
  copyFileSync(EXE, privateExe);
  const pdb = (process.env.SUMATRA_TEST_EXE || EXE).replace(/\.exe$/i, ".pdb");
  if (existsSync(pdb)) copyFileSync(pdb, join(dir, "SumatraPDF.pdb"));
  for (const file of readdirSync(dirname(EXE)).filter((file) => file.endsWith(".dll"))) {
    copyFileSync(join(dirname(EXE), file), join(dir, file));
  }
  const scenarios = process.env.SUMATRA_FONT_DOWNLOAD_NETWORK_ONLY === "1" ? [] : ["missing", "cached"];
  if (process.env.SUMATRA_FONT_DOWNLOAD_NETWORK === "1") scenarios.push("network", "cancel");
  for (const scenario of scenarios) {
    console.log(`Testing ${scenario} Markdown fonts`);
    const cached = scenario === "cached";
    const network = scenario === "network" || scenario === "cancel";
    writeFileSync(
      join(dir, "sumatrapdfrestrict.ini"),
      `[Policies]\nInternetAccess = ${network ? 1 : 0}\nDiskAccess = 1\nSavePreferences = 1\nRegistryAccess = 0\nPrinterAccess = 0\nCopySelection = 1\nFullscreenAccess = 1\n`,
    );
    const profile = join(dir, scenario);
    mkdirSync(profile);
    const prefs = join(profile, "SumatraPDFEnhanced-settings.txt");
    writeFileSync(
      prefs,
      "UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nReuseInstance = false\nMarkdownUI [\nFontName =\nMathFontName =\n]\n",
    );
    if (cached) seedLora(profile);
    await withControlledSumatra(
      privateExe,
      async (client, proc) => {
        const frame = await waitForFrame(proc.pid!);
        assert(frame, "app window was not created");
        const controls = await chooseFont(proc.pid!, frame);
        if (scenario === "cancel") {
          const downloading = await state(client);
          assert(/busy=1/.test(downloading), "font download finished before cancellation could be tested");
          assert(!isWindowEnabled(controls.combo), "font dropdown stayed enabled during download");
          sendMessage(controls.window, 0x0010, 0, 0);
          const canceled = await state(client);
          assert(/busy=0/.test(canceled) && /font=(?:\n|$)/.test(canceled), "Cancel applied the pending font settings");
          await pollUntil(() => existsSync(join(profile, "markdown-fonts", REVISION, "FONT-SOURCES.txt")), Boolean, {
            timeoutMs: 60_000,
            error: "abandoned font download did not finish caching",
          });
          const settled = await state(client);
          assert(/font=(?:\n|$)/.test(settled), "font download completion applied canceled settings");
          assert(!isWindowVisible(controls.window), "canceled Settings window reopened");
          assert(!readFileSync(prefs, "utf8").includes("FontName = Lora"), "Cancel changed the saved font settings");
          return;
        }
        const result = await pollUntil(
          () => state(client),
          (value) =>
            /busy=0/.test(value) &&
            (cached || network
              ? /font=Lora(?:\n|$)/.test(value) || /status=.*(?:Cannot|failed|disabled|Unknown)/.test(value)
              : /disabled in restricted mode/.test(value)),
          { timeoutMs: network ? 60_000 : 10_000, error: (value) => `font selection did not settle: ${value}` },
        );
        if (cached || network) {
          assert(/font=Lora(?:\n|$)/.test(result), `selected font did not apply: ${result}`);
          assert(!isWindowVisible(controls.window), "font did not apply and close Settings");
          if (network) {
            for (const file of loraAssets()) {
              const downloaded = readFileSync(join(profile, "markdown-fonts", REVISION, file));
              const original = readFileSync(join(ROOT, "src/fonts/markdown", file));
              const sha256 = (data: Buffer) => createHash("sha256").update(data).digest("hex");
              assert(sha256(downloaded) === sha256(original), `downloaded font asset is incorrect: ${file}`);
            }
            assert(
              existsSync(join(profile, "markdown-fonts", REVISION, "FONT-SOURCES.txt")),
              "downloaded fonts have no source/license notice",
            );
            assert(
              !existsSync(join(profile, "markdown-fonts", REVISION, "browser")),
              "selecting Lora downloaded unrelated NewCM assets",
            );
          }
        } else {
          assert(isWindowVisible(controls.window), "failed download closed Settings");
          assert(isWindowEnabled(controls.combo), "failed download left the font dropdown disabled");
          assert(/font=(?:\n|$)/.test(result), "failed download changed the current font");
          assert(!existsSync(join(profile, "markdown-fonts")), "restricted download created a font cache");
          assert(!readFileSync(prefs, "utf8").includes("FontName = Lora"), "failed download changed the saved font");
        }
        sendMessage(controls.window, 0x0010, 0, 0);
      },
      ["-appdata", profile],
      { env: { PATH: `${dirname(EXE)};${process.env.PATH ?? ""}` } },
    );
  }
  console.log(`markdown-font-downloads: ${scenarios.join(", ")} selection scenarios passed`);
}
if (import.meta.main) await runStandalone(testit);
