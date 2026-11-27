# Roda a coleta completa no Windows e registra informações da máquina.
# Requer gcc (MinGW-w64, ex.: MSYS2 ou winlibs) ou clang no PATH.
# Uso (na raiz do repositório): .\experimentos\run_windows.ps1
$ErrorActionPreference = "Stop"

# Roda a partir da raiz, independente de onde foi chamado
Set-Location (Split-Path -Parent $PSScriptRoot)

New-Item -ItemType Directory -Force -Path dados | Out-Null

# ---------- Compilação ----------
$cc = if (Get-Command gcc -ErrorAction SilentlyContinue) { "gcc" }
      elseif (Get-Command clang -ErrorAction SilentlyContinue) { "clang" }
      else { throw "gcc/clang não encontrado no PATH." }

$flags = @("-O2", "-Wall", "-Wextra", "-Icode")

& $cc @flags code\benchmark.c code\platform_thread.c -o code\benchmark.exe
if ($LASTEXITCODE -ne 0) { throw "Falha na compilação do benchmark" }

& $cc @flags testes\test_platform.c code\platform_thread.c -o code\test_platform.exe
if ($LASTEXITCODE -ne 0) { throw "Falha na compilação do test_platform" }

# ---------- Informações da máquina ----------
$cpu     = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name.Trim()
$nucleos = (Get-CimInstance Win32_Processor |
            Measure-Object NumberOfLogicalProcessors -Sum).Sum
$os      = Get-CimInstance Win32_OperatingSystem
$plano   = (powercfg /getactivescheme) -replace '^.*:\s*', ''   # análogo ao governor

@(
    "data: $(Get-Date -Format o)"
    "sistema: $($os.Caption.Trim()) (versão $($os.Version), build $($os.BuildNumber))"
    "cpu: $cpu"
    "nucleos_logicos: $nucleos"
    "compilador: $((& $cc --version | Select-Object -First 1))"
) | Set-Content -Encoding UTF8 dados\info_maquina_windows.txt

# ---------- Benchmark ----------
.\code\benchmark.exe --reps 30 --out dados\windows_resultados.csv
if ($LASTEXITCODE -ne 0) { throw "Falha no benchmark" }

# ---------- Análise ----------
$py = if (Get-Command python -ErrorAction SilentlyContinue) { "python" } else { "py" }
& $py experimentos\analyze.py dados\windows_resultados.csv --out experimentos
if ($LASTEXITCODE -ne 0) { throw "Falha na análise" }