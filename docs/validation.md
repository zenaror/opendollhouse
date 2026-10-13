# Primeiro checkpoint de desenvolvimento — 2026-10-13

## Escopo e ambiente

Protótipo C++17 de leitura estrutural, sem VM de execução, pixels decodificados,
renderer, áudio ou alteração da instalação original. Ferramentas usadas no Linux:
GCC 13.3.0, CMake 3.28.3 e Python 3.12.3; nenhuma dependência instalada para a tarefa.

## Testes sintéticos

CTest inclui o programa de formatos e integração da CLI. O programa de formatos
executa 317 checks, incluindo offsets de subviews, campos endian, versões BHAV,
operandos preservados, desvios inválidos, truncamento em todos os pontos das
fixtures FAR/BHAV/SPR2, comprimentos impossíveis e limites antes de alocação.
A CLI é verificada para corpus vazio, input malformado misturado a válido,
diagnóstico de origem, exclusão dos contadores do recurso que falhou e JSON válido.

Os checks passaram em build Debug e com AddressSanitizer/UBSan. LeakSanitizer
falhou no sandbox por incompatibilidade com ptrace; o teste de vazamentos não foi
concluído. O perfil sanitizado foi executado com `detect_leaks=0`, mantendo ASan
e UBSan ativos. Não interpretar essa configuração como ausência comprovada de leaks.

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DODH_SANITIZERS=ON
cmake --build build-sanitize -j 2
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-sanitize --output-on-failure
ASAN_OPTIONS=detect_leaks=0 ./build-sanitize/odh-inspect '../referencias/The Sims' > reports/corpus-sanitize.json
python3 tools/check_census.py reports/corpus-sanitize.json
```

## Corpus local

O inspector nativo reproduziu os 14 contadores do levantamento independente
anterior em scripts Python inline, com zero erros nos checks implementados.
Também percorreu o corpus com ASan/UBSan, sem diagnósticos nessa execução.

| Contador | Observado |
|---|---:|
| Arquivos selecionados | 1.089 |
| FAR / entradas | 90 / 43.723 |
| Nomes normalizados repetidos entre FARs | 115 |
| Containers IFF | 3.570 |
| BHAV / instruções | 28.778 / 401.238 |
| Primitivas distintas, por opcode | 49 |
| Frames SPR2 | 143.487 |
| Frames com flag Z | 142.707 |
| Frames com flag alpha | 135.582 |

O manifesto fingerprint seleciona as mesmas classes de extensão da CLI: FAR,
IFF, STX, SPF, WLL e FLR. Total: 2.710.890.759 bytes. SHA-256 do manifesto
canônico de paths relativos, tamanhos e hashes:

```text
174db55df3257fca3107a5269faabd143fc3d2b9025164fe9076175bfa2ff667
```

O JSON completo fica em `reports/input-manifest.json`, ignorado pelo Git. O
encoding canônico está definido em `tools/fingerprint.py` e no próprio manifesto.
O fingerprint detecta alterações no arquivo durante sua leitura, mas não oferece
snapshot atômico de uma instalação em execução. Fechar o jogo ao reproduzir.

Executável original indicado pelo operador, separado do manifesto de recursos:
`Sims.exe.bak`, 4.422.663 bytes; textos 1.91, versão numérica 1.9.1.0; SHA-256:

```text
32857c25e00594a9bc5d700f7b373b6df59ab76ec51b36aa1d30f3dd973eff58
```

Metadados e hashes identificam os arquivos locais; não autenticam sua procedência.
Não houve comparação integral contra a ISO, execução original ou teste de hardware.

Uma execução inicial Debug do inspector host levou 7,72 segundos, com RSS máximo
de 9.036 KiB via `/usr/bin/time -v`. É uma observação única, com caches e ambiente
host, anterior a pequenos ajustes de CLI; não é benchmark controlado nem previsão
de consumo, carregamento ou FPS no New 3DS.

## Engenharia reversa e próximas lacunas

A instalação de Ghidra indicada pela OMM e pelo operador foi verificada em disco:
application.version 12.1.3 PUBLIC, launcher headless presente, Java mínimo 21;
OpenJDK 21.0.12.1 disponível. Nenhum projeto Ghidra TS1 foi importado/analisado
neste checkpoint. Validar loader PE e linguagem x86 antes da primeira análise.

Próximas unidades: campos comprimidos TTAB, resolução de precedência/dependências,
cadeia de interação da cadeira e interpretação de primitivas. Usar Ghidra para
perguntas específicas sobre o original que as fontes de recursos não resolvam.
Experimento gráfico no New 3DS continua necessário cedo no desenvolvimento.

Igualdade de contadores, checks estruturais e sanitizers não comprovam semântica,
fidelidade, compatibilidade dos saves, suporte às expansões ou execução nos consoles.
