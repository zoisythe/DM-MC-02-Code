# 双击 M2006一键自检.bat 使用。通过 ST-Link 读取整车固件的状态，保持 VRA 回中。
param([switch]$Can2)

$ErrorActionPreference = 'Stop'
$programmer = 'D:\app\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe'
$toolBin = 'D:\app\arm-gnu-toolchain-15.2.rel1\bin'
$elf = Join-Path $PSScriptRoot 'build\firmware.elf'
$testName = if ($Can2) { 'CAN2五电调自检' } else { 'M2006自检' }
$durationMs = if ($Can2) { 15000 } else { 10000 }
$outputDir = Join-Path $PSScriptRoot "build\$testName"
$started = [DateTimeOffset]::UtcNow.ToOffset([TimeSpan]::FromHours(8))
$reportPath = Join-Path $outputDir ('{0}_{1:yyyyMMdd_HHmmss_fff}_{2}.txt' -f $testName, $started, $PID)
$scratch = Join-Path $outputDir ([IO.Path]::GetFileNameWithoutExtension($reportPath))
$samples = [Collections.Generic.List[object]]::new()
$report = [Collections.Generic.List[string]]::new()
$report.Add("$testName 静态报告")
$report.Add("开始时间（北京时间） $($started.ToString('yyyy-MM-dd HH:mm:ss zzz'))")
if ($Can2) {
    $report.Add('配置 CAN2 / P3，经典 CAN 1 Mbps。C620 ID 1–4 对应 0x201–0x204，C610 ID 5 对应 0x205。')
    $report.Add('通过 ST-Link 只读采集固件每路累计反馈计数，目标采集时长 15 秒。')
} else {
    $report.Add('配置 经典 CAN 1 Mbps，C610 ID 5，反馈帧 0x205。通过 ST-Link 热插拔只读检查整车固件。')
}
$report.Add('检查范围为 CAN 反馈和静态遥测。电机带载能力、动力线和减速箱仍需通过单独的运动测试判断。')

function Invoke-SwdRead {
    param([string[]]$ReadArguments)
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $programmer
    $info.WorkingDirectory = $scratch
    $info.Arguments = '-c port=SWD mode=HOTPLUG freq=1000 ' + ($ReadArguments -join ' ')
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($info)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(15000)) {
            $process.Kill()
            throw 'ST-Link 单次读取超过 15 秒。请检查主板供电和 SWD 接线。'
        }
        $output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0 -or $output -match '(?im)^\s*Error\s*:') {
            throw "ST-Link 读取失败（退出码 $($process.ExitCode)）。`r`n$($output.Trim())"
        }
    } finally {
        $process.Dispose()
    }
}

function Get-ElfSymbol {
    param([string]$Name, [string]$Type, [int]$ExpectedSize = 0)
    $match = [regex]::Match($symbols, "(?m)^([0-9a-fA-F]{8}) ([0-9a-fA-F]{8}) [$Type] $Name$")
    if (-not $match.Success) { throw "本地 ELF 中找不到 $Name 符号。" }
    $size = [Convert]::ToInt32($match.Groups[2].Value, 16)
    if ($ExpectedSize -gt 0 -and $size -ne $ExpectedSize) {
        throw "本地 ELF 的 $Name 结构大小不受支持。"
    }
    [pscustomobject]@{ Address = [Convert]::ToInt32($match.Groups[1].Value, 16); Size = $size }
}

function Find-M2006Code {
    param([byte[]]$LocalImage, [byte[]]$BoardImage, [int]$MotorAddress)
    # GCC 的 Thumb 代码段包含 RAM 地址字面量。要求接收、故障处理和轮询的整个连续代码段一致。
    $first = Get-ElfSymbol check_feedback_timeout Tt
    $last = Get-ElfSymbol m2006_control_poll Tt
    $offset = $first.Address - 0x08000000
    $length = $last.Address - $first.Address + $last.Size
    if ($offset -lt 0 -or $length -lt 128 -or $offset + $length -gt $LocalImage.Length) { return $false }
    $signature = [BitConverter]::ToString($LocalImage, $offset, $length)
    if (-not $signature.Contains([BitConverter]::ToString([BitConverter]::GetBytes($MotorAddress)))) { return $false }
    $boardCode = [BitConverter]::ToString($BoardImage)
    $position = $boardCode.IndexOf($signature, [StringComparison]::Ordinal)
    return $position -ge 0 -and $position % 6 -eq 0 -and
        $boardCode.IndexOf($signature, $position + 1, [StringComparison]::Ordinal) -lt 0
}

function Read-Snapshot {
    param([string]$Name, [int]$Length)
    $path = Join-Path $scratch $Name
    if (Test-Path -LiteralPath $path) {
        $data = [IO.File]::ReadAllBytes($path)
        Remove-Item -LiteralPath $path
        if ($data.Length -eq $Length) { return ,$data }
    }
    throw "ST-Link 返回的 $Name 不完整。预期读取 $Length 字节。"
}

function Get-Diagnosis {
    $first = $samples[0]
    $last = $samples[$samples.Count - 1]
    for ($i = 1; $i -lt $samples.Count; ++$i) {
        $previous = $samples[$i - 1]
        $current = $samples[$i]
        if ($current.UpMs -lt $previous.UpMs -or $current.Frames -lt $previous.Frames -or
            $current.OtherFrames -lt $previous.OtherFrames -or $current.TxTimeouts -lt $previous.TxTimeouts) {
            return '采集期间计数回退，可能发生了主板重启或计数回绕。请重测。'
        }
    }
    if ($last.UpMs - $first.UpMs -lt 8000) {
        return '10 秒内主板时基推进不足，无法评估反馈。请检查固件运行状态。'
    }
    $frames = $last.Frames - $first.Frames
    $report.Add("采集期间新增 C610 反馈 $frames 帧。")
    $otherFrames = $null
    if ($null -ne $last.OtherFrames) {
        $otherFrames = $last.OtherFrames - $first.OtherFrames
        $report.Add("四轮 C620 反馈共新增 $otherFrames 帧。")
    }
    if ($frames -eq 0) {
        if ($null -eq $otherFrames) {
            return '未捕获 C610 ID 5 的新增反馈。请检查 C610 供电、CAN 接线、ID 和电机反馈插头。'
        }
        if ($otherFrames -gt 0) {
            return '总线上有四轮反馈，但未捕获 ID 5 反馈。先检查 C610 的供电、CAN 接线、ID 和电机反馈插头。'
        }
        return '两类电调均无新增反馈。先检查电调供电、CAN 接线和终端。'
    }
    if ($samples | Where-Object { $_.FeedbackFault -ne 0 -or $_.TxTimeouts -ne 0 }) {
        return '收到 C610 反馈，同时发现反馈锁止或累计发送超时。请结合下方采样判断故障来源。'
    }
    if ($null -ne $otherFrames -and ($samples | Where-Object { ($_.Psr -band 0xA0) -ne 0 })) {
        return '收到 C610 反馈，但采样期间 CAN 状态出现错误被动或总线关闭。'
    }
    for ($i = 1; $i -lt $samples.Count; ++$i) {
        if ($samples[$i].Frames -eq $samples[$i - 1].Frames) {
            return '收到 C610 反馈，但部分采样间隔内反馈计数没有增加。请检查连接是否间歇中断。'
        }
    }
    if ($null -eq $otherFrames) { return 'C610 反馈持续增加，采样期间未见 M2006 反馈锁止或发送超时。' }
    return 'C610 反馈持续增加，采样期间未见反馈锁止、发送超时或 CAN 总线异常。'
}

function Get-Can2Diagnosis {
    $first = $samples[0]
    $last = $samples[$samples.Count - 1]
    for ($i = 1; $i -lt $samples.Count; ++$i) {
        if ($samples[$i].UpMs -lt $samples[$i - 1].UpMs) {
            return '采集期间主板时基回退，可能发生了重启或计数回绕。请重测。'
        }
    }
    $seconds = ($last.UpMs - $first.UpMs) / 1000.0
    if ($seconds -lt 13) { return '采集期间主板时基推进不足，无法评估五路反馈。' }
    $report.Add(('主板采集跨度 {0:F2} 秒，共 {1} 次采样。' -f $seconds, $samples.Count))
    $devices = @('C620 左前', 'C620 右前', 'C620 左后', 'C620 右后', 'C610 丝杆')
    $received = $continuous = 0
    for ($id = 1; $id -le $devices.Count; ++$id) {
        $field = if ($id -eq 5) { 'Frames' } else { "FramesId$id" }
        $label = 'ID {0} · 0x{1:X3} · {2}' -f $id, (0x200 + $id), $devices[$id - 1]
        $stalled = $reversed = 0
        for ($i = 1; $i -lt $samples.Count; ++$i) {
            $delta = $samples[$i].$field - $samples[$i - 1].$field
            if ($delta -eq 0) { ++$stalled }
            if ($delta -lt 0) { ++$reversed }
        }
        if ($reversed -gt 0) {
            $report.Add("$label | 计数回退，本路结果无效")
            continue
        }
        $frames = $last.$field - $first.$field
        $status = '未收到新增反馈'
        if ($frames -gt 0) {
            ++$received
            if ($stalled -eq 0) {
                ++$continuous
                $status = '各采样间隔均收到反馈'
            } else {
                $status = "收到反馈，$stalled 个采样间隔内计数未增加"
            }
        }
        $report.Add(('{0} | {1} 帧 | {2:F1} 帧/秒 | {3}' -f $label, $frames, ($frames / $seconds), $status))
    }
    if ($samples | Where-Object { ($_.Psr -band 0xA0) -ne 0 }) {
        $report.Add('采样期间曾观察到 CAN 错误被动或总线关闭。')
    }
    if ($samples | Where-Object { $_.FeedbackFault -ne 0 -or $_.TxTimeouts -ne 0 }) {
        $report.Add('采样中看到丝杆反馈锁止或上电以来累计发送超时，详见原始值。')
    }
    return "五路中 $received 路收到新增反馈，其中 $continuous 路在每个采样间隔内均收到反馈。"
}

$exitCode = 1
try {
    New-Item -ItemType Directory -Path $scratch -Force | Out-Null
    if ($Can2) {
        Write-Host 'CAN2 五电调静态自检。请保持底盘摇杆和 VRA 回中，接好 ST-Link，并给主板和五个电调供电。'
    } else {
        Write-Host 'M2006 静态自检。请让 VRA 保持回中，主板和 C610 正常供电，ST-Link 已连接。'
    }
    foreach ($path in @($programmer, "$toolBin\arm-none-eabi-nm.exe", "$toolBin\arm-none-eabi-objcopy.exe", $elf)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "找不到 $path" }
    }
    $localImagePath = Join-Path $scratch 'local.bin'
    & "$toolBin\arm-none-eabi-objcopy.exe" -O binary $elf $localImagePath
    if ($LASTEXITCODE -ne 0) { throw '无法从本地 ELF 导出固件数据。' }
    $localImage = [IO.File]::ReadAllBytes($localImagePath)
    $flashReadLength = 128KB
    if ($localImage.Length -eq 0 -or $localImage.Length -gt $flashReadLength) { throw '本地 ELF 的固件长度无效。' }
    Write-Host '正在核对板上固件与本地 ELF……'
    Invoke-SwdRead @('-u', '0x08000000', ('0x{0:X}' -f $flashReadLength), 'flash.bin')
    $boardImage = Read-Snapshot 'flash.bin' $flashReadLength
    $exactMatch = [BitConverter]::ToString($boardImage, 0, $localImage.Length) -ceq [BitConverter]::ToString($localImage)
    if (-not $exactMatch) {
        $archives = Get-ChildItem -LiteralPath (Split-Path -Parent $elf) -Directory -Filter 'last-flashed-*' | Sort-Object Name -Descending
        foreach ($archive in $archives) {
            $archivedElf = Join-Path $archive.FullName 'firmware.elf'
            if (-not (Test-Path -LiteralPath $archivedElf -PathType Leaf)) { continue }
            $archivedImagePath = Join-Path $scratch 'archived.bin'
            & "$toolBin\arm-none-eabi-objcopy.exe" -O binary $archivedElf $archivedImagePath
            if ($LASTEXITCODE -ne 0) { continue }
            $archivedImage = [IO.File]::ReadAllBytes($archivedImagePath)
            if ($archivedImage.Length -eq 0 -or $archivedImage.Length -gt $boardImage.Length) { continue }
            if ([BitConverter]::ToString($boardImage, 0, $archivedImage.Length) -ceq [BitConverter]::ToString($archivedImage)) {
                $elf = $archivedElf
                $localImagePath = $archivedImagePath
                $exactMatch = $true
                break
            }
        }
    }
    $symbols = (& "$toolBin\arm-none-eabi-nm.exe" -S $elf) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw '无法读取本地 ELF 符号。' }
    $motorBase = (Get-ElfSymbol m2006_status BbDd 52).Address
    $chassisBase = $null
    if ($exactMatch) {
        $chassisBase = (Get-ElfSymbol chassis_status BbDd 1056).Address
        $localHash = (Get-FileHash -LiteralPath $localImagePath -Algorithm SHA256).Hash
        $report.Add("板上固件已匹配本地 ELF。SHA-256 $localHash")
        $report.Add("匹配的 ELF $elf")
    } elseif (-not $Can2 -and (Find-M2006Code $localImage $boardImage $motorBase)) {
        $report.Add('整车固件有差异，板上的 M2006 反馈及轮询机器码与本地 ELF 完全匹配，已核实其状态地址。底盘遥测字段留空。')
    } else {
        $dumpPath = [IO.Path]::ChangeExtension($reportPath, '.bin')
        [IO.File]::WriteAllBytes($dumpPath, $boardImage)
        $boardHash = (Get-FileHash -LiteralPath $dumpPath -Algorithm SHA256).Hash
        $report.Add("板上首个 Flash 扇区的 SHA-256 $boardHash")
        $report.Add("供进一步识别版本的只读固件副本 $dumpPath")
        throw '板上遥测地址无法确认。请把本次报告和同名 .bin 文件一起发来。'
    }
    $snapshotBase = $motorBase
    $snapshotEnd = $motorBase + 52
    if ($null -ne $chassisBase) {
        $snapshotBase = [Math]::Min($motorBase, $chassisBase)
        $snapshotEnd = [Math]::Max($snapshotEnd, $chassisBase + 32)
    }
    if ($Can2) {
        $wheelBase = (Get-ElfSymbol motors BbDd 144).Address
        $snapshotBase = [Math]::Min($snapshotBase, $wheelBase)
        $snapshotEnd = [Math]::Max($snapshotEnd, $wheelBase + 144)
        # CCCR 和 NBTP 可只读确认 FDCAN2 已启动并使用整车的 1 Mbps 时序。
        Invoke-SwdRead @('-u', '0x4000A418', '0x8', 'can2.bin')
        $config = Read-Snapshot 'can2.bin' 8
        $cccr = [BitConverter]::ToUInt32($config, 0)
        $nbtp = [BitConverter]::ToUInt32($config, 4)
        $report.Add(('FDCAN2 启动检查 CCCR=0x{0:X8}，NBTP=0x{1:X8}。' -f $cccr, $nbtp))
        if ($nbtp -ne 0x02030C01) {
            throw 'FDCAN2 的时序与 CAN2 整车固件不符。请核对当前烧录版本和主板运行状态。'
        }
        if (($cccr -band 1) -ne 0) {
            $report.Add('采集开始前 FDCAN2 处于初始化状态，继续采样以观察总线是否恢复。')
        }
    }
    $snapshotLength = $snapshotEnd - $snapshotBase
    if ($snapshotBase -lt 0x20000000 -or $snapshotEnd -gt 0x20010000 -or $snapshotLength -gt 4096) {
        throw '本地 ELF 的遥测地址超出支持的 DTCM RAM 范围。'
    }
    $snapshotArguments = @('-u', ('0x{0:X8}' -f $snapshotBase), ('0x{0:X}' -f $snapshotLength), 'snapshot.bin')
    $report.Add(('M2006 状态地址 0x{0:X8}。' -f $motorBase))
    Write-Host ('开始约 {0} 秒采集……' -f ($durationMs / 1000))
    $clock = [Diagnostics.Stopwatch]::StartNew()
    do {
        $sampleMs = $clock.ElapsedMilliseconds
        Invoke-SwdRead $snapshotArguments
        $data = Read-Snapshot 'snapshot.bin' $snapshotLength
        $motor = $motorBase - $snapshotBase
        $otherFrames = $psr = $ecr = $null
        if ($null -ne $chassisBase) {
            $chassis = $chassisBase - $snapshotBase
            $otherFrames = [BitConverter]::ToUInt32($data, $chassis + 16)
            $psr = [BitConverter]::ToUInt32($data, $chassis + 24)
            $ecr = [BitConverter]::ToUInt32($data, $chassis + 28)
        }
        $sample = [ordered]@{
            SampleMs      = $sampleMs
            UpMs          = [BitConverter]::ToUInt32($data, $motor)
            Frames        = [BitConverter]::ToUInt32($data, $motor + 4)
            LastFrameMs   = [BitConverter]::ToUInt32($data, $motor + 8)
            TxRequested   = [BitConverter]::ToUInt32($data, $motor + 12)
            TxCompleted   = [BitConverter]::ToUInt32($data, $motor + 16)
            TxTimeouts    = [BitConverter]::ToUInt32($data, $motor + 20)
            FeedbackFault = [BitConverter]::ToUInt32($data, $motor + 24)
            RequestedRpm  = [BitConverter]::ToInt32($data, $motor + 28)
            TargetRpm     = [BitConverter]::ToInt32($data, $motor + 32)
            RotorRpm      = [BitConverter]::ToInt32($data, $motor + 36)
            CurrentRaw    = [BitConverter]::ToInt32($data, $motor + 40)
            CommandRaw    = [BitConverter]::ToInt32($data, $motor + 44)
            OtherFrames   = $otherFrames
            Psr           = $psr
            Ecr           = $ecr
        }
        if ($Can2) {
            for ($id = 1; $id -le 4; ++$id) {
                $offset = $wheelBase - $snapshotBase + 36 * ($id - 1)
                $sample["FramesId$id"] = [BitConverter]::ToUInt32($data, $offset)
                $sample["LastFrameMsId$id"] = [BitConverter]::ToUInt32($data, $offset + 4)
            }
        }
        $samples.Add([pscustomobject]$sample)
        if ($sampleMs -lt $durationMs) {
            $delay = [Math]::Min(1000 - ($clock.ElapsedMilliseconds - $sampleMs), $durationMs - $clock.ElapsedMilliseconds)
            if ($delay -gt 0) { Start-Sleep -Milliseconds $delay }
        }
    } while ($sampleMs -lt $durationMs)
    $result = if ($Can2) { Get-Can2Diagnosis } else { Get-Diagnosis }
    $exitCode = 0
} catch {
    $result = "自检未完成。$($_.Exception.Message)"
} finally {
    $report.Add("结论 $result")
    if ($samples.Count -gt 0) {
        $report.Add('')
        $report.Add('以下为采样数据。时间单位 ms，转速为转子 rpm，电流为协议原始值，PSR/ECR 为 CAN 状态和错误计数寄存器的十进制值。')
        $report.Add('UpMs 为上电时间。Frames、OtherFrames、TxRequested、TxCompleted 和 TxTimeouts 均为上电以来累计值。')
        if ($Can2) { $report.Add('Frames 为 ID 5 累计反馈，FramesId1 至 FramesId4 为四轮各自的累计反馈。') }
        foreach ($line in ($samples | ConvertTo-Csv -NoTypeInformation)) { $report.Add($line) }
    }
    [IO.File]::WriteAllLines($reportPath, $report, [Text.UTF8Encoding]::new($true))
    foreach ($name in @('local.bin', 'archived.bin', 'flash.bin', 'can2.bin', 'snapshot.bin')) {
        Remove-Item -LiteralPath (Join-Path $scratch $name) -ErrorAction SilentlyContinue
    }
    if ((Test-Path -LiteralPath $scratch) -and -not (Get-ChildItem -LiteralPath $scratch -Force)) {
        Remove-Item -LiteralPath $scratch
    }
    Write-Host "`n$result`n报告 $reportPath"
}
exit $exitCode
