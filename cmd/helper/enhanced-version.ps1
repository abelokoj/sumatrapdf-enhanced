function Assert-EnhancedVersion([string]$Version) {
    if ($Version -cnotmatch '^[0-9]{4}-[0-9]{2}-[0-9]{2}\.[1-9][0-9]*$') { throw 'Enhanced version must use YYYY-MM-DD.N' }
    $date = [DateTime]::MinValue
    $counter = [UInt16]0
    if (-not [DateTime]::TryParseExact($Version.Substring(0,10), 'yyyy-MM-dd', [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::None, [ref]$date) -or $date.Year -lt 2000 -or -not [UInt16]::TryParse($Version.Substring(11), [ref]$counter)) { throw 'Invalid Enhanced release date or release counter (1 to 65535)' }
}

function Assert-EnhancedReleaseSequence([string]$Version, $Releases) {
    Assert-EnhancedVersion $Version
    $published = @($Releases | Where-Object { -not $_.draft -and $_.tag_name -match '^enhanced-(?:[0-9]{4}-[0-9]{2}-[0-9]{2}\.[1-9][0-9]*|v[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?)$' })
    if (@($published | Where-Object { $_.tag_name -eq "enhanced-$Version" }).Count) { throw 'Published versions are preserved; choose a new version' }
    $lastCounter = $published.Count
    foreach ($release in $published) {
        if ($release.tag_name -match '^enhanced-([0-9]{4}-[0-9]{2}-[0-9]{2}\.[1-9][0-9]*)$') {
            Assert-EnhancedVersion $Matches[1]
            $lastCounter = [Math]::Max($lastCounter, [int]$release.tag_name.Split('.')[-1])
        }
    }
    if ([int]$Version.Split('.')[-1] -ne $lastCounter + 1) { throw "Enhanced release counter must be $($lastCounter + 1), following every published app release" }
}
