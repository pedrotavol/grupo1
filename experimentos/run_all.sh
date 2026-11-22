#!/usr/bin/env bash
# Roda a coleta completa no Linux e registra informações da máquina.
# Uso (na raiz do repositório):  bash experimentos/run_all.sh
set -e
mkdir -p dados
make -C code
{
  echo "data: $(date -Is)"
  echo "kernel: $(uname -srv)"
  echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2)"
  echo "nucleos_logicos: $(nproc)"
  echo "compilador: $(gcc --version | head -1)"
  echo "governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo n/d)"
} > dados/info_maquina_linux.txt
./code/benchmark --reps 30 --out dados/linux_resultados.csv
python3 experimentos/analyze.py dados/linux_resultados.csv --out experimentos
