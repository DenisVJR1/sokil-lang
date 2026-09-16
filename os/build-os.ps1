# build-os.ps1 — збірка OS ISO (16/32/64) для Сокола
$gcc = "C:\Users\voits\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
$as = "$gcc\as.exe"; $ld = "$gcc\ld.exe"; $oc = "$gcc\objcopy.exe"; $g = "$gcc\gcc.exe"
Set-Location "$PSScriptRoot"

Write-Host "=== Збірка Сокіл OS ==="

# 1. Зберігаємо бут-сектор (sp江淮 loader)
& $as -o loader.o loader.asm 2>&1
& $oc -O binary -j .text loader.o loader.bin 2>&1
$lb = [System.IO.File]::ReadAllBytes("loader.bin")
if ($lb.Length -ne 512) { Write-Host "loader: $($lb.Length) байт (має 512)"; exit 1 }
if ($lb[510] -ne 0x55 -or $lb[511] -ne 0xAA) { Write-Host "loader: немає 0x55AA"; exit 1 }
Write-Host "  loader: OK ($($lb.Length) bytes, 0x55AA)"

# 2. Зберігаємо stage2 для кожної архітектури
foreach ($arch in @("16","32","64")) {
    $asm = "s2_$arch.asm"
    & $as -o "s2_${arch}.o" $asm 2>&1
    & $oc -O binary -j .text "s2_${arch}.o" "s2_${arch}.bin" 2>&1
    $sb = [System.IO.File]::ReadAllBytes("s2_${arch}.bin")
    Write-Host "  s2_$arch : $($sb.Length) bytes"
}

# 3. Збираємо mkiso.exe
& $g -O2 -std=c99 -static -o mkiso.exe mkiso.c 2>&1
Write-Host "  mkiso.exe: OK"

# 4. Для кожної архітектури: floppy.img -> ISO
foreach ($arch in @("16","32","64")) {
    $floppy = New-Object byte[] 1474560   # 1.44MB
    $loader = [System.IO.File]::ReadAllBytes("loader.bin")
    $s2 = [System.IO.File]::ReadAllBytes("s2_$arch.bin")
    [System.Array]::Copy($loader, 0, $floppy, 0, 512)
    [System.Array]::Copy($s2, 0, $floppy, 512, [Math]::Min($s2.Length, 1536))
    $imgPath = "sokil$arch.img"
    $isoPath = "sokil$arch.iso"
    [System.IO.File]::WriteAllBytes($imgPath, $floppy)
    & .\mkiso.exe $isoPath $imgPath 2>&1
    Write-Host "  $isoPath : OK"
}

Write-Host ""
Write-Host "=== Готово! ==="
Write-Host "  Запуск: qemu-system-i386.exe -cdrom sokil16.iso"
Write-Host "          qemu-system-i386.exe -cdrom sokil32.iso"
Write-Host "          qemu-system-x86_64.exe -cdrom sokil64.iso"