import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { cmdId, makePdf, pollUntil, runStandalone, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync, waitForExit } from "./win-automation.ts";

function check(value: unknown, message: string): asserts value {
  if (!value) throw new Error(`settings-transfer: ${message}`);
}

export async function testit() {
  const base = tmpPath("settings-transfer");
  mkdirSync(base, { recursive: true });
  const document = join(base, "reading.pdf");
  writeFileSync(document, makePdf(1, 612, 0, 0), "latin1");
  const settings = join(base, "SumatraPDFEnhanced-settings.txt");
  writeFileSync(
    settings,
    `UiLanguage = en\nReuseInstance = false\nRestoreSession = false\nCheckForUpdates = false\nUIFontSize = 14\nHomePageFixedHeader = false\nLiveAppearancePreview = false\nPinIconStyle = 10\nFileStates [\n [\n FilePath = ${document}\n Completed = true\n IsPinned = true\n OpenCount = 2\n ]\n]\nFuturePreference = retained\n`,
  );
  let app = await launchControlled(["-appdata", base], { saveSettings: true });
  try {
    const exported = join(base, "export.txt");
    let result = await app.client.request(ControlCommand.TestSettingsTransfer, ["export", exported]);
    check(result[0] === 0, `Export failed: ${JSON.stringify(result)}`);
    const data = readFileSync(exported, "utf8");
    for (const name of [
      "UIFontSize",
      "Annotations [",
      "ToolbarSize",
      "LiveAppearancePreview",
      "Completed = true",
      "IsPinned = true",
      "FuturePreference = retained",
    ])
      check(data.includes(name), `Export omitted ${name}`);
    const canonical = readFileSync(settings, "utf8");
    for (const [index, text] of [
      "plain unrelated text",
      "UIFontSize = broken\n",
      "UiLanguage = en\nAnnotations [\n",
      "ShowToolbar = perhaps\n",
      "UiLanguage = en\0binary",
    ].entries()) {
      const invalid = join(base, `invalid-${index}.txt`);
      writeFileSync(invalid, text);
      result = await app.client.request(ControlCommand.TestSettingsTransfer, ["import", invalid]);
      check(result[0] !== 0, `Invalid file ${index} was accepted`);
      check(readFileSync(settings, "utf8") === canonical, "Rejected import changed settings");
    }
    result = await app.client.request(ControlCommand.TestSettingsTransfer, ["export", settings]);
    check(result[0] !== 0, "Export overwrote the live settings file");
    const changed = data
      .replace(/(^\s*UIFontSize = ).*$/m, (_, prefix) => `${prefix}18`)
      .replace(/(^\s*HomePageFixedHeader = ).*$/m, "$1true")
      .replace(/(^\s*LiveAppearancePreview = ).*$/m, "$1true");
    check(changed !== data, "Import fixture did not change settings");
    const source = join(base, "changed.txt");
    writeFileSync(source, changed);
    result = await app.client.request(ControlCommand.TestSettingsTransfer, ["import", source]);
    check(result[0] === 0, `Import failed: ${JSON.stringify(result)}`);
    const backup = /^OK backup=(.*)$/.exec(String(result[1]))?.[1];
    check(backup && existsSync(backup), "Import did not return a backup");
    check(readFileSync(backup, "utf8") === canonical, "Backup does not contain the previous settings");
    const after = join(base, "after.txt");
    result = await app.client.request(ControlCommand.TestSettingsTransfer, ["export", after]);
    check(result[0] === 0, "Export after live import failed");
    const imported = readFileSync(after, "utf8");
    check(/^\s*UIFontSize = 18$/m.test(imported), "Imported UI font size did not apply");
    check(/^\s*HomePageFixedHeader = true$/m.test(imported), "Imported Home preference did not apply");
    check(/^\s*LiveAppearancePreview = true$/m.test(imported), "Advanced-only setting did not apply");
    check(imported.includes("Completed = true") && imported.includes("IsPinned = true"), "Import lost file metadata");
    check(imported.includes("FuturePreference = retained"), "Import lost a future preference");
    sendCommandSync(app.frame, cmdId("CmdExit"));
    check(await waitForExit(app.proc), "Imported app did not exit");
    app.client.close();
    app = await launchControlled(["-appdata", base], { saveSettings: true });
    result = await app.client.request(ControlCommand.TestSettingsTransfer, ["export", after]);
    check(
      result[0] === 0 && /^\s*UIFontSize = 18$/m.test(readFileSync(after, "utf8")),
      "Restart lost imported preferences",
    );
    result = await app.client.request(ControlCommand.TestSettingsTransfer, ["import", backup]);
    check(result[0] === 0, "Could not undo import using its backup");
    await pollUntil(
      () => readFileSync(settings, "utf8"),
      (s) => /^\s*UIFontSize = 14$/m.test(s),
      { error: "Backup restore did not apply" },
    );
    check(readFileSync(document, "latin1") === makePdf(1, 612, 0, 0), "Settings transfer changed a document");
    console.log(
      "Settings transfer: full export, invalid input rejection, automatic backup, live application, metadata, future preferences, restart, undo passed",
    );
  } finally {
    app.client.close();
    await killAndWait(app.proc);
  }
}

if (import.meta.main) await runStandalone(testit);
