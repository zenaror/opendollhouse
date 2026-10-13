# SLOT e recurso de roteamento — 2026-10-13

`read_slot` lê registros geométricos SLOT, distintos dos vínculos de ocupação
de `odh_world`. Isso fornece dados para a próxima etapa de roteamento. A ação
da cadeira ainda para na primitiva 45; nenhum percurso foi executado.

## Perfil

Cabeçalho de 16 bytes: reserved uint32, versão uint32, assinatura `TOLS` e
contagem uint32, little endian. Reserved é preservado, não usado como regra.
Layouts explícitos 4..10, spans 34/34/54/58/62/66/70 bytes. Antes de alocar,
contagem deve caber no payload e no limite ODH de 4.096 registros por tabela.
Truncamento, assinatura ou versão desconhecida produzem diagnóstico.

Cada registro conserva tipo, três offsets float como bits uint32, scores
standing/sitting/ground int32, flags uint32 e snap target int32. Proximity,
max size, campo i10, gradient, height, facing e resolution só existem conforme
as extensões da versão. Ausência é distinta de valor zero. A versão 5 foi
testada com fixture sintética, mas não aparece neste corpus.

Não aplicar escala de proximity antiga, height zero->5, facing default,
resolution default ou scores no leitor. Esses são assuntos do perfil de
execução. NaN/infinito e negative zero têm bits preservados; uma camada de
geometria deverá validar valores antes de usá-los. Bytes não consumidos são
reportados, sem supor automaticamente que sejam padding.

`slot_of_type` seleciona a ocorrência dentro de um tipo, na ordem do arquivo.
Não confundir esse índice com o índice absoluto do registro.

## Checks

CTest 9/9 passou em Debug e ASan/UBSan. A suíte SLOT possui 538 checks
sintéticos: todas as versões, cada truncamento das fixtures, sinais/flags,
bits float, extensões ausentes, limites, seleção intercalada por tipo e
bytes restantes. A CLI tem integração FAR/IFF/SLOT com seletores e diagnóstico
de ambiguidades. LSan não validado (ptrace; `detect_leaks=0`).

O inspector percorreu o corpus completo em Debug e sanitizadores, saída 0,
stderr vazio e relatórios idênticos. Os 14 contadores anteriores continuam
coincidindo; isso valida leitura do perfil, não geometria ou gameplay.

| Versão SLOT | Tabelas |
|---|---:|
| 4 | 51 |
| 6 | 525 |
| 7 | 71 |
| 8 | 15 |
| 9 | 1 |
| 10 | 899 |

Total: 1.562 tabelas, 7.142 registros, zero bytes restantes. Tipos observados:
0=2.482, 1=66, 3=4.594. Contagens incluem cópias do corpus; não são universais.
Reports locais ignorados: `reports/corpus-slot.json` e
`reports/corpus-slot-sanitize.json`.

## Registro indicado pela cadeira

`Global.iff` em `referencias/The Sims/GameData/Global/Global.far`, entrada de
148.833 bytes, SHA-256 revalidado
`bdfd8456abceaa3e44f0a9754815248ea1e80c7b0df4fbaff53843fae3731478`.
SLOT100 é versão10, 29 registros, todos tipo3. A seleção tipo3/índice1 é
registro absoluto1. Campos observados:

- offsets: três bits float zero;
- standing=1, sitting=0, ground=0;
- flags=85 (`0x55`), snap target=-1;
- proximity raw=[16,16,16], max size=0, i10=0;
- gradient bits=1044381696 (`0x3e400000`);
- height=0, facing=-2, resolution=16.

JSON Debug e ASan/UBSan idênticos. Reports
`reports/chair-global-route-slot.json` e sua variante `-sanitize` preservam
essa consulta. `DiningChairs.iff` também contém SLOT0 versão4 com dois registros
e SLOT128 versão10 com oito; ambos passaram pelo leitor.

Essa seleção segue o operando data1/scopeGlobal2 de `sit core` e a resolução
de recursos da referência fixa FreeSO. Não comprova a precedência ou o
tratamento do executável original. Campos e flags ainda precisam de semântica
e comparação adequadas antes de alegar percurso fiel.

## Reprodução e próxima etapa

```sh
./build/odh-slot '../referencias/The Sims/GameData/Global/Global.far' Global.iff 100 3 1
./build/odh-slot '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0
./build/odh-inspect '../referencias/The Sims' > reports/corpus-slot.json
python3 tools/check_census.py reports/corpus-slot.json
```

Implementar posição/footprints, geração de destinos e roteamento, com retomada
da VM por ticks. Separar dados SLOT brutos de defaults/unidades do runtime.
Sprites/paletas e composição Linux continuam necessários ao cenário funcional.
