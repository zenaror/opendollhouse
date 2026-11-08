# Compilar e usar

O aplicativo é experimental. Use uma cópia dos dados e um perfil de teste;
não há uma versão completa jogável publicada. Os comandos partem da raiz do clone.

## Requisitos Linux

Compilador C++17, CMake 3.16 ou superior e Python 3.8 ou superior para ferramentas
auxiliares. A janela usa X11; PangoCairo é recomendado para texto e acentos.
Em Debian, Ubuntu e Linux Mint:

```sh
sudo apt install build-essential cmake python3 libx11-dev libpango1.0-dev
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native --parallel 2
ctest --test-dir build/native --output-on-failure
```

Com `-DODH_X11=OFF`, a configuração dispensa a janela. Sem PangoCairo, o texto
usa uma alternativa bitmap com limitações de alfabetos. Essas opções não comprovam
funcionamento em outra plataforma; veja [portabilidade](portability.md).

## Iniciar

Forneça uma pasta extraída da Complete Collection, separada do código:

```sh
export GAME_ROOT="/caminho/para/The Sims"
./build/native/odh-person --game-root "$GAME_ROOT" --mode 1
```

`--mode 1` abre a entrada residencial/vizinhança. Use `--help` para conferir as
opções da revisão compilada. O motor não exige o executável original nem CD
inserido. Importação direta de ISO ainda não está implementada.

Para isolar configurações, dados, estado e cache em um teste:

```sh
export XDG_CONFIG_HOME="$PWD/validation-output/profile/config"
export XDG_DATA_HOME="$PWD/validation-output/profile/data"
export XDG_STATE_HOME="$PWD/validation-output/profile/state"
export XDG_CACHE_HOME="$PWD/validation-output/profile/cache"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_STATE_HOME" "$XDG_CACHE_HOME"
./build/native/odh-person --game-root "$GAME_ROOT" --mode 1
```

Ao continuar uma família existente, preserve o perfil completo, incluindo o
registro de ocupação e os arquivos de estado. Copiar somente o save pode não
conservar a autorização para abrir aquele lote.

## Inspecionar dados

```sh
mkdir -p validation-output
python3 tools/fingerprint.py "$GAME_ROOT" > validation-output/input-manifest.json
./build/native/odh-inspect "$GAME_ROOT" > validation-output/corpus.json
```

O inspetor lê estruturas e emite diagnósticos; não executa scripts nem comprova
que os recursos funcionam no jogo. Instalações diferentes podem ter contagens
legitimamente diferentes. Consulte [formatos e limites](formats.md).

## Diagnóstico

Os verificadores `tools/validate_runtime_smoke.py` e
`tools/validate_runtime_sequence.py` aceitam execução delimitada; consulte
`--help`. Use-os com um perfil isolado e resultados fora dos arquivos-fonte.
A [validação](validation-workflow.md) explica os limites de cada verificação.

Para uma execução contínua sem janela, `--headless-ticks` aceita de 0 a 65535
atualizações. Um dia no relógio do jogo corresponde a 43200 atualizações.
Esse limite permite testar o dia inteiro no mesmo processo; ele não aumenta
o trabalho permitido por personagem em cada atualização. A execução ainda
pode parar por erro ou por uma escolha do jogador pendente. Use cópias do
perfil completo e confira o salvamento em um novo processo ao terminar.

`ODH_TRACE_MOTIVE_CHANGES=1` habilita observação limitada das alterações de
necessidades: até 256 registros, com contagem de excedentes. O registro não
altera o save e não representa todos os valores ao longo da partida. Compare
os pontos da ação, distinguindo seu efeito do desgaste natural das necessidades.

A entrada integrada aceita `--lot-window-size LARGURA ALTURA`, entre 320–1024
por 200–1024, até 1048576 pixels; o padrão é 800×600. Essa opção não pode ser
combinada com execução sem janela. Ela não equivale a suporte a resolução ou
controles de outro aparelho.
