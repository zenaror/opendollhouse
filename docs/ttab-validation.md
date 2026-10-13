# Checkpoint TTAB e alvo Linux — 2026-10-13

Decisão humana: a primeira versão funcional será Linux, antes dos ports.
A meta está ativa; este checkpoint conclui uma unidade de leitura de recursos.
VM, pixels, renderização, interação executada e persistência ainda estão pendentes.

## Cobertura e checks

`read_ttab` lê layouts 4..8 sem compressão e 9 com coding 0/1. Versões 2/3
têm status `legacy_unsupported`; payload ausente difere de contagem zero.
Campos desconhecidos não recebem semântica de simulação. Bits não consumidos
são reportados; não se presume que sejam sempre padding.

CTest: 4/4 passaram no build Debug e ASan/UBSan. A suíte TTAB tem 104 checks
sintéticos, incluindo larguras, sinais, fronteiras entre bytes, truncamento,
limites de alocação, variantes raw e seleção ambígua na CLI. Os testes não
dependem dos recursos comerciais. LeakSanitizer continua desativado por limitação
de ptrace no sandbox (`ASAN_OPTIONS=detect_leaks=0`); leaks não foram validados.

O corpus local completo do perfil descrito em `validation.md` percorreu o novo
inspector em Debug e ASan/UBSan, saída 0 e stderr vazio. Os relatórios são
idênticos e os 14 contadores anteriores continuam coincidindo. Relatórios locais
ignorados: `reports/corpus-ttab.json` e `reports/corpus-ttab-sanitize.json`.

| Estado/campo | Ocorrências |
|---|---:|
| TTAB decodificadas | 1.564 |
| Dessas, comprimidas | 1.398 |
| Contagem vazia | 270 |
| Legadas não suportadas | 19 |
| Payload ausente | 2 |
| Interações decodificadas | 7.124 |
| Entradas de motivos | 113.984 |
| Bits restantes somados | 21.587 |

As contagens incluem cópias do corpus; não são cobertura universal de TS1.
Os dois payloads ausentes e as 19 tabelas legadas permanecem lacunas explícitas.

## Cadeira candidata

Consulta em leitura: `Objects.far`, entrada `DiningChairs.iff`, TTAB ID 128.
A identidade da entrada é SHA-256
`cffff7b4e8b5d1b31351c32a2f8656f50dbf72a82217bf38b4828853fc6f90c7`.

A tabela versão 9 comprimida contém uma interação: ação BHAV `0x1001`, teste
BHAV `0x1002`, flags 1, label index 0, attenuation code 3, attenuation bits
1017370378, autonomy threshold 50, joining index -1. Tem 16 entradas de motivos;
a de índice 6 tem delta 25, as demais delta 0. Restam dois bits. Esta descrição
registra campos; não identifica automaticamente rótulo, necessidade ou regra.

`odh-ttab` produziu JSON idêntico em Debug e sanitizadores. Isso habilita a
investigação dos BHAVs indicados, mas ainda não prova sentar/levantar no ODH.

## Reprodução

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
ctest --test-dir build --output-on-failure
cmake -S . -B build-sanitize -DODH_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-sanitize -j 2
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-sanitize --output-on-failure
ASAN_OPTIONS=detect_leaks=0 ./build-sanitize/odh-inspect '../The Sims' > reports/corpus-ttab-sanitize.json
python3 tools/check_census.py reports/corpus-ttab-sanitize.json
./build/odh-ttab '../The Sims/GameData/Objects/Objects.far' DiningChairs.iff 128
```

Próxima unidade: dependências e semântica da interação, VM headless com traces
e diagnósticos, seguida de composição gráfica Linux. O perfil de memória e
as interfaces portáveis continuam orientados às restrições do New 3DS.
