# Threads e Sincronização — Mutex e Spinlock

Projeto da disciplina **GBC045 – Sistemas Operacionais**, do curso de Bacharelado em Ciência da Computação da Universidade Federal de Uberlândia (UFU).

## 1. Apresentação

O projeto implementa uma **camada de abstração de threads** (`platform_thread.h/.c`), que oferece uma API única para operações de concorrência, utilizando os recursos nativos de cada sistema operacional:

- **Linux/FreeBSD:** biblioteca POSIX Threads (pthreads).
- **Windows:** API nativa Win32.
    

A partir dessa camada, são comparados experimentalmente dois mecanismos de sincronização: **mutex** (`pt_mutex_t`) e **spinlock** (`pt_spinlock_t`).

O benchmark utiliza múltiplas threads para executar um número fixo de aquisições de lock. Em cada aquisição, a thread entra na seção crítica, realiza um trabalho e libera o lock.

São avaliadas as seguintes configurações:

|Variável|Valores|
|---|---|
|Número de threads|1, 2, 4, 8 e 16|
|Duração da seção crítica|Curta, média (~1 µs) e longa (~100 µs)|
|Mecanismo de sincronização|Mutex e spinlock|

Cada configuração é executada **30 vezes**, com uma rodada inicial de aquecimento descartada. São coletados o tempo total de execução e a vazão (aquisições por segundo). Os resultados são processados por um script Python, que calcula estatísticas e gera gráficos.

### Integrantes

- Pedro Tavares Oliveira
- Santiago Vitor Santos do Carmo
- Simão Baptista Sunga
    

## 2. Estrutura do repositório

```
├── README.md
├── requirements.txt
├── video/                    # Vídeo de apresentação
├── code/                     # Implementação e benchmark
│   ├── platform_thread.h
│   ├── platform_thread.c
│   ├── benchmark.c
│   └── Makefile
├── testes/                   # Testes da camada de abstração
├── dados/                    # Resultados experimentais
│   ├── analise_linux/
│   └── analise_windows/
└── experimentos/              # Scripts de execução e análise
    ├── run_all.sh
    ├── run_windows.ps1
    └── analyze.py
```

## 3. Requisitos

|Plataforma|Ferramentas|
|---|---|
|Linux|GCC, Make e Python 3.9+|
|Windows|GCC (MinGW-w64) ou Clang e Python 3.9+|

As dependências Python são instaladas pelo arquivo `requirements.txt`:

```
pip install -r requirements.txt
```

O arquivo contém as bibliotecas necessárias para a análise dos resultados: NumPy, Pandas e Matplotlib.

## 4. Instalação

### 4.1. Clonar o repositório

```
git clone https://github.com/pedrotavol/grupo1.git
cd grupo1
```

### 4.2. Linux

Em distribuições baseadas em Debian ou Ubuntu, instale as ferramentas necessárias:

```
sudo apt install build-essential python3 python3-pip
```

Verifique as instalações:

```
gcc --version
make --version
python3 --version
```

Instale as dependências Python:

```
pip install -r requirements.txt
```

Caso a distribuição restrinja instalações globais com `pip`, utilize um ambiente virtual.

### 4.3. Windows

1. Instale o GCC MinGW-w64, por exemplo, pelo [MSYS2](https://www.msys2.org/) ou [WinLibs](https://winlibs.com/). Também é possível utilizar Clang.
    
2. Adicione o compilador ao `PATH`.
    
3. Instale o Python 3.9 ou superior.
    

No PowerShell, verifique as instalações e instale as dependências:

```
gcc --version
python --version
pip install -r requirements.txt
```

## 5. Execução

Todos os comandos devem ser executados na raiz do repositório.

### 5.1. Testes da camada de abstração

Antes de executar os experimentos, é possível verificar o funcionamento das operações implementadas.

**Linux:**

```
make -C code test
```

**Windows (PowerShell):**

```
gcc -O2 -Wall -Wextra -Icode testes\test_platform.c code\platform_thread.c -o code\test_platform.exe
.\code\test_platform.exe
```

Saída esperada:

```
Todos os testes passaram com sucesso!
```

### 5.2. Execução dos experimentos

Os scripts automatizam a compilação, a coleta de informações da máquina, a execução do benchmark e a análise dos resultados.

**Linux:**

```
bash experimentos/run_all.sh
```

**Windows (PowerShell):**

```
.\experimentos\run_windows.ps1
```

Caso a política de execução do PowerShell bloqueie o script:

```
powershell -ExecutionPolicy Bypass -File .\experimentos\run_windows.ps1
```

Ao final, são gerados:

|Arquivo|Descrição|
|---|---|
|`dados/info_maquina_<plataforma>.txt`|Informações do sistema, processador e compilador|
|`dados/<plataforma>_resultados.csv`|Dados brutos das execuções|
|`experimentos/resumo.csv`|Estatísticas dos experimentos|
|`experimentos/cruzamentos.txt`|Comparações entre mutex e spinlock|
|`experimentos/grafico1_*.png`|Gráficos de vazão por número de threads|
|`experimentos/grafico2_*.png`|Gráficos de comparação entre os mecanismos de sincronização|

**Observação:** os arquivos de análise são sobrescritos a cada execução. Os resultados finais utilizados no projeto foram preservados em `dados/analise_linux/` e `dados/analise_windows/`.

### 5.3. Análise comparativa

Para analisar conjuntamente os resultados obtidos no Linux e no Windows:

```
python3 experimentos/analyze.py dados/analise_linux/resultados_linux.csv dados/analise_windows/resultados_windows.csv --out experimentos
```

## 6. Opções do benchmark

O executável `benchmark` também permite configurar diretamente a execução dos experimentos:

|Opção|Descrição|
|---|---|
|`--reps N`|Define o número de repetições|
|`--out ARQUIVO`|Define o arquivo CSV de saída|
|`--quick`|Executa um teste rápido, com menos aquisições e 3 repetições|
|`--extra`|Adiciona seções críticas de 10 µs e 30 µs|
|`--plataforma NOME`|Define o identificador da plataforma no CSV|

Exemplos:

```
./code/benchmark --reps 30 --out saida.csv
./code/benchmark --quick
./code/benchmark --extra
./code/benchmark --plataforma nome
```
