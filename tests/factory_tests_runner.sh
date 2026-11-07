#!/bin/bash
set -e

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

echo "Validando ambiente..."
if ! command -v cmake &> /dev/null; then
    echo "Erro: cmake não encontrado. Instale CMake." >&2
    exit 1
fi

if ! command -v make &> /dev/null; then
    echo "Erro: make não encontrado. Instale GNU Make." >&2
    exit 1
fi

if ! command -v python3 &> /dev/null; then
    echo "Erro: python3 não encontrado. Instale Python 3." >&2
    exit 1
fi

# Configurar build se necessário
if [ ! -f "CMakeCache.txt" ]; then
    echo "Configurando CMake..."
    cmake .
fi

# Compilar se necessário
if [ ! -f "src/sound_factory.cpp.o" ]; then
    echo "Compilando..."
    make -j$(nproc)
fi

# Executar testes
echo "Executando testes..."
python3 tests/factory_tests_runner.py
