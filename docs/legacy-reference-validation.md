# Inventário comparativo da Legacy Collection

Em 2026-10-15, após o usuário confirmar o término da cópia, foi feita leitura
estática de `../referencias/The Sims Legacy Collection` e
`../referencias/The Sims`. A origem Steam/2025 é informação fornecida pelo
usuário, ainda sem confronto com manifesto de distribuição.

## Escopo e resultados

| Árvore fornecida | Arquivos regulares | Bytes de arquivos |
|---|---:|---:|
| Legacy Collection | 10.406 | 3.668.976.396 |
| Instalação anterior | 9.311 | 3.415.570.602 |

Comparar caminhos relativos com `casefold` encontrou 4.963 caminhos comuns,
5.443 exclusivos da Legacy e 4.348 exclusivos da instalação anterior, sem
colisões nessa chave. Essas contagens incluem documentação e outros arquivos;
não representam uma contagem de recursos jogáveis ou de conteúdo novo.

Foram comparados por SHA256 todos os arquivos comuns `.hit`, `.hot` e `.hsm`:
27 de cada extensão, 81 ao todo, todos idênticos. Ambas as árvores contêm
90 FAR. A amostra dos cinco primeiros caminhos FAR comuns em ordem lexical
teve os seguintes resultados, apesar de tamanhos iguais em cada par:

| Caminho relativo | SHA256 entre edições |
|---|---|
| `Deluxe/Deluxe.far` | Diferente |
| `Downloads/BalloonArch/BalloonArch.far` | Diferente |
| `Downloads/BedMMDownload/BedMMDownload.far` | Diferente |
| `Downloads/BirthdayCake/BirthdayCake.far` | Diferente |
| `Downloads/BookshelfMagic/BookshelfMagic.far` | Igual |

O conteúdo interno dos FAR diferentes ainda não foi comparado; não se atribui
a diferença a scripts, traduções, gráficos ou correções do motor sem essa
análise. A amostra não permite estimar a proporção de diferenças no catálogo.

## Identidade para futuras análises estáticas

`The Sims Legacy Collection/Sims.exe` tem 7.125.288 bytes e SHA256
`b7c8765ed2383cbda8c047694d54a21d97895c8dae57dcc87716ca6e78b342e3`.
O cabeçalho indica PE32 (`0x10b`), máquina x86 (`0x14c`), oito seções e
image base `0x400000`. O timestamp COFF bruto é `1743203867`; ele não serve
como comprovação da data de distribuição. Essa identidade é distinta da
usada nas evidências anteriores; image base igual não implica endereços
de rotinas iguais. Nenhum executável foi executado ou modificado.

## Reprodução e limites

Artefatos locais ignorados pelo Git:

- `reports/legacy-reference/inventory.py`: inventário por metadados e hashes
  das tabelas de áudio comuns e da amostra FAR.
- `reports/legacy-reference/legacy-reference-inventory.json`: contagens,
  extensões, caminhos exclusivos, hashes e estabilidade das duas árvores.
- `reports/legacy-reference/pe-identities.json`: identidades dos executáveis
  da raiz, obtidas por cabeçalhos PE e SHA256.

Para repetir o inventário a partir do checkout:

```sh
python3 reports/legacy-reference/inventory.py
```

Os metadados comparados antes/depois incluem caminho, tamanho, mtime, modo,
dispositivo e inode. As duas árvores ficaram estáveis durante a leitura;
isso não comprova completude da distribuição nem verifica todos os bytes dos
arquivos que não foram amostrados. Hashes iguais demonstram identidade dos
arquivos comparados, sem comprovar execução ou compatibilidade do runtime ODH.
Não houve alteração de código de runtime ou teste de hardware. Os dados
comerciais permanecem fora do repositório.
