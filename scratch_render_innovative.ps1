Add-Type -AssemblyName System.Drawing

$artifactDir = "C:\Users\abelo\.gemini\antigravity\brain\bc13cb30-3bca-4de0-9d75-71372ae4e772"

$innovativeDesigns = @(
    @{
        Id = 1
        Name = "Design 01 - Seamless Aero Dock (Tri-Segment Capsule)"
        Concept = "Unified aerodynamic pill dock with micro-lit chambers; shared boundary prevents visual clutter"
        Min = "#38BDF8"; Max = "#10B981"; Close = "#F43F5E"
        Type = "SegmentedPill"
    },
    @{
        Id = 2
        Name = "Design 02 - Luminous Quantum Halos (Concentric Rings)"
        Concept = "Hollow circular conduits with intense photon cores; distinct from plain solid macOS traffic lights"
        Min = "#F59E0B"; Max = "#06D6A0"; Close = "#EF4444"
        Type = "HaloRing"
    },
    @{
        Id = 3
        Name = "Design 03 - Morphic Geometric Runes (Shape = Function)"
        Concept = "Shape communicates action: Horizontal Dash pill (Min), Diamond Rhombus (Max), Shielded Cross (Close)"
        Min = "#60A5FA"; Max = "#34D399"; Close = "#FB7185"
        Type = "GeometricMorphic"
    },
    @{
        Id = 4
        Name = "Design 04 - Obsidian Cyber-Prisms (Chamfered Hex)"
        Concept = "Precision-cut jewel facets with directional edge specular highlights for a modern architectural look"
        Min = "#FBBF24"; Max = "#2DD4BF"; Close = "#F43F5E"
        Type = "ChamferPrism"
    },
    @{
        Id = 5
        Name = "Design 05 - OLED Minimal Micro-Pips (Interactive Bloom)"
        Concept = "Subtle 6px precision pips that blossom into tactile action targets with glowing glyphs on mouse hover"
        Min = "#38BDF8"; Max = "#10B981"; Close = "#FF3366"
        Type = "MicroPips"
    },
    @{
        Id = 6
        Name = "Design 06 - Aurora Duotone Ribbons (Fluid Capsules)"
        Concept = "Horizontal pill ribbons with vibrant dual-tone gradients and crisp laser-etched action lines"
        Min = "#F59E0B"; Max = "#10B981"; Close = "#E11D48"
        Type = "AuroraRibbon"
    },
    @{
        Id = 7
        Name = "Design 07 - Sumatra Gold Royale (Jewel-Encrusted)"
        Concept = "Signature luxury champagne-gold chassis hosting inset Ruby, Emerald, and Topaz gems"
        Min = "#EAB308"; Max = "#10B981"; Close = "#DC2626"
        GoldChassis = "#D97706"
        Type = "GoldRoyale"
    }
)

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

$fontTitle = New-Object System.Drawing.Font("Segoe UI", 14, [System.Drawing.FontStyle]::Bold)
$fontSub = New-Object System.Drawing.Font("Segoe UI", 10, [System.Drawing.FontStyle]::Regular)
$fontCode = New-Object System.Drawing.Font("Consolas", 9.5, [System.Drawing.FontStyle]::Regular)
$fontLabel = New-Object System.Drawing.Font("Segoe UI", 9.5, [System.Drawing.FontStyle]::Bold)
$fontPosterHead = New-Object System.Drawing.Font("Segoe UI", 21, [System.Drawing.FontStyle]::Bold)
$fontPosterSub = New-Object System.Drawing.Font("Segoe UI", 11, [System.Drawing.FontStyle]::Regular)

function Draw-Innovative-Card($g, $d, $xOffset, $yOffset) {
    $w = 1160
    $h = 290

    # Background card
    $bgBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#111318"))
    Draw-RoundBox $g $null $bgBrush $xOffset $yOffset $w $h 14
    $bgBrush.Dispose()

    $cardPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#222632"), 1.5)
    Draw-RoundBox $g $cardPen $null $xOffset $yOffset $w $h 14
    $cardPen.Dispose()

    $titleBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#F8FAFC"))
    $subBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#94A3B8"))

    $g.DrawString($d.Name, $fontTitle, $titleBrush, ($xOffset + 28), ($yOffset + 20))
    $g.DrawString($d.Concept, $fontSub, $subBrush, ($xOffset + 28), ($yOffset + 48))

    $titleBrush.Dispose()
    $subBrush.Dispose()

    # 1. LIGHT WINDOW
    $boxW = 520
    $boxH = 135
    $yBox = $yOffset + 80
    $xLight = $xOffset + 28

    $lBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#F8FAFC"))
    $lPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#CBD5E1"), 1)
    Draw-RoundBox $g $lPen $lBrush $xLight $yBox $boxW $boxH 10
    $lBrush.Dispose()

    $lHeadBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#E2E8F0"))
    Draw-RoundBox $g $null $lHeadBrush $xLight $yBox $boxW 38 10
    $g.FillRectangle($lHeadBrush, $xLight, ($yBox + 18), $boxW, 20)
    $g.DrawLine($lPen, $xLight, ($yBox + 38), ($xLight + $boxW), ($yBox + 38))
    $lHeadBrush.Dispose()
    $lPen.Dispose()

    $lblDarkText = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#334155"))
    $g.DrawString("SumatraPDF Enhanced - Light Theme", $fontLabel, $lblDarkText, ($xLight + 14), ($yBox + 10))
    $lblDarkText.Dispose()

    # Mock content
    $cMock1 = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#E2E8F0"))
    Draw-RoundBox $g $null $cMock1 ($xLight + 16) ($yBox + 52) 180 12 4
    Draw-RoundBox $g $null $cMock1 ($xLight + 16) ($yBox + 72) 320 8 3
    Draw-RoundBox $g $null $cMock1 ($xLight + 16) ($yBox + 88) 260 8 3
    $cMock1.Dispose()

    # 2. DARK WINDOW
    $xDark = $xOffset + 576
    $dBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#16181F"))
    $dPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#2A2E3B"), 1)
    Draw-RoundBox $g $dPen $dBrush $xDark $yBox $boxW $boxH 10
    $dBrush.Dispose()

    $dHeadBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#1F232D"))
    Draw-RoundBox $g $null $dHeadBrush $xDark $yBox $boxW 38 10
    $g.FillRectangle($dHeadBrush, $xDark, ($yBox + 18), $boxW, 20)
    $g.DrawLine($dPen, $xDark, ($yBox + 38), ($xDark + $boxW), ($yBox + 38))
    $dHeadBrush.Dispose()
    $dPen.Dispose()

    $lblLightText = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#E2E8F0"))
    $g.DrawString("SumatraPDF Enhanced - Dark Theme", $fontLabel, $lblLightText, ($xDark + 14), ($yBox + 10))
    $lblLightText.Dispose()

    $cMock2 = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#262B37"))
    Draw-RoundBox $g $null $cMock2 ($xDark + 16) ($yBox + 52) 180 12 4
    Draw-RoundBox $g $null $cMock2 ($xDark + 16) ($yBox + 72) 320 8 3
    Draw-RoundBox $g $null $cMock2 ($xDark + 16) ($yBox + 88) 260 8 3
    $cMock2.Dispose()

    # RENDER INNOVATIVE CONTROLS PER TYPE
    $renderInnovative = {
        param($curX, $curY, $isDarkTheme)
        
        switch ($d.Type) {
            "SegmentedPill" {
                $dockW = 86
                $dockH = 22
                $dockX = $curX - 10
                $dockY = $curY - 3

                $dockBg = if ($isDarkTheme) { "#14171E" } else { "#DCE3EC" }
                $dockBorder = if ($isDarkTheme) { "#2D3442" } else { "#BAC7D5" }
                $dB = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($dockBg))
                $dP = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml($dockBorder), 1)
                Draw-RoundBox $g $dP $dB $dockX $dockY $dockW $dockH 11
                $dB.Dispose()
                $dP.Dispose()

                $divP = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml($dockBorder), 1)
                $g.DrawLine($divP, ($dockX + 28), ($dockY + 4), ($dockX + 28), ($dockY + $dockH - 4))
                $g.DrawLine($divP, ($dockX + 57), ($dockY + 4), ($dockX + 57), ($dockY + $dockH - 4))
                $divP.Dispose()

                $b1 = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($d.Min))
                $g.FillEllipse($b1, ($dockX + 9), ($dockY + 6), 10, 10)
                $b1.Dispose()

                $b2 = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($d.Max))
                $g.FillEllipse($b2, ($dockX + 38), ($dockY + 6), 10, 10)
                $b2.Dispose()

                $b3 = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($d.Close))
                $g.FillEllipse($b3, ($dockX + 67), ($dockY + 6), 10, 10)
                $b3.Dispose()

                $gp = New-Object System.Drawing.Pen([System.Drawing.Color]::White, 1.3)
                $g.DrawLine($gp, ($dockX + 11), ($dockY + 11), ($dockX + 17), ($dockY + 11))
                $g.DrawLine($gp, ($dockX + 40), ($dockY + 13), ($dockX + 46), ($dockY + 9))
                $g.DrawLine($gp, ($dockX + 70), ($dockY + 9), ($dockX + 74), ($dockY + 13))
                $g.DrawLine($gp, ($dockX + 74), ($dockY + 9), ($dockX + 70), ($dockY + 13))
                $gp.Dispose()
            }

            "HaloRing" {
                $btns = @(
                    @{ C = $d.Min; X = $curX },
                    @{ C = $d.Max; X = ($curX + 26) },
                    @{ C = $d.Close; X = ($curX + 52) }
                )
                foreach ($b in $btns) {
                    $haloPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml($b.C), 1.5)
                    $g.DrawEllipse($haloPen, $b.X, $curY, 16, 16)
                    $haloPen.Dispose()

                    $coreBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($b.C))
                    $g.FillEllipse($coreBrush, ($b.X + 5), ($curY + 5), 6, 6)
                    $coreBrush.Dispose()
                }
            }

            "GeometricMorphic" {
                $minB = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($d.Min))
                Draw-RoundBox $g $null $minB $curX ($curY + 4) 18 8 4
                $minB.Dispose()

                $diaB = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($d.Max))
                $diamond = New-Object System.Drawing.Drawing2D.GraphicsPath
                $diaX = $curX + 32
                $diaY = $curY + 1
                $diamond.AddPolygon(@(
                    (New-Object System.Drawing.PointF(($diaX + 8), $diaY)),
                    (New-Object System.Drawing.PointF(($diaX + 16), ($diaY + 8))),
                    (New-Object System.Drawing.PointF(($diaX + 8), ($diaY + 16))),
                    (New-Object System.Drawing.PointF($diaX, ($diaY + 8)))
                ))
                $g.FillPath($diaB, $diamond)
                $diaB.Dispose()
                $diamond.Dispose()

                $clsB = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($d.Close))
                $clsX = $curX + 58
                Draw-RoundBox $g $null $clsB $clsX $curY 16 16 4
                $clsB.Dispose()

                $xPen = New-Object System.Drawing.Pen([System.Drawing.Color]::White, 1.8)
                $g.DrawLine($xPen, ($clsX + 4), ($curY + 4), ($clsX + 12), ($curY + 12))
                $g.DrawLine($xPen, ($clsX + 12), ($curY + 4), ($clsX + 4), ($curY + 12))
                $xPen.Dispose()
            }

            "ChamferPrism" {
                $btns = @(
                    @{ C = $d.Min; X = $curX },
                    @{ C = $d.Max; X = ($curX + 25) },
                    @{ C = $d.Close; X = ($curX + 50) }
                )
                foreach ($b in $btns) {
                    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
                    $cx = $b.X
                    $cy = $curY
                    $path.AddPolygon(@(
                        (New-Object System.Drawing.PointF(($cx + 4), $cy)),
                        (New-Object System.Drawing.PointF(($cx + 12), $cy)),
                        (New-Object System.Drawing.PointF(($cx + 16), ($cy + 4))),
                        (New-Object System.Drawing.PointF(($cx + 16), ($cy + 12))),
                        (New-Object System.Drawing.PointF(($cx + 12), ($cy + 16))),
                        (New-Object System.Drawing.PointF(($cx + 4), ($cy + 16))),
                        (New-Object System.Drawing.PointF($cx, ($cy + 12))),
                        (New-Object System.Drawing.PointF($cx, ($cy + 4)))
                    ))
                    $br = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($b.C))
                    $g.FillPath($br, $path)
                    $br.Dispose()

                    $pStroke = New-Object System.Drawing.Pen([System.Drawing.Color]::White, 0.8)
                    $g.DrawLine($pStroke, ($cx + 4), $cy, ($cx + 12), $cy)
                    $pStroke.Dispose()
                    $path.Dispose()
                }
            }

            "MicroPips" {
                $p1 = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($d.Min))
                $g.FillEllipse($p1, ($curX + 4), ($curY + 4), 8, 8)
                $p1.Dispose()

                $p2 = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($d.Max))
                $g.FillEllipse($p2, ($curX + 28), ($curY + 4), 8, 8)
                $p2.Dispose()

                $p3 = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($d.Close))
                Draw-RoundBox $g $null $p3 ($curX + 50) ($curY) 16 16 5
                $p3.Dispose()

                $xp = New-Object System.Drawing.Pen([System.Drawing.Color]::White, 1.6)
                $g.DrawLine($xp, ($curX + 54), ($curY + 4), ($curX + 62), ($curY + 12))
                $g.DrawLine($xp, ($curX + 62), ($curY + 4), ($curX + 54), ($curY + 12))
                $xp.Dispose()
            }

            "AuroraRibbon" {
                $rW = 20
                $rH = 13
                $b1 = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($d.Min))
                Draw-RoundBox $g $null $b1 $curX ($curY + 2) $rW $rH 6
                $b1.Dispose()

                $b2 = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($d.Max))
                Draw-RoundBox $g $null $b2 ($curX + 26) ($curY + 2) $rW $rH 6
                $b2.Dispose()

                $b3 = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($d.Close))
                Draw-RoundBox $g $null $b3 ($curX + 52) ($curY + 2) $rW $rH 6
                $b3.Dispose()

                $lp = New-Object System.Drawing.Pen([System.Drawing.Color]::White, 1.4)
                $g.DrawLine($lp, ($curX + 6), ($curY + 8), ($curX + 14), ($curY + 8))
                $g.DrawLine($lp, ($curX + 32), ($curY + 5), ($curX + 40), ($curY + 11))
                $g.DrawLine($lp, ($curX + 58), ($curY + 5), ($curX + 66), ($curY + 11))
                $g.DrawLine($lp, ($curX + 66), ($curY + 5), ($curX + 58), ($curY + 11))
                $lp.Dispose()
            }

            "GoldRoyale" {
                $dockW = 84
                $dockH = 22
                $dockX = $curX - 8
                $dockY = $curY - 3

                $chassisBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#D97706"))
                $chassisPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml("#FDE68A"), 1)
                Draw-RoundBox $g $chassisPen $chassisBrush $dockX $dockY $dockW $dockH 5
                $chassisBrush.Dispose()
                $chassisPen.Dispose()

                $gemMin = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($d.Min))
                $g.FillEllipse($gemMin, ($dockX + 8), ($dockY + 5), 12, 12)
                $gemMin.Dispose()

                $gemMax = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($d.Max))
                $g.FillEllipse($gemMax, ($dockX + 36), ($dockY + 5), 12, 12)
                $gemMax.Dispose()

                $gemClose = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($d.Close))
                $g.FillEllipse($gemClose, ($dockX + 64), ($dockY + 5), 12, 12)
                $gemClose.Dispose()

                $goldGlyph = New-Object System.Drawing.Pen([System.Drawing.Color]::White, 1.4)
                $g.DrawLine($goldGlyph, ($dockX + 11), ($dockY + 11), ($dockX + 17), ($dockY + 11))
                $g.DrawLine($goldGlyph, ($dockX + 39), ($dockY + 13), ($dockX + 45), ($dockY + 9))
                $g.DrawLine($goldGlyph, ($dockX + 67), ($dockY + 9), ($dockX + 73), ($dockY + 13))
                $g.DrawLine($goldGlyph, ($dockX + 73), ($dockY + 9), ($dockX + 67), ($dockY + 13))
                $goldGlyph.Dispose()
            }
        }
    }

    &$renderInnovative ($xLight + $boxW - 90) ($yBox + 11) $false
    &$renderInnovative ($xDark + $boxW - 90) ($yBox + 11) $true

    $yLegend = $yOffset + 242
    $legBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#94A3B8"))
    $textBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#E2E8F0"))
    $ordBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#64748B"))

    $g.DrawString("Innovation:", $fontLabel, $legBrush, ($xOffset + 28), $yLegend)
    $g.DrawString("Aesthetic Style: $($d.Type)", $fontCode, $textBrush, ($xOffset + 120), $yLegend)
    $g.DrawString("Highlights: $($d.Min) / $($d.Max) / $($d.Close)", $fontCode, $textBrush, ($xOffset + 380), $yLegend)
    $g.DrawString("Ergonomic Win32 Titlebar Layout", $fontSub, $ordBrush, ($xOffset + 780), $yLegend)

    $legBrush.Dispose()
    $textBrush.Dispose()
    $ordBrush.Dispose()
}

foreach ($d in $innovativeDesigns) {
    $bmp = New-Object System.Drawing.Bitmap(1160, 290)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

    Draw-Innovative-Card $g $d 0 0

    $outPng = Join-Path $artifactDir ("innovative_window_design_{0:D2}.png" -f $d.Id)
    $bmp.Save($outPng, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bmp.Dispose()
    Write-Host "Created $outPng"
}

$posterW = 1240
$posterH = 2360
$posterBmp = New-Object System.Drawing.Bitmap($posterW, $posterH)
$pg = [System.Drawing.Graphics]::FromImage($posterBmp)
$pg.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$pg.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
$pg.Clear([System.Drawing.ColorTranslator]::FromHtml("#090B0E"))

$pHeadB = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#FFFFFF"))
$pSubB = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml("#94A3B8"))

$pg.DrawString("SumatraPDF Enhanced - Next-Gen Window Controls (Beyond macOS)", $fontPosterHead, $pHeadB, 40, 24)
$pg.DrawString("7 Innovative Concept Palettes Re-imagining Window Management for Modern High-DPI Displays", $fontPosterSub, $pSubB, 40, 58)

$pHeadB.Dispose()
$pSubB.Dispose()

$cardY = 100
foreach ($d in $innovativeDesigns) {
    Draw-Innovative-Card $pg $d 40 $cardY
    $cardY += 315
}

$masterPng = Join-Path $artifactDir "all_7_innovative_window_designs.png"
$posterBmp.Save($masterPng, [System.Drawing.Imaging.ImageFormat]::Png)
$pg.Dispose()
$posterBmp.Dispose()
Write-Host "Created Master Poster: $masterPng"
