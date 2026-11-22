
$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path dados | Out-Null

# Compilação com MSVC
# Executar em um Developer PowerShell for Visual Studio

cl /nologo /O2 /W4 /Icode code\*.c /Fe:code\benchmark.exe

if ($LASTEXITCODE -ne 0) {
    throw "Falha na compilação"
}

# Informações da máquina
$cpu = Get-CimInstance Win32_Processor |
    Select-Object -First 1 -ExpandProperty Name

$nucleos = (Get-CimInstance Win32_Processor |
    Measure-Object NumberOfLogicalProcessors -Sum).Sum

$info = @(
    "data: $(Get-Date -Format o)"
    "sistema: $(Get-CimInstance Win32_OperatingSystem | Select-Object -ExpandProperty Caption)"
    "cpu: $cpu"
    "nucleos_logicos: $nucleos"
    "compilador: Microsoft C/C++ Optimizing Compiler 19.52.37029 (x64)"
)

$info | Set-Content dados/info_maquina_windows.txt

# Execução do benchmark
.\code\benchmark.exe --reps 30 --out dados/windows_resultados.csv

if ($LASTEXITCODE -ne 0) {
    throw "Falha no benchmark"
}

# Análise dos resultados
python experimentos/analyze.py dados/windows_resultados.csv --out experimentos

if ($LASTEXITCODE -ne 0) {
    throw "Falha na análise"
}
