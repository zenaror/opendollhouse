# OpenDollhouse

<a href="https://www.buymeacoffee.com/zenaro147" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 60px !important;width: 217px !important;" ></a>

Reimplementação em **C/C++** do The Sims 1 para PC, orientada pela experiência
original, pelas sete expansões e pelo conteúdo personalizado clássico.
Simitone e FreeSO são referências de comportamento e código.

**Projeto experimental: ainda não há uma versão completa jogável.** O motor
nativo já possui leitores de dados, simulação parcial, interface Linux de teste,
áudio e salvamento em cenários limitados. A interface original, construção/compra
completas e a cobertura das expansões continuam em desenvolvimento.

Linux é o primeiro alvo de validação. Windows, macOS Intel/Apple Silicon,
Android, New Nintendo 3DS, PS Vita e Switch são alvos futuros; Wii U é um alvo
desejado. Isso não representa suporte ou desempenho comprovado nessas plataformas.

## Dados do jogo

Você precisa fornecer uma cópia dos dados do **The Sims Complete Collection**,
fora do checkout. O projeto não distribui dados comerciais. A entrada atual é
uma pasta de dados extraídos; importação direta de ISO/CAB não está implementada.
O motor não depende do executável original nem de um CD inserido.

## Compilar e testar no Linux

Requisitos: compilador C++17, CMake 3.16+ e Python 3.8+ para scripts auxiliares.
A janela exige X11; PangoCairo é recomendado para textos e idiomas.
Em Debian/Ubuntu/Linux Mint:

```sh
sudo apt install build-essential cmake python3 libx11-dev libpango1.0-dev
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native --parallel 2
ctest --test-dir build/native --output-on-failure
```

Para compilar apenas ferramentas e testes sem janela, configure com
`-DODH_X11=OFF`. Os testes de janela são opcionais e exigem um display isolado.

## Uso experimental

A partir da raiz do checkout, indique sua pasta de dados:

```sh
export GAME_ROOT="/caminho/para/The Sims"
./build/native/odh-person --game-root "$GAME_ROOT" --mode 1
```

O seletor de vizinhança e as interações ainda têm cobertura limitada.
Use um perfil de testes e preserve seus saves originais.
Consulte [ferramentas e exemplos](docs/tools.md) e a
[matriz de funcionalidades](docs/playable-feature-matrix.md) antes de testar.

## Código e documentação

- `src/`, `include/odh/` e `native/`: motor e componentes C/C++.
- `tools/`: programas de inspeção, interface e validação.
- `tests/`: testes e dados sintéticos.
- [Documentação](docs/README.md): arquitetura, formatos, planos e validações.
- [Mods e expansões](docs/modding.md): guia do suporte futuro; ainda sem API pública estável.
- [Changelog](CHANGELOG.md): mudanças relevantes ainda não lançadas.
- [Referências e atribuições](docs/references.md): Simitone, FreeSO e dependências.

## Licenciamento

A licença do código do ODH ainda será definida. Não presumir que a licença de
uma referência se aplica ao projeto. Reutilização de código de terceiros exige
verificar as licenças dos arquivos e dependências. Dados do jogo não são distribuídos.
