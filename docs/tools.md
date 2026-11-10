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

## Compilação para desenvolvimento

Em um build nativo com GCC ou Clang, `-DODH_BUILD_FAST_PERSON=ON` acrescenta o
alvo opcional `odh-person-fast`. Ele reutiliza as bibliotecas do motor e compila
o aplicativo com menos otimização para reduzir o tempo entre alterações. Para
gerá-lo, use `cmake --build build/native --target odh-person-fast --parallel 2`.

Para investigar viagens ainda em desenvolvimento, acrescente
`-DODH_FAST_PERSON_ALL_TRAVEL_DESTINATIONS=ON` à configuração. Essa opção exige
o alvo rápido e permite nele os destinos instalados; o aplicativo `odh-person`
continua com sua política normal. A disponibilidade nesse build de diagnóstico
não declara uma expansão completa. Ambas as opções ficam desligadas por padrão.

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

Para examinar um salvamento nativo sem iniciar a simulação, o utilitário dos
testes de integração oferece comandos de leitura:

```sh
cmake --build build/native --target odh-cas-movein-e2e --parallel 2
./build/native/odh-cas-movein-e2e --inspect-runtime-entity partida.odhs 42
./build/native/odh-cas-movein-e2e --inspect-travel-return partida.odhs "$GAME_ROOT/UserData/Neighborhood.iff"
./build/native/odh-cas-movein-e2e --inspect-session partida.odhs
```

O primeiro comando de inspeção aceita de 1 a 16 números internos de objetos ou
personagens e mostra seus dados, ações e esperas em JSON. Um número pode ter
mais de uma geração preservada; cada corpo aparece separado. O segundo mostra
as diferenças de dados pessoais entre a casa e o destino de uma viagem, com a
origem de cada valor. Essa prévia não publica o retorno nem altera o salvamento.
Use o arquivo da vizinhança correspondente à partida; uma origem incompatível
é rejeitada. A leitura do estado não comprova que uma ação funciona no jogo.

`--inspect-session` também mostra o catálogo armazenado no salvamento, quando
presente: a ordem original dos recursos, seus arquivos, identificadores e os
números de vizinho atribuídos. O JSON inclui as contagens totais, quantas linhas
foram mostradas e se houve corte, com limite de 4096 recursos. Salvamentos
antigos sem esse catálogo mostram `catalog: null`. A inspeção de retorno usa o
mesmo formato para os estados da casa, da visita e da prévia do retorno. Esses
dados descrevem o arquivo salvo; não substituem a conferência dos arquivos do
jogo instalado.

## Consultar menus sem executar ações

Para conferir várias opções em uma única abertura de uma partida salva, repita
`--headless-menu-query`. Cada par indica o Sim selecionado e o alvo da consulta:

```sh
./build/native/odh-person --game-root "$GAME_ROOT" --mode 1 \
  --load-session partida.odhs --headless-ticks 0 \
  --headless-menu-query 1 2 --headless-menu-query 1 3
```

Os números são exemplos; use os identificadores da partida inspecionada. As
consultas usam o Sim atualmente selecionado, sem avançar o tempo nem enviar
ações. O limite é de 32 consultas por execução. O JSON conserva a primeira em
`menu_query`; quando há mais de uma, `menu_queries` contém todas na ordem pedida.
Assim é possível conferir vários menus sem recarregar a partida para cada alvo.
Uma opção oferecida ainda precisa ser testada em execução para demonstrar seu efeito.

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

O validador de gestão de famílias oferece `--collect-shutdown-failures` para
coletar etapas independentes após um relatório completo de vazamento de memória.
O encerramento permanece reprovado, e o comando continua retornando erro.
O modo padrão interrompe a verificação nessa falha.

### Continuar de um checkpoint autenticado

Uma execução pode parar em um diálogo antes de atingir a duração pedida e ainda
gravar um estado válido. No recibo de `validate_runtime_smoke.py`, `ok` e `errors`
continuam descrevendo o resultado da execução: autenticar um save não aprova um
horizonte incompleto. O campo separado `checkpoint`, quando presente, descreve
um novo save conferido pelo inspetor nativo e pelo registro atual da casa, com
as provas de processo e de integridade das entradas preservadas.

`checkpoint.path`, `sha256` e `tick` identificam o arquivo e o instante;
`profile` e `profile_after` identificam o perfil completo e seu inventário.
`native_authority` registra a identidade conferida da casa e da família. Sem
essa autenticação, `checkpoint` fica `null`; `checkpoint_error`, quando presente,
explica o impedimento. Uma reabertura canônica de zero atualizações pode ter
`ok: true` e `checkpoint: null`, pois reutiliza o arquivo de entrada sem produzir
um novo save.

Para quem integra o verificador em Python, este exemplo usa `record` devolvido
pela execução anterior e a mesma instância `smoke` de `Smoke`, já configurada
com binário, inspetor e dados do jogo. Depois de examinar o resultado e decidir
explicitamente continuar, use um nome de etapa ainda não criado:

```python
from pathlib import Path

cp = record.get("checkpoint")
if cp is None or not cp.get("authenticated"):
    raise ValueError("a execução não forneceu um novo checkpoint autenticado")

continued = smoke.run(
    "continue-approved", Path(cp["path"]), 1024,
    extras=["--mode", "1"], requirements=[],
    expected_base=cp["tick"], expected_source_hash=cp["sha256"],
    profile_source=Path(cp["profile"]),
    expected_profile_hashes=cp["profile_after"],
    automatic_dialog_ok=False,
)
```

A chamada copia o perfil e confere novamente seus hashes. Preserve também o
recibo original e os mesmos arquivos de binário e inspetor. O exemplo não
responde diálogos nem repete tentativas automaticamente: uma escolha pendente
precisa de decisão explícita, e a nova execução pode parar novamente. Examine
`continued["ok"]`, `errors` e seu próprio `checkpoint` separadamente. Nenhum
campo presume que uma ação anterior continua ativa ou já terminou.

## Preparar avisos em testes sem janela

`--headless-info-dialog-ok-limit N` confirma até `N` avisos informativos, de 1 a
32, durante uma execução com `--headless-ticks` positivo. Perguntas, campos de
texto e listas de escolha permanecem abertos. Cada confirmação aparece em
`headless_info_dialog_answers` no JSON. Use essa opção em perfis de teste para
atravessar os avisos iniciais na mesma execução. Ela exige uso separado das
outras opções de resposta de diálogo.

## Gravar pontos de retorno em testes sem janela

`--headless-checkpoint-interval N` grava a partida a cada `N` passos da execução.
É uma opção de teste para casas residenciais, desativada por padrão. Parta de
uma partida nativa já gravada e de uma cópia integral do seu perfil. Um arquivo
`.odhs` isolado não basta: o perfil precisa guardar a ligação da família com a casa.

```sh
build/native/odh-person --game-root "/caminho/do/jogo" \
  --load-session "/perfil-copiado/partida-atual.odhs" --mode 1 \
  --headless-ticks 4096 --save-session-on-exit \
  --headless-checkpoint-interval 512 --no-audio
```

Nesse exemplo, o comando grava sete pontos intermediários e o estado final.
O limite é de 32 gravações desse comando, incluindo a saída. O intervalo conta
a partir do ponto retomado. Se for maior que a duração pedida, só há a gravação
final. Cada ponto aceito aparece no JSON e imediatamente no log, com o instante
e o caminho; uma falha posterior preserva os pontos já aceitos.

A opção exige uma execução com duração positiva e gravação na saída. Ela não
aceita viagens, janelas, edição direta de dados, comandos de teste adicionais
ou opções que encerram a execução antecipadamente. Os avisos podem ser tratados
com a opção informativa descrita acima; perguntas continuam aguardando resposta.

## Registrar os arquivos de uma compilação

Depois de terminar um build CMake com o gerador **Unix Makefiles**, registre os
arquivos e as opções fornecidos à etapa de ligação:

```sh
mkdir -p ../opendollhouse-validation
python3 tools/collect_cmake_link_inputs.py --source . --build build/native \
  --target odh-person --collect --output ../opendollhouse-validation/linked-build.json
python3 tests/cmake_link_inputs.py
```

Mantenha fontes e build parados durante a coleta. Sem `--collect`, a ferramenta
mostra somente o planejamento. O relatório serve ao diagnóstico da compilação;
o funcionamento continua dependendo dos testes do aplicativo. Ele distingue os
objetos fornecidos nas bibliotecas das partes efetivamente usadas no programa,
que exigiriam um mapa produzido pelo vinculador. A ferramenta recusa arquivos de
dependências ausentes e saídas já existentes. Guarde os resultados fora dos
diretórios de fontes e build, como no exemplo.
