param([Parameter(Mandatory=$true)][string]$DeviceProject)
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath $DeviceProject).Path
$name='Sample_Project_RH850_S1_CAN_Transfer'
$mot=Join-Path $root ('DefaultBuild\'+$name+'.mot')
$map=Get-Content -LiteralPath (Join-Path $root ('DefaultBuild\'+$name+'.map')) -Raw
$memory=New-Object 'Collections.Generic.Dictionary[uint32,byte]'
foreach($line in Get-Content -LiteralPath $mot){
    if($line -notmatch '^S([0-9])([0-9A-Fa-f]+)$'){throw 'Invalid S-record syntax.'}
    $type=[int]$Matches[1];$hex=$Matches[2]
    if($hex.Length%2){throw 'Odd S-record length.'}
    $bytes=New-Object byte[] ($hex.Length/2)
    $sum=0
    for($i=0;$i -lt $bytes.Length;$i++){$bytes[$i]=[Convert]::ToByte($hex.Substring($i*2,2),16);$sum+=$bytes[$i]}
    if($bytes[0] -ne $bytes.Length-1 -or ($sum -band 255) -ne 255){throw 'Invalid S-record checksum/count.'}
    if($type -notin @(1,2,3)){continue}
    $addressBytes=$type+1
    [uint32]$address=0
    for($i=1;$i -le $addressBytes;$i++){$address=($address -shl 8) -bor $bytes[$i]}
    for($i=$addressBytes+1;$i -lt $bytes.Length-1;$i++){
        if($address -ge 0x100000){throw 'Programmer image exceeds R7F701684 Code Flash.'}
        if($memory.ContainsKey($address)){throw 'Overlapping S-record data.'}
        $memory.Add($address,$bytes[$i]);$address++
    }
}
function Symbol([string]$name){
    $match=[regex]::Match($map,('(?m)^  _'+[regex]::Escape($name)+'\r?\n\s+([0-9a-fA-F]{8})\s'))
    if(!$match.Success){throw "Missing linked symbol: $name"}
    return [Convert]::ToUInt32($match.Groups[1].Value,16)
}
$mapping=@{23='rs_canfd_rx_fifo_interrupt';34='r_Config_UART0_interrupt_send';35='r_Config_UART0_interrupt_receive';36='r_Config_UART0_interrupt_error';80='r_Config_TAUJ0_0_interrupt';84='r_Config_OSTM0_interrupt'}
$default=Symbol 'Device_UnhandledIsr'
if((Symbol 'stacktop') -gt [Convert]::ToUInt32('FEDF4800',16)){
    throw 'Transfer RAM/stack collides with debugger reserved area.'
}
for($slot=0;$slot -lt 512;$slot++){
    $at=[uint32](0x200+$slot*4)
    [uint32]$value=0
    for($b=0;$b -lt 4;$b++){
        $pos=[uint32]($at+$b)
        if(!$memory.ContainsKey($pos)){throw "Missing vector byte: $pos"}
        $value=$value -bor ([uint32]$memory[$pos] -shl ($b*8))
    }
    $expected=if($mapping.ContainsKey($slot)){Symbol $mapping[$slot]}else{$default}
    if($value -ne $expected -or $value -lt 0x1000 -or !$memory.ContainsKey($value)){
        throw "Actual MOT / map / vector mismatch at channel $slot"
    }
}
$projection=Get-Content -LiteralPath (Join-Path $root 'generated_projection\device_vectors.c') -Raw
if($projection -match '(?i)(?<![A-Za-z])[A-Z]:[\\/]|/Users/|/home/'){throw 'Generated source contains a local absolute path.'}
$reset=Get-Content -LiteralPath (Join-Path $root 'boot.asm') -Raw
if($reset -notmatch 'LOCAL_RAM_ADDR\s+\.set\s+0xfede0000' -or
   $reset -notmatch 'LOCAL_RAM_END\s+\.set\s+0xfee00000'){throw 'Unexpected RH850 RAM ECC startup extent.'}
$maximum=[uint32]($memory.Keys | Measure-Object -Maximum).Maximum
Write-Host ('PASS: S-record checksum/range,512 actual vector destinations,IRQ23/84,RAM extent and generated comments. ROM high=0x{0:X8}.' -f $maximum)
