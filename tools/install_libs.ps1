# tools\install_libs.ps1
# TeensyMaestro Community Edition vendor libraries -> Arduino\libraries junction linker
# Creates NTFS directory junctions (no file copies)

param(
  [string]$Prefix = "_junction_"   # Visible prefix so you don't mistake it for a real folder
)

$Root        = "$env:USERPROFILE\dev\TeensyMaestro"
$Vendor      = Join-Path $Root 'vendor-libraries'
$ArduinoLibs = Join-Path $env:USERPROFILE 'Arduino\libraries'

Write-Host ""
Write-Host "===== TeensyMaestro vendor-libraries JUNCTION installer ====="
Write-Host "Vendor:   $Vendor"
Write-Host "Arduino:  $ArduinoLibs"
Write-Host "Prefix:   $Prefix"
Write-Host ""

if (!(Test-Path $Vendor)) { throw "Vendor folder not found: $Vendor" }
if (!(Test-Path $ArduinoLibs)) {
  Write-Host "Creating Arduino libraries folder: $ArduinoLibs"
  New-Item -ItemType Directory -Force -Path $ArduinoLibs | Out-Null
}

# Helper: ensure minimal library.properties exists in source vendor lib (Arduino builder expects it)
function Ensure-LibraryProperties {
  param([string]$LibPath, [string]$LibFolderName)
  $prop = Join-Path $LibPath 'library.properties'
  if (Test-Path $prop) { return }
  $safeName = "$LibFolderName-TNXQSO"
@"
name=$safeName
version=0.0.0
author=TNX QSO
maintainer=TNX QSO
sentence=Vendored library for TeensyMaestro, linked via junctions
paragraph=Auto-generated library.properties, edit as needed
category=Communication
url=https://github.com/tnxqso
architectures=*
"@ | Set-Content -NoNewline $prop -Encoding UTF8
  Write-Host "  Created minimal library.properties in source"
}

# Remove previous prefixed items to avoid stale links
Get-ChildItem -Path $ArduinoLibs -Directory |
  Where-Object { $_.Name.StartsWith($Prefix, [System.StringComparison]::OrdinalIgnoreCase) } |
  ForEach-Object {
    $target = $_.FullName
    try {
      $isJunction = ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
      if ($isJunction) {
        Write-Host "Removing previous junction: $target"
      } else {
        Write-Warning "Existing prefixed item is not a junction; removing directory: $target"
      }
      Remove-Item -Recurse -Force -LiteralPath $target
    } catch {
      Write-Warning "Failed to remove $target : $($_.Exception.Message)"
    }
  }

# Create junctions for all vendor libraries
$created = @()
Get-ChildItem -Path $Vendor -Directory | ForEach-Object {
  $src  = $_.FullName
  $name = $_.Name
  $dstName = "$Prefix$name"
  $dst  = Join-Path $ArduinoLibs $dstName

  Write-Host ""
  Write-Host "- Processing $name"
  Ensure-LibraryProperties -LibPath $src -LibFolderName $name

  if (Test-Path -LiteralPath $dst) {
    Write-Host "  Removing existing destination: $dst"
    Remove-Item -Recurse -Force -LiteralPath $dst
  }

  Write-Host "  Creating junction:"
  Write-Host "    Target: $src"
  Write-Host "    Link:   $dst"

  try {
    New-Item -ItemType Junction -Path $dst -Target $src | Out-Null
  } catch {
    throw "Failed to create junction for $name -> $($_.Exception.Message)"
  }

  # Verify: must be a junction and directory listing must succeed
  try {
    $link = Get-Item -LiteralPath $dst -Force
    $isJunction = ($link.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
    if (-not $isJunction) { throw "Link exists but is not a junction." }

    # Attempt to enumerate (will throw if target missing/inaccessible)
    Get-ChildItem -LiteralPath $dst -Force | Out-Null

    $created += [PSCustomObject]@{ Name = $dstName; Link = $dst; Target = $src }
    Write-Host "  OK"
  } catch {
    Write-Warning "  Junction verification failed for $dst : $($_.Exception.Message)"
  }
}

Write-Host ""
Write-Host "Created junctions:"
$created | Format-Table Name, Link, Target -AutoSize

Write-Host ""
Write-Host "Tips:"
Write-Host "  * In File Explorer, junctions look like folders; the prefix '$Prefix' keeps them obvious."
Write-Host "  * To remove all created junctions later, delete the prefixed folders under Arduino\\libraries."
Write-Host "  * No admin required for directory junctions on NTFS."
Write-Host ""
Write-Host "Done."
