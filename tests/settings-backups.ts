import { mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { cmdId, runStandalone, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync, waitForExit } from "./win-automation.ts";

function check(ok: unknown, message: string): asserts ok {
  if (!ok) throw new Error(`settings-backups: ${message}`);
}

export async function testit() {
  const base = tmpPath("settings-backups");
  const custom = join(base, "chosen-backup-folder");
  const original = join(base, "SumatraPDFEnhanced-settings.txt");
  mkdirSync(base, { recursive: true });
  writeFileSync(
    original,
    `UiLanguage = en\nReuseInstance = false\nRestoreSession = false\nCheckForUpdates = false\nUIFontSize = 14\nSettingsBackupMode = automatic\nSettingsBackupDirectory = ${custom}\n`,
  );
  let app = await launchControlled(["-appdata", base], { saveSettings: true });
  try {
    const action = async (name: string, path = "") => {
      const result = await app.client.request(ControlCommand.TestSettingsTransfer, [name, path]);
      check(result[0] === 0, `${name} failed: ${JSON.stringify(result)}`);
      return String(result[1]);
    };
    const backups = (directory: string, kind: string) =>
      readdirSync(directory).filter((f) => f.includes(`-${kind}-`) && f.endsWith(".txt"));
    await action("set", "HomePageFixedHeader=true");
    await action("flush-backups");
    check(backups(custom, "auto").length > 0, "Saved settings did not create an automatic backup in the chosen folder");
    let automatic = backups(custom, "auto");
    check(
      automatic.some((name) => readFileSync(join(custom, name), "utf8").includes("HomePageFixedHeader = true")),
      "Backup missed the saved preference",
    );
    const unchangedCount = automatic.length;
    await action("export", join(base, "unchanged.txt"));
    await action("flush-backups");
    check(backups(custom, "auto").length === unchangedCount, "Unchanged settings generated a duplicate backup");
    await action("set", "SettingsBackupMode=manual");
    await action("flush-backups");
    const manualStart = backups(custom, "auto").length;
    await action("set", "HomePageFixedHeader=false");
    await action("flush-backups");
    check(backups(custom, "auto").length === manualStart, "Manual mode created an automatic backup");
    await action("backup", custom);
    await action("flush-backups");
    const manual = backups(custom, "manual");
    check(manual.length === 1, "Manual backup was not created");
    const manualData = readFileSync(join(custom, manual[0]!), "utf8");
    check(
      manualData.includes("HomePageFixedHeader = false") && manualData.includes("SettingsBackupMode = manual"),
      "Manual copy did not contain current saved settings",
    );
    await action("set", "SettingsBackupMode=automatic");
    for (let i = 0; i < 23; i++) {
      await action("set", `HomePageFixedHeader=${i % 2 ? "true" : "false"}`);
      await action("flush-backups");
    }
    check(backups(custom, "auto").length === 20, "Automatic retention did not keep the latest 20 backups");
    check(backups(custom, "manual").length === 1, "Automatic retention removed a manual copy");
    await action("set", "SettingsBackupDirectory=");
    await action("set", "HomePageFixedHeader=true");
    await action("flush-backups");
    check(backups(join(base, "Backups"), "auto").length > 0, "Default backup folder was not used");
    await action("set", "HomePageFixedHeader=false");
    sendCommandSync(app.frame, cmdId("CmdExit"));
    check(await waitForExit(app.proc), "App did not exit after flushing backups");
    app.client.close();
    check(
      backups(join(base, "Backups"), "auto").some((name) =>
        readFileSync(join(base, "Backups", name), "utf8").includes("HomePageFixedHeader = false"),
      ),
      "Exit lost the final saved backup",
    );
    app = await launchControlled(["-appdata", base], { saveSettings: true });
    await action("flush-backups");
    check(readFileSync(original, "utf8").includes("SettingsBackupMode = automatic"), "Restart lost backup mode");
    console.log(
      "Settings backups: automatic changed saves, manual mode, chosen/default folders, no duplicate saves, 20-copy retention, manual preservation, exit and restart passed",
    );
  } finally {
    app.client.close();
    await killAndWait(app.proc);
  }
}

if (import.meta.main) await runStandalone(testit);
