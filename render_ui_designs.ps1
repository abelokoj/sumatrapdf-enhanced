Add-Type -AssemblyName System.Drawing

$artifactDir = "C:\Users\abelo\.gemini\antigravity\brain\bc13cb30-3bca-4de0-9d75-71372ae4e772"

function Draw-RoundBox($g, $pen, $brush, $x, $y, $w, $h, $radius) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $radius * 2
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    if ($brush -ne $null) { $g.FillPath($brush, $path) }
    if ($pen -ne $null) { $g.DrawPath($pen, $path) }
    $path.Dispose()
}

$fontHead = New-Object System.Drawing.Font("Georgia", 22, [System.Drawing.FontStyle]::Bold)
$fontSection = New-Object System.Drawing.Font("Segoe UI", 12, [System.Drawing.FontStyle]::Bold)
$fontTitle = New-Object System.Drawing.Font("Segoe UI", 15, [System.Drawing.FontStyle]::Bold)
$fontBody = New-Object System.Drawing.Font("Segoe UI", 10.5, [System.Drawing.FontStyle]::Regular)
$fontBodyBold = New-Object System.Drawing.Font("Segoe UI", 10.5, [System.Drawing.FontStyle]::Bold)
$fontItalic = New-Object System.Drawing.Font("Georgia", 10.5, [System.Drawing.FontStyle]::Italic)
$fontSmall = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Regular)
$fontIPA = New-Object System.Drawing.Font("Lucida Console", 11, [System.Drawing.FontStyle]::Regular)

function Save-Bitmap($bmp, $filename) {
    $path = Join-Path $artifactDir $filename
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Output "Saved: $path"
}

# ==========================================
# 1. DICTIONARY DESIGN 1: CURATED LEXICON
# ==========================================
$w = 900; $h = 560
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit

# Window Frame
$g.Clear([System.Drawing.ColorTranslator]::FromHtml("#18191E"))
$winBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#22232A"))
$winPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#383A46"), 1.5)
Draw-RoundBox $g $winPen $winBrush 30 30 840 500 16

# Title bar
$tbBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#1C1D24"))
Draw-RoundBox $g $null $tbBrush 30 30 840 46 16
$g.FillRectangle($tbBrush, 30, 56, 840, 20)
$dotRed = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#EF4444"))
$dotYel = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#F59E0B"))
$dotGrn = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#10B981"))
$g.FillEllipse($dotRed, 52, 48, 12, 12)
$g.FillEllipse($dotYel, 72, 48, 12, 12)
$g.FillEllipse($dotGrn, 92, 48, 12, 12)
$g.DrawString("Design 1: Curated Lexicon (Apple Books & Oxford Edition)", $fontSection, [System.Drawing.Brushes]::White, 125, 43)

# Search Input
$searchBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#16171D"))
$searchPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#454859"), 1)
Draw-RoundBox $g $searchPen $searchBg 55 90 790 40 8
$g.DrawString("[?]  Search word or phrase... [ ephemeral ]", $fontBody, [System.Drawing.Brushes]::LightGray, 70, 100)

# Word Header
$g.DrawString("ephemeral", $fontHead, [System.Drawing.Brushes]::White, 55, 145)
$ipaBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#2E313D"))
Draw-RoundBox $g $null $ipaBg 240 152 140 28 6
$g.DrawString("/ɪˈfɛm.ər.əl/", $fontIPA, [System.Drawing.Brushes]::SkyBlue, 248, 158)

# Audio button
$audioBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#3B82F6"))
Draw-RoundBox $g $null $audioBg 395 152 110 28 6
$g.DrawString("Play Audio", $fontSmall, [System.Drawing.Brushes]::White, 410, 158)

# Tags
$tagBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#8B5CF6"))
Draw-RoundBox $g $null $tagBg 520 152 95 28 6
$g.DrawString("ADJECTIVE", $fontSmall, [System.Drawing.Brushes]::White, 532, 158)

# Sense 1
$g.DrawString("1. Lasting for a very short time; transitory; fleeting.", $fontBodyBold, [System.Drawing.Brushes]::White, 55, 200)
$g.DrawString("""Fame in the digital era is remarkably ephemeral and easily overtaken.""", $fontItalic, [System.Drawing.Brushes]::Silver, 75, 226)

# Synonyms pills
$g.DrawString("Synonyms:", $fontSmall, [System.Drawing.Brushes]::Gray, 75, 258)
$syns = @("transient", "evanescent", "fleeting", "fugacious")
$sx = 145
foreach ($s in $syns) {
    $pillBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#2A2C38"))
    $pillPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#4F5368"), 1)
    Draw-RoundBox $g $pillPen $pillBg $sx 254 85 24 5
    $g.DrawString($s, $fontSmall, [System.Drawing.Brushes]::LightSkyBlue, ($sx + 8), 258)
    $sx += 95
}

# Sense 2
$g.DrawString("2. (Botany/Hydrology) Having a very brief active life cycle.", $fontBodyBold, [System.Drawing.Brushes]::White, 55, 296)
$g.DrawString("""Desert ephemerals bloom rapidly after torrential rains, seeding within days.""", $fontItalic, [System.Drawing.Brushes]::Silver, 75, 322)

# Reading Context Bridge Card
$ctxBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#1E202A"))
$ctxPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#3E4256"), 1)
Draw-RoundBox $g $ctxPen $ctxBg 55 365 790 85 10
$g.DrawString("[Context]  CAPTURED FROM PDF READING SESSION", $fontSmall, [System.Drawing.Brushes]::Gold, 75, 377)
$g.DrawString("Document: Philosophy of Time.pdf  •  Page 42  •  Chapter 3", $fontSmall, [System.Drawing.Brushes]::LightSteelBlue, 75, 396)
$g.DrawString("""...the ephemeral nature of empires when contrasted against geological epochs...""", $fontItalic, [System.Drawing.Brushes]::White, 75, 416)

# Action Buttons
$btnFlash = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#10B981"))
Draw-RoundBox $g $null $btnFlash 55 470 190 38 8
$g.DrawString("+ Save to Flashcards", $fontBodyBold, [System.Drawing.Brushes]::White, 70, 480)

$btnCopy = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#2F3342"))
Draw-RoundBox $g $null $btnCopy 260 470 130 38 8
$g.DrawString("[Copy] Card", $fontBodyBold, [System.Drawing.Brushes]::White, 280, 480)

Save-Bitmap $bmp "dictionary_design_1_curated_lexicon.png"
$g.Dispose(); $bmp.Dispose()

# ==========================================
# 2. DICTIONARY DESIGN 2: DEEP-WORK INSPECTOR
# ==========================================
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit

$g.Clear([System.Drawing.ColorTranslator]::FromHtml("#0D1117"))
$winBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#161B22"))
$winPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#30363D"), 1.5)
Draw-RoundBox $g $winPen $winBrush 30 30 840 500 12

# Title bar
$tbBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#1F242C"))
Draw-RoundBox $g $null $tbBrush 30 30 840 44 12
$g.FillRectangle($tbBrush, 30, 54, 840, 20)
$g.FillEllipse($dotRed, 52, 46, 12, 12)
$g.FillEllipse($dotYel, 72, 46, 12, 12)
$g.FillEllipse($dotGrn, 92, 46, 12, 12)
$g.DrawString("Design 2: Deep-Work Inspector (Obsidian / Notion Dual-Pane Split)", $fontSection, [System.Drawing.Brushes]::White, 125, 42)

# Left Pane: Search & History (Width 250)
$sideBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#12161C"))
$g.FillRectangle($sideBg, 31, 75, 250, 454)
$g.DrawLine($winPen, 281, 75, 281, 529)

# Sidebar Search
$sideSearch = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#21262D"))
Draw-RoundBox $g $null $sideSearch 45 90 220 32 6
$g.DrawString("Filter lookups...", $fontSmall, [System.Drawing.Brushes]::Gray, 55, 98)

$g.DrawString("RECENT LOOKUPS", $fontSmall, [System.Drawing.Brushes]::DodgerBlue, 45, 135)
$words = @("> serendipity", "  ephemeral", "  ubiquitous", "  juxtaposition", "  paradigm", "  quixotic")
$wy = 160
foreach ($wd in $words) {
    if ($wd.StartsWith(">")) {
        $selBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#1F2D3D"))
        $g.FillRectangle($selBg, 35, $wy - 4, 240, 26)
        $g.DrawString($wd, $fontBodyBold, [System.Drawing.Brushes]::DodgerBlue, 45, $wy)
    } else {
        $g.DrawString($wd, $fontBody, [System.Drawing.Brushes]::Silver, 45, $wy)
    }
    $wy += 30
}

$g.DrawString("DICTIONARY PACKS", $fontSmall, [System.Drawing.Brushes]::DodgerBlue, 45, 360)
$g.DrawString("[x] WordNet 3.1 EN (Offline)", $fontSmall, [System.Drawing.Brushes]::LightGreen, 45, 385)
$g.DrawString("[x] Kaikki Comprehensive", $fontSmall, [System.Drawing.Brushes]::LightGreen, 45, 410)
$g.DrawString("[ ] French-English Pack", $fontSmall, [System.Drawing.Brushes]::Gray, 45, 435)
$g.DrawString("+ Add Language Pack...", $fontSmall, [System.Drawing.Brushes]::DeepSkyBlue, 45, 465)

# Right Pane: Detailed Entry
$g.DrawString("serendipity", $fontHead, [System.Drawing.Brushes]::White, 310, 95)
$g.DrawString("/ˌsɛr.ənˈdɪp.ɪ.ti/", $fontIPA, [System.Drawing.Brushes]::SkyBlue, 310, 140)
$g.DrawString("NOUN  •  Etymology: Serendip (Old Persian / Sri Lanka) via Horace Walpole (1754)", $fontSmall, [System.Drawing.Brushes]::Silver, 310, 168)
$g.DrawLine($winPen, 310, 190, 840, 190)

$g.DrawString("1. The faculty of making fortunate discoveries by accident or good fortune.", $fontBodyBold, [System.Drawing.Brushes]::White, 310, 205)
$g.DrawString("""Alexander Fleming's discovery of penicillin was a monumental act of serendipity.""", $fontItalic, [System.Drawing.Brushes]::Gainsboro, 330, 230)

$g.DrawString("WORD FAMILY & MORPHOLOGY", $fontSmall, [System.Drawing.Brushes]::Gold, 310, 275)
$g.DrawString("• Adjective: serendipitous (""a serendipitous encounter on the metro"")", $fontBody, [System.Drawing.Brushes]::Silver, 330, 298)
$g.DrawString("• Adverb: serendipitously (""serendipitously arrived just in time"")", $fontBody, [System.Drawing.Brushes]::Silver, 330, 322)

$g.DrawString("TAGS & FREQUENCY", $fontSmall, [System.Drawing.Brushes]::Gold, 310, 360)
$g.DrawString("#literature   #psychology   #gre-high-frequency   Top 5000 English Words", $fontSmall, [System.Drawing.Brushes]::LightSkyBlue, 330, 382)

# Quick Action Bar
$g.DrawLine($winPen, 310, 420, 840, 420)
Draw-RoundBox $g $null $btnFlash 310 445 160 36 6
$g.DrawString("+ Add to Anki/Hub", $fontBodyBold, [System.Drawing.Brushes]::White, 325, 454)

$btnObs = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#7C3AED"))
Draw-RoundBox $g $null $btnObs 485 445 160 36 6
$g.DrawString("Export Markdown", $fontBodyBold, [System.Drawing.Brushes]::White, 505, 454)

Save-Bitmap $bmp "dictionary_design_2_deep_work_inspector.png"
$g.Dispose(); $bmp.Dispose()

# ==========================================
# 3. DICTIONARY DESIGN 3: FLUID GLASS POPOVER
# ==========================================
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit

# Simulated document background
$g.Clear([System.Drawing.ColorTranslator]::FromHtml("#1A1D24"))
$docBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#282C37"))
$g.FillRectangle($docBrush, 50, 40, 800, 480)
for ($i = 60; $i -lt 480; $i += 24) {
    $g.DrawLine([System.Drawing.Pens]::DimGray, 80, $i, 820, $i)
}

# Floating Popover Card
$popBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#111317"))
$popPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#4F46E5"), 2)
Draw-RoundBox $g $popPen $popBg 140 100 620 360 16

# Search Header
$g.DrawString("ubiquitous", $fontHead, [System.Drawing.Brushes]::White, 175, 130)
$g.DrawString("/juːˈbɪk.wɪ.təs/    ADJECTIVE", $fontSmall, [System.Drawing.Brushes]::SkyBlue, 175, 175)

$g.DrawLine([System.Drawing.Pens]::DarkSlateGray, 175, 205, 725, 205)

$g.DrawString("Present, appearing, or found everywhere; omnipresent.", $fontTitle, [System.Drawing.Brushes]::WhiteSmoke, 175, 225)
$g.DrawString("""Mobile computation has become ubiquitous across 21st-century civilization.""", $fontItalic, [System.Drawing.Brushes]::LightGray, 175, 265)

$g.DrawString("Synonyms: omnipresent  •  pervasive  •  universal  •  rampant", $fontSmall, [System.Drawing.Brushes]::Plum, 175, 310)

# Hotkey HUD strip at bottom
$hudBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#1D2028"))
Draw-RoundBox $g $null $hudBg 160 360 580 45 8
$g.DrawString("[Enter] Save Card    [Space] Pronounce    [1-9] Senses    [Esc] Dismiss", $fontSmall, [System.Drawing.Brushes]::Gold, 200, 375)

Save-Bitmap $bmp "dictionary_design_3_fluid_glass_popover.png"
$g.Dispose(); $bmp.Dispose()

# ==========================================
# 4. LEARNING HUB DESIGN 1: MASTERY DASHBOARD
# ==========================================
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit

$g.Clear([System.Drawing.ColorTranslator]::FromHtml("#0F172A"))
$winBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#1E293B"))
$winPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#334155"), 1.5)
Draw-RoundBox $g $winPen $winBrush 30 30 840 500 14

# Title bar
$tbBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#0F172A"))
Draw-RoundBox $g $null $tbBrush 30 30 840 44 14
$g.FillRectangle($tbBrush, 30, 54, 840, 20)
$g.FillEllipse($dotRed, 52, 46, 12, 12)
$g.FillEllipse($dotYel, 72, 46, 12, 12)
$g.FillEllipse($dotGrn, 92, 46, 12, 12)
$g.DrawString("Design 1: Mastery Dashboard (Anki Pro & Duolingo Clean Gamified Studio)", $fontSection, [System.Drawing.Brushes]::White, 125, 42)

# Metrics Strip (4 stat widgets)
$stats = @(
    @{ Icon="Streak:"; Val="14 Days"; Label="Daily Streak"; Col="#F97316" },
    @{ Icon="Cards:"; Val="342 Cards"; Label="Learned Total"; Col="#3B82F6" },
    @{ Icon="Score:"; Val="91%"; Label="Recall Accuracy"; Col="#10B981" },
    @{ Icon="Due:"; Val="24 Due"; Label="Reviews Today"; Col="#EAB308" }
)
$sx = 55
foreach ($st in $stats) {
    $statBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#0F172A"))
    $statPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#334155"), 1)
    Draw-RoundBox $g $statPen $statBg $sx 85 185 65 8
    $g.DrawString($st.Icon + " " + $st.Val, $fontTitle, (New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($st.Col))), ($sx + 12), 95)
    $g.DrawString($st.Label, $fontSmall, [System.Drawing.Brushes]::Silver, ($sx + 15), 125)
    $sx += 200
}

# Active Study Deck Banner
$g.DrawString("ACTIVE DECKS SHELF", $fontSection, [System.Drawing.Brushes]::White, 55, 165)
$decks = @(
    @{ Name="GRE Vocabulary Core"; Total="180 Words"; Due="12 Due"; Prog=75 },
    @{ Name="PDF Reading Highlights"; Total="64 Words"; Due="8 Due"; Prog=85 },
    @{ Name="Medical Terminology"; Total="98 Words"; Due="4 Due"; Prog=45 }
)
$dx = 55
foreach ($dk in $decks) {
    $dkBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#111827"))
    $dkPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#374151"), 1)
    Draw-RoundBox $g $dkPen $dkBg $dx 195 250 115 10
    $g.DrawString($dk.Name, $fontBodyBold, [System.Drawing.Brushes]::White, ($dx + 12), 205)
    $g.DrawString($dk.Total + "  •  " + $dk.Due, $fontSmall, [System.Drawing.Brushes]::Gold, ($dx + 12), 230)
    # Progress bar
    $barBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#1F2937"))
    Draw-RoundBox $g $null $barBg ($dx + 12) 255 226 8 4
    $barFill = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#10B981"))
    Draw-RoundBox $g $null $barFill ($dx + 12) 255 ($dk.Prog * 2.26) 8 4
    $g.DrawString("Start Session ->", $fontSmall, [System.Drawing.Brushes]::LightSkyBlue, ($dx + 140), 280)
    $dx += 270
}

# Study Card Arena
$arenaBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#0F172A"))
$arenaPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#475569"), 1)
Draw-RoundBox $g $arenaPen $arenaBg 55 325 790 185 12
$g.DrawString("CARD 4 OF 24  •  ACTIVE RECALL (SM-2)", $fontSmall, [System.Drawing.Brushes]::SkyBlue, 75, 338)
$g.DrawString("ephemeral", $fontHead, [System.Drawing.Brushes]::White, 350, 360)
$g.DrawString("/ɪˈfɛm.ər.əl/", $fontIPA, [System.Drawing.Brushes]::Silver, 385, 400)
$g.DrawString("[ Space / Click to Reveal Meaning & Context ]", $fontSmall, [System.Drawing.Brushes]::Gray, 290, 430)

# 4 SM-2 Interval buttons
$btns = @(
    @{ T="(1) Again <10m"; C="#EF4444" },
    @{ T="(2) Hard 1d"; C="#F59E0B" },
    @{ T="(3) Good 3d"; C="#3B82F6" },
    @{ T="(4) Easy 7d"; C="#10B981" }
)
$bx = 75
foreach ($b in $btns) {
    $btnBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($b.C))
    Draw-RoundBox $g $null $btnBrush $bx 460 175 36 6
    $g.DrawString($b.T, $fontBodyBold, [System.Drawing.Brushes]::White, ($bx + 20), 468)
    $bx += 195
}

Save-Bitmap $bmp "learning_hub_design_1_mastery_dashboard.png"
$g.Dispose(); $bmp.Dispose()

# ==========================================
# 5. LEARNING HUB DESIGN 2: COGNITIVE CANVAS
# ==========================================
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit

$g.Clear([System.Drawing.ColorTranslator]::FromHtml("#18181B"))
$winBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#27272A"))
$winPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#3F3F46"), 1.5)
Draw-RoundBox $g $winPen $winBrush 30 30 840 500 12

$tbBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#202023"))
Draw-RoundBox $g $null $tbBrush 30 30 840 44 12
$g.FillRectangle($tbBrush, 30, 54, 840, 20)
$g.FillEllipse($dotRed, 52, 46, 12, 12)
$g.FillEllipse($dotYel, 72, 46, 12, 12)
$g.FillEllipse($dotGrn, 92, 46, 12, 12)
$g.DrawString("Design 2: Cognitive Knowledge Canvas (RemNote & SuperMemo Suite)", $fontSection, [System.Drawing.Brushes]::White, 125, 42)

# Left Column: Deck & Tags Filter
$sideBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#1E1E21"))
$g.FillRectangle($sideBg, 31, 75, 230, 454)
$g.DrawLine($winPen, 261, 75, 261, 529)

$g.DrawString("KNOWLEDGE SOURCES", $fontSmall, [System.Drawing.Brushes]::Violet, 45, 90)
$g.DrawString("[All] All Cards (342)", $fontBodyBold, [System.Drawing.Brushes]::White, 45, 115)
$g.DrawString("[Due] Due Today (24)", $fontBody, [System.Drawing.Brushes]::Gold, 45, 145)
$g.DrawString("By PDF Document:", $fontSmall, [System.Drawing.Brushes]::Gray, 45, 180)
$g.DrawString("  * Quantum_Physics.pdf", $fontSmall, [System.Drawing.Brushes]::LightBlue, 45, 205)
$g.DrawString("  * Neuroscience_Vol1.pdf", $fontSmall, [System.Drawing.Brushes]::LightBlue, 45, 230)
$g.DrawString("  * Ancient_History.pdf", $fontSmall, [System.Drawing.Brushes]::LightBlue, 45, 255)

$g.DrawString("Tags:", $fontSmall, [System.Drawing.Brushes]::Gray, 45, 295)
$g.DrawString("  #exam-prep   #priority", $fontSmall, [System.Drawing.Brushes]::Plum, 45, 320)
$g.DrawString("  #difficult   #research", $fontSmall, [System.Drawing.Brushes]::Plum, 45, 345)

# Right Column: Multi-Mode Testing Arena
$tabBg1 = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#3B82F6"))
$tabBg2 = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#3F3F46"))
Draw-RoundBox $g $null $tabBg1 285 90 140 32 6
$g.DrawString("Sentence Cloze", $fontSmall, [System.Drawing.Brushes]::White, 305, 98)
Draw-RoundBox $g $null $tabBg2 435 90 140 32 6
$g.DrawString("Flashcard Flip", $fontSmall, [System.Drawing.Brushes]::Silver, 460, 98)
Draw-RoundBox $g $null $tabBg2 585 90 150 32 6
$g.DrawString("Multiple Choice", $fontSmall, [System.Drawing.Brushes]::Silver, 610, 98)

# Cloze Prompt Card
$clozeBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#18181B"))
$clozePen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#52525B"), 1)
Draw-RoundBox $g $clozePen $clozeBg 285 140 560 250 12

$g.DrawString("FILL IN THE BLANK (CLOZE RECALL):", $fontSmall, [System.Drawing.Brushes]::Gold, 310, 160)
$g.DrawString("""The biologist noted that insect swarms were remarkably ___________,""", $fontTitle, [System.Drawing.Brushes]::White, 310, 195)
$g.DrawString("""emerging only for three days following the desert monsoon.""", $fontTitle, [System.Drawing.Brushes]::White, 310, 225)

# Input Box
$inputBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#27272A"))
$inputPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#3B82F6"), 2)
Draw-RoundBox $g $inputPen $inputBg 310 270 340 40 8
$g.DrawString("ephemeral|", $fontTitle, [System.Drawing.Brushes]::White, 325, 276)

$btnCheck = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#10B981"))
Draw-RoundBox $g $null $btnCheck 665 270 140 40 8
$g.DrawString("Check [Enter]", $fontBodyBold, [System.Drawing.Brushes]::White, 685, 280)

# Jump to PDF link
$g.DrawString("PDF Source: Entomology Treatise (2024), Page 112", $fontSmall, [System.Drawing.Brushes]::LightGray, 310, 335)
$g.DrawString("-> Jump directly to this page in SumatraPDF Reader", $fontSmall, [System.Drawing.Brushes]::DodgerBlue, 310, 355)

Save-Bitmap $bmp "learning_hub_design_2_cognitive_canvas.png"
$g.Dispose(); $bmp.Dispose()

# ==========================================
# 6. LEARNING HUB DESIGN 3: ZEN COMPANION
# ==========================================
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit

# Warm Sepia paper background
$g.Clear([System.Drawing.ColorTranslator]::FromHtml("#F5EDE0"))
$winBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#FBF0D9"))
$winPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#D8C8B0"), 2)
Draw-RoundBox $g $winPen $winBrush 30 30 840 500 16

# Title bar
$tbBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#EFE2CC"))
Draw-RoundBox $g $null $tbBrush 30 30 840 44 16
$g.FillRectangle($tbBrush, 30, 54, 840, 20)
$g.FillEllipse($dotRed, 52, 46, 12, 12)
$g.FillEllipse($dotYel, 72, 46, 12, 12)
$g.FillEllipse($dotGrn, 92, 46, 12, 12)
$g.DrawString("Design 3: Zen Reader's Companion (Readwise Reader & Calibre Elegance)", $fontSection, [System.Drawing.Brushes]::SaddleBrown, 125, 42)

# Centralized Sprint Card
$g.DrawString("5-MINUTE DAILY VOCABULARY SPRINT", $fontSmall, [System.Drawing.Brushes]::DarkOliveGreen, 320, 105)
$g.DrawString("12 Cards Remaining in Today's Gentle Review", $fontSmall, [System.Drawing.Brushes]::DarkGoldenrod, 305, 125)

$cardBg = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#FFF9EE"))
$cardPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#DECAB0"), 1.5)
Draw-RoundBox $g $cardPen $cardBg 140 160 620 250 16

$g.DrawString("s e r e n d i p i t y", $fontHead, [System.Drawing.Brushes]::SaddleBrown, 310, 205)
$g.DrawString("/ˌsɛr.ənˈdɪp.ɪ.ti/  •  noun", $fontItalic, [System.Drawing.Brushes]::Gray, 335, 255)
$g.DrawString("Can you recall the meaning and sentence from your reading?", $fontBody, [System.Drawing.Brushes]::DimGray, 240, 295)

$g.DrawString("[ Tap or press Spacebar to reflect & reveal ]", $fontSmall, [System.Drawing.Brushes]::DarkGoldenrod, 300, 345)

# Gentle 2-rating buttons
$btnReview = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#E08B7A"))
Draw-RoundBox $g $null $btnReview 250 445 170 42 10
$g.DrawString("Needs Review", $fontBodyBold, [System.Drawing.Brushes]::White, 285, 456)

$btnMaster = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#7FA988"))
Draw-RoundBox $g $null $btnMaster 480 445 170 42 10
$g.DrawString("Mastered", $fontBodyBold, [System.Drawing.Brushes]::White, 530, 456)

Save-Bitmap $bmp "learning_hub_design_3_zen_companion.png"
$g.Dispose(); $bmp.Dispose()

Write-Output "All 6 designs rendered to PNG successfully!"
