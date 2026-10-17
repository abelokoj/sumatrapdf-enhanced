import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { assemblePdf, tmpPath } from "./util.ts";

type Page = { id: string; lines: string[]; annotations?: string[]; offset?: number };

const FIRST = [
  "Revision transfer overview",
  "The northern observatory measures stellar motion every clear evening.",
  "Record the corrected orbit before comparing the annual measurements.",
  "This highlighted sentence follows the revised observation chapter.",
  "Calibration establishes a stable reference for subsequent observations.",
  "Review the telescope alignment and retain the historical measurement notes.",
];
const SECOND = [
  "Instrument drawing and geometry",
  "The east laboratory contains a copper chamber and a glass inspection window.",
  "The rectangular boundary identifies the inspection region beside the chamber.",
  "An ink stroke records the technician's original hand drawn alignment mark.",
  "The blue translucent fill should preserve its appearance after the transfer.",
  "Maintain the red border thickness and the timestamp for this laboratory note.",
];
const THIRD = [
  "Final conclusions and maintenance",
  "The southern workshop verifies the mechanical mounting at the end of the trial.",
  "Replace the worn coupling before making the final temperature comparison.",
  "A separate comment explains the remaining maintenance recommendations.",
  "These conclusions appear before the measurement chapter in the revised edition.",
];
const COVER = [
  "New edition cover",
  "This cover page was inserted into the revised document.",
  "It has no counterpart in the annotated original and must receive no annotations.",
];

// Fixed PDF positions let the test distinguish page matching from page-number copying.
function first(annotations = false, offset = 0): Page {
  return {
    id: "observations",
    lines: FIRST,
    offset,
    annotations: annotations
      ? [
          "<< /Type /Annot /Subtype /Highlight /Rect [72 637 493 655] /QuadPoints [72 655 493 655 72 637 493 637] /C [1 1 0] /CA 0.65 /Contents (Preserve this highlight) /T (Transfer author) /NM (source-highlight) /M (D:20261012120000Z) /F 4 >>",
          "<< /Type /Annot /Subtype /Text /Rect [510 650 534 674] /C [0 1 0] /Contents (Observation comment) /T (Transfer author) /NM (source-note) /F 4 >>",
        ]
      : [],
  };
}
function second(annotations = false): Page {
  return {
    id: "instruments",
    lines: SECOND,
    annotations: annotations
      ? [
          "<< /Type /Annot /Subtype /Ink /Rect [100 390 230 440] /InkList [[102 400 140 432 185 405 225 425]] /C [0.1 0.2 0.8] /BS << /W 2.5 >> /CA 0.8 /Contents (Hand drawn alignment) /NM (source-ink) /F 4 >>",
          "<< /Type /Annot /Subtype /Square /Rect [300 360 440 450] /C [1 0 0] /IC [0 0 1] /BS << /W 3 >> /CA 0.4392157 /Contents (Blue filled rectangle) /NM (source-square) /F 4 >>",
        ]
      : [],
  };
}
function third(annotations = false): Page {
  return {
    id: "conclusions",
    lines: THIRD,
    annotations: annotations
      ? [
          "<< /Type /Annot /Subtype /Text /Rect [450 500 474 524] /C [1 0.5 0] /Contents (Maintenance comment) /NM (source-conclusion) /F 4 >>",
        ]
      : [],
  };
}

function escapePdf(text: string): string {
  return text.replace(/[\\()]/g, "\\$&");
}
function document(pages: Page[]): string {
  const objects = ["", "", "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"];
  const kids: number[] = [];
  for (const page of pages) {
    const pageId = objects.length + 1;
    const streamId = pageId + 1;
    objects.push("", "");
    kids.push(pageId);
    const annots: number[] = [];
    for (const annotation of page.annotations ?? []) {
      annots.push(objects.length + 1);
      objects.push(annotation.replace("/Subtype", `/P ${pageId} 0 R /Subtype`));
    }
    const text = page.lines
      .map((line, index) => `BT /F1 12 Tf 72 ${720 - index * 24 - (page.offset ?? 0)} Td (${escapePdf(line)}) Tj ET`)
      .join("\n");
    objects[streamId - 1] = `<< /Length ${Buffer.byteLength(text, "latin1")} >>\nstream\n${text}\nendstream`;
    objects[pageId - 1] =
      `<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R >> >> /Contents ${streamId} 0 R` +
      (annots.length ? ` /Annots [${annots.map((id) => `${id} 0 R`).join(" ")}]` : "") +
      " >>";
  }
  objects[0] = "<< /Type /Catalog /Pages 2 0 R >>";
  objects[1] = `<< /Type /Pages /Count ${pages.length} /Kids [${kids.map((id) => `${id} 0 R`).join(" ")}] >>`;
  return assemblePdf(objects);
}

export function writeTransferFixtures(dir: string): Record<string, string> {
  mkdirSync(dir, { recursive: true });
  const definitions: Record<string, Page[]> = {
    old: [first(true), second(true), third(true)],
    revised: [{ id: "cover", lines: COVER }, third(), first(), second()],
    shifted: [first(false, 72), second(), third()],
    ambiguous: [first(), first(), second(), third()],
    unrelated: [{ id: "unrelated", lines: ["A completely different publication about tropical gardening."] }],
    blank: [{ id: "blank", lines: [] }],
    preannotated: [{ id: "cover", lines: COVER }, third(), first(), second(true)],
  };
  const files: Record<string, string> = {};
  for (const [name, pages] of Object.entries(definitions)) {
    const path = join(dir, `${name}.pdf`);
    writeFileSync(path, document(pages), "latin1");
    files[name] = path;
  }
  return files;
}

// Regenerate inspectable fixtures: bun tests/annotation-transfer-data.ts.
if (import.meta.main) {
  console.log(writeTransferFixtures(tmpPath("annotation-transfer-fixtures")));
}
