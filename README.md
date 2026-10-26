# OpenDollhouse

Projeto para recuperar a experiência do The Sims 1 para PC, com suporte
progressivo às sete expansões e ao conteúdo personalizado clássico.
Direção vigente: **portar o jogo baseado no Simitone/FreeSO para C/C++**, com
interface, gráficos, áudio e controles nativos, sem C#/.NET no produto final.
A interface e o comportamento do The Sims 1 original continuam sendo o destino.
O fork alexjyong e sua revisão FreeSO são referências de código e comportamento.
O desenvolvimento atual acontece no programa C++; o candidato gerenciado
anterior está preservado, sem ampliar ou executar sua ponte nesta frente.

Na avaliação anterior do candidato gerenciado foram observados compra e uso
de objetos, salvamento/reabertura e um ciclo limitado de criar, mudar e excluir
uma família. Esses resultados não comprovam os mesmos fluxos no port C++.
Agora o `Vm` do programa C++ usa diretamente o executor compartilhado das
expressões das ações. Um morador adicional completou a ação da geladeira na
Casa 07, a fila esvaziou e a sessão foi salva, reaberta e continuada nesse
executável sem runtime gerenciado. Estado, evidência e limites estão em
[native-state-integration.md](docs/native-state-integration.md).
O jogo completo ainda não está portado ou aprovado como primeira versão jogável.
Plano completo em [native-port-plan.md](docs/native-port-plan.md), estado atual
em [current-handoff.md](docs/current-handoff.md) e evidências da referência em
[avaliação Simitone](docs/simitone-linux-evaluation.md).

Base C++ em desenvolvimento: leitores de recursos, núcleo de execução BHAV e modelo inicial de
entidades/slots em C++17,
com testes sem janela, traces, rasterização CPU de avatar texturizado e decode
de sprites de objetos com alpha/Z, com composição de avatar/cadeira em imagens.
O probe da cadeira tem preview X11 opcional ligado ao World/playback. Ainda
faltam inicialização/encerramento fiel das poses, apresentação completa do lote, persistência completa, serviços
completos de simulação e validação em consoles.

Plataformas planejadas: Linux como primeiro ambiente de validação; New Nintendo
3DS, PlayStation Vita, Android, Nintendo Switch, Windows e macOS (Apple Silicon
arm64 e Intel x86_64) também são alvos futuros; Wii U é um desejo adicional.
O código deve ser planejado desde o início para separar regras do jogo de
gráficos, áudio, controles e arquivos de cada plataforma. O núcleo final não
dependerá de C#/.NET; as limitações da base gerenciada orientam a migração e permanecem registradas como evidência histórica.
Cada console exige toolchain e validação
próprias; não há build ou teste de hardware do Switch validado neste projeto.
Windows/macOS não podem ser testados manualmente pelo usuário atual e precisarão
de CI ou máquinas dedicadas antes de qualquer declaração de suporte.

O desenvolvimento segue o status atual da meta na interface. A meta foi
recriada e inicialmente pausada; uma retomada posterior autoriza continuar.
Consultar a meta e a OMM antes de executar, sem usar o texto histórico da pausa
para sobrepor seu status atual.

## Dados do jogo

O motor ODH lê recursos externos do The Sims Complete Collection. O jogador
fornece os dados copiados do jogo ou uma ISO da qual preparar esses dados.
O runtime não depende do executável original, de disco inserido ou de verificação
de CD. A entrada prioritária desta versão é a pasta de dados extraídos, adequada
também ao armazenamento dos portáteis.

A ISO local foi inspecionada somente para leitura: os recursos de instalação
estão em `Setup/data1.cab` até `data5.cab`, sem FAR diretamente acessíveis.
Os CLIs atuais leem arquivos/pastas; leitura direta de ISO e importação dos
CABs ainda não estão implementadas. A preparação desses assets será uma etapa
separada, preservando a fonte e diagnosticando dados ausentes ou incompatíveis.

A [janela Linux de seleção do mapa](docs/neighborhood-map-validation.md),
`odh-neighborhood-window --user-data DIR --ui-graphics UIGraphics.far --mode 1`,
mostra o fundo original e marcadores clicáveis. Retorna o número da casa em
JSON e fecha; ainda não abre uma sessão de lote. Requer build com X11.

## Organização e continuidade

| Conteúdo | Local canônico |
|---|---|
| Orientação do projeto | `AGENTS.md`; base compartilhada no papel OMM `project-base` |
| Planejamento e critérios Linux/ports | [docs/architecture.md](docs/architecture.md) |
| Contratos e validação técnica | `docs/`; índice de documentos abaixo |
| Núcleo C++ e interfaces | `src/` e `include/odh/` |
| Fixtures sintéticas e regressões | `tests/` |
| CLIs, verificadores e scripts Ghidra | `tools/` |
| Configuração de build e testes | `CMakeLists.txt` |
| Evidências locais e projetos de análise | `reports/`, ignorado pelo Git |
| Consultas upstream fixadas | `refs/upstream/`; [revisões e créditos](docs/references.md) |
| Dados comerciais preservados | pasta externa `../referencias/` |
| Posição atual e pendências | handoff OMM no escopo `OpenDollhouse` |

O repositório mantido é `opendollhouse`, no Git de trabalho indicado em
`AGENTS.md`. FreeSO, Simitone, seu submódulo e FSOMonoGame são clones locais
de consulta; suas estruturas e revisões ficam preservadas. Logs, capturas,
receipts e projetos Ghidra únicos ficam retidos em `reports/`; ser ignorado
pelo Git não os torna descartáveis. Não houve remoção de evidências neste
checkpoint de organização.

## Compilar e testar o programa C++

Requisitos desta etapa: compilador C++17 e CMake 3.16 ou superior. No Linux,
Iconv converte textos antigos do jogo. A janela jogável precisa dos arquivos de
desenvolvimento X11 e PangoCairo com Cairo para desenhar menus e rótulos usando
fontes do sistema. Em distribuições Debian/Ubuntu, os pacotes são `libx11-dev`
e `libpango1.0-dev`. O build padrão para com uma mensagem clara se algum deles
faltar, em vez de produzir uma janela sem controles visíveis. Os scripts
auxiliares usam Python 3.8 ou superior, somente com a biblioteca padrão.

```sh
cmake -S . -B build/linux-checkpoint -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-checkpoint -j 2
ctest --test-dir build/linux-checkpoint --output-on-failure
```

`-DODH_X11=OFF` compila sem janela e não exige Pango. Com X11 ligado, PangoCairo
é usado quando disponível; sem ele, a janela mantém rótulos com uma fonte
bitmap incorporada. Essa alternativa translitera acentos latinos comuns e
mostra `?` para caracteres de outros alfabetos, então não substitui o suporte
multilíngue completo do PangoCairo. `-DODH_LINUX_TEXT_RENDERER=OFF` seleciona
essa alternativa sem tentar localizar PangoCairo. Nenhum conjunto de fontes
comerciais ou de terceiros é copiado ao projeto. Comandos de janela e seus
limites estão em
[live-view-validation.md](docs/live-view-validation.md).

Os testes geram fixtures sintéticas na memória; não dependem dos dados comerciais.

O gate de integração CAS para lote vago é explicitamente opt-in e não faz parte
do CTest. Ele cria uma família de dois Sims com o catálogo CAS e
`TemplatePerson.iff`, compra um lote base vago pelo preço SIMI, inicia os dois
no runtime, salva após 16 ciclos e retoma em outro processo. O save retomado é
comparado byte a byte com 32 ciclos sem interrupção. O perfil do jogador é
temporário e os dados instalados são verificados por SHA-256 antes/depois.

```sh
cmake --build build --target odh-cas-movein-e2e odh-person
python3 tests/cas_movein_e2e.py \
  --runner build/odh-cas-movein-e2e --person build/odh-person \
  --game-root '../referencias/The Sims'
```

## Inspecionar uma instalação fornecida pelo usuário

Mantenha a instalação fora deste checkout. O exemplo abaixo pressupõe uma pasta
irmã `referencias/The Sims`. Feche o jogo enquanto registra o corpus de referência.

```sh
mkdir -p reports
python3 tools/fingerprint.py '../referencias/The Sims' > reports/input-manifest.json
./build/odh-inspect '../referencias/The Sims' > reports/corpus.json 2> reports/corpus-errors.txt
python3 tools/check_census.py reports/corpus.json
```

`odh-inspect` abre arquivos somente para leitura. Emite JSON com contagens e
diagnósticos em stderr. Saída 0 significa que os checks implementados passaram;
1 indica erros em entradas; 2 indica erro de uso ou preparação. Não extrai recursos
nem executa BHAVs. O relatório deve ser interpretado junto com seu status de saída.

O comparador reconhece o snapshot local de 2026-10-13. Outra instalação pode ter
contagens diferentes legitimamente. Igualdade de contagens não autentica arquivos:
o manifesto de hashes registra a identidade dos inputs efetivamente selecionados.
Ele não cobre todos os formatos da instalação, o executável ou a ISO.

Para consultar os campos de uma tabela de interação específica:

```sh
./build/odh-ttab '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 128
```

O nome da entrada é exato; o ID aceita decimal ou prefixo `0x`. A ferramenta
rejeita seleção ambígua. Saída 0 indica tabela decodificada ou contagem vazia,
3 indica payload ausente ou versão legada não suportada, 2 indica erro.
O JSON preserva bits restantes e não executa os BHAVs indicados pela tabela.

Para inspecionar um BHAV ou fazer uma execução limitada sem serviços do mundo:

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1002
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run
```

O modo `--run` emite trace e status JSON. Sem entidades, a cadeira para com
diagnóstico de scope não suportado, saída 3. Saída 0 indica retorno true/false
da rotina; 2 indica erro de uso/recurso. Há um orçamento de 10.000 instruções,
distinto de ticks do jogo. A biblioteca preserva o estado para retomada ao
esgotar o orçamento. Recursos globais opcionais são fornecidos explicitamente
com `--global ARCHIVE.far ENTRY.iff`; não há busca nem precedência automática.
`--semiglobal ARCHIVE.far ENTRY.iff` carrega um semiglobal ligado pelo GLOB128
Pascal do code owner, com validação do nome e dos scopes. O JSON registra o
vínculo. O [contrato de Main/idle do avatar](docs/person-lifecycle-validation.md)
descreve os caminhos nativos encontrados e os limites da execução atual.
A biblioteca possui [fila limitada e retorno ao Main](docs/interaction-validation.md)
com gate do wrapper global281 nativo; o preview ainda usa o host anterior.
`odh-person` executa o Main ou uma rotina explícita do avatar com owners,
Sleep e fila no mesmo relógio. O [gate do host de pessoa](docs/person-host-validation.md)
registra execução nativa parcial. O [tuning por owner](docs/tuning-validation.md)
carrega BCON explícitos. O [contexto de lote](docs/lot-validation.md) carrega SIMI
e zoning externos; Main e Init atravessam suas primeiras consultas de lote.
Inicialização completa e carregamento da casa como simulação seguem pendentes.
A [importação de entidades salvas](docs/saved-world-validation.md), com
`odh-saved-world`, agora cria um World próprio com IDs, bancos, posições,
contenção e relacionamentos. Os scripts salvos permanecem dormentes; a execução
do lote completo ainda está pendente.
O [host de avatares salvos](docs/saved-avatar-render-validation.md),
`odh-saved-scene`, renderiza corpo/cabeça/mãos e o frame salvo dos dois avatares
reais de House07 nas posições do World, em uma imagem CPU estática. A execução
integral do lote continua pendente. O [plano Linux inclui os arquivos
Downloads](docs/linux-download-resources-validation.md), e House07 da Legacy
já carregou com a família e passou por Sentar/Levantar agendados no runtime;
janela interativa, ciclo contínuo e demais interações seguem pendentes. Com
`--objects`, o
[host compõe os objetos salvos](docs/saved-object-architecture-validation.md),
incluindo contenção por SLOT e gráficos dinâmicos. `odh-architecture` decodifica
as grades de piso/parede e mapas de recursos. Com `--floors FLOORS_IFF`, o
[compositor de pisos e terreno](docs/saved-floor-validation.md) desenha materiais
globais, meios pisos diagonais e aplica altitude a pisos, objetos e avatares.
Com `--terrain-theme 0`, a [base do terreno](docs/terrain-surface-validation.md)
compõe o solo com cores salvas e normais de relevo. Temas são explícitos; grama
em lâminas, iluminação por rooms e execução do lote seguem pendentes.
Com `--walls WALLS_IFF` (junto de `--floors`), o
[snapshot de paredes erguidas](docs/saved-wall-validation.md) aplica padrões e
máscaras reais de portas/janelas, com depth compartilhado e
[junções reais](docs/saved-wall-cap-validation.md).
Com `--architecture-resource FILE` repetível, o
[snapshot resolve FLRm/WALm externos](docs/saved-scene-external-architecture-validation.md)
e renderiza lotes sem avatar; House23 foi verificada com pisos, paredes e objetos
reais. `--architecture-only --floors` oferece preview explícito de pisos sem
restaurar World. Os limites de retenção e o guard de pool/water permanecem ativos.
`--level 1|2 --wall-mode up|down` seleciona o
[andar e as paredes abaixadas](docs/wall-view-validation.md); cutaway por
proximidade e visibilidade runtime seguem pendentes.
O [leitor SPR#](docs/wall-resource-validation.md), `odh-spr`, decodifica os padrões
e máscaras reais de paredes, com seleção IFF/FAR e paleta explícita. A [composição CPU](docs/wall-sprite-validation.md) preserva os recortes das máscaras. `odh-wall-appearance` já prepara os
[overrides de portas/janelas](docs/wall-appearance-validation.md) a partir dos
bindings OBJD e flags salvos, preservando os dados e sem reInit.
O [RNG compartilhado](docs/random-validation.md) fornece Random Number e os
ciclos de Sleep com seed explícita.
Com [PersonData por temp](docs/person-indirect-validation.md), o Init privado4097
retorna true no probe.
O [startup do avatar](docs/avatar-startup-validation.md), via `--initialize`,
liga Init/entrada8/BodySTR ao Main preservando World e RNG. Ainda não resolve
aparências nem restaura o save completo.
O [contexto do bairro](docs/neighborhood-validation.md), via `--neighborhood`,
fornece NBRS, enumeração e PersonData dos vizinhos. Main chega à primitiva de
relacionamentos. O [vínculo do personagem real](docs/avatar-neighbor-validation.md),
via `--avatar-file`, restaura PersonData por GUID antes de Main. O
[backend de relacionamentos](docs/relationship-validation.md) lê e escreve
em cópia própria de NBRS. O [bootstrap explícito](docs/bootstrap-validation.md)
fornece GameEdition e dois globals de runtime. A
[enumeração de entidades](docs/entity-next-validation.md) cobre pessoas, objetos
e GUIDs exatos. As [funções de entrada](docs/entry-points-validation.md) vêm
do OBJD/OBJf da entidade, inclusive o dispatch de startup; Main chega agora
a GotoRoutingSlot no perfil visitante. A [ativação de família](docs/family-restore-validation.md)
seleciona FAMI pela casa e classifica Bob como residente de House07; Main chega
à operação de inventário. A [consulta FindToken](docs/inventory-validation.md)
lê NGBH e ultrapassa esse gate; o Main residente chega à animação44.
O [snapshot salvo](docs/saved-prefix-validation.md) decodifica posições e bancos
iniciais de ObjM e resolve IDs à tabela objt. O [estado comum](docs/object-state-validation.md)
inclui frames, relações e slots, com catálogo OBJD de fontes explícitas.
A [cauda conhecida](docs/saved-tail-validation.md) inclui multitile, portais e
aparência/dados salvos dos personagens. Resolução gráfica da aparência,
publicação das entidades/posições em World e arquitetura seguem pendentes.

Um probe com entidades construídas permite investigar as branches da cadeira:

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1002 --run --world-probe
```

O cenário tem caller 1, objeto 2 e outro avatar 3, com um slot no objeto.
`--occupant 0|1|3`, `--posture 0..32767` e `--autonomous` configuram o estado
inicial. O relatório identifica `ODH_VM_constructed_world`: os BHAVs vêm do
arquivo selecionado, mas as entidades são construídas no ODH. Não carrega
um lote/save nem comprova comportamento do executável original.

O registro de roteamento indicado pela cadeira pode ser consultado em leitura:

```sh
./build/odh-slot '../referencias/The Sims/GameData/Global/Global.far' Global.iff 100 3 1
```

Os últimos seletores significam tipo 3 e índice 1 dentro desse tipo. Sem eles,
a ferramenta lista todos os registros. Os valores são brutos; floats são bits
uint32 e campos ausentes da versão são `null`. Saída 0 indica leitura, 2 erro.

Para investigar a ação com percurso e ticks no cenário construído:

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run --world-probe --route-probe --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff
```

O JSON inclui movimentos e posição final. O perfil inicial de roteamento
suporta o SLOT selecionado, com política de percurso e velocidade provisórias.
A ação ainda para em `snap`, saída 3; não é sentar/levantar completo.

Adicione `--snap-probe --object-def 16808` para usar o OBJD explícito da
cadeira selecionada. Esse perfil liga a ocupação ao SLOT privado; sem a opção
de animação abaixo, a ação para na44. Não mostra uma animação de sentar.

Adicione `--animation-probe '../referencias/The Sims/GameData/Animation/Animation.far'
adult-chair-sit-new.cmx.bcf --age 21` para avançar frames e eventos silenciosamente.
O probe adulto entrega `xevt100`, testa o GUID da cadeira e para no som23
sem a opção adicional abaixo, saída3. Os opcodes do JSON são decimais.
O relatório distingue eventos observados, progresso e ticks da simulação;
`route_ticks` nesse modo também conta o tempo das animações. Ainda não há ossos,
som ou renderização de personagem. A idade explícita evita selecionar criança
a partir do zero inicial do probe antigo.

Com `--sound-probe`, o CLI resolve FWAV e registra pedidos não bloqueantes,
com backend `none` e status `skipped_no_backend`; não reproduz áudio.
O probe adulto termina o clipe e, sem recursos de reset, para na44 ID0.

Adicione `--reset-probe a2o-sit-loop.cmx.bcf` para resolver a postura sentada
pela STR150 global e manter seu loop. `--post-ticks 240` avança apenas os
controladores após o fim/parada da VM, preservando seu status; não conclui a ação.
O probe real com reset completa o BHAV selecionado, saída0/returned_true,
com avatar no slot e alinhado à direção da cadeira. A notificação final é
`no_thread` quando a thread principal não é solicitada.
Personagens carregando objetos e gestos das mãos ainda exigem implementação.

`--stand-after --animation-extra adult-chair-stand-new.cmx.bcf` executa depois
a função Stand do OBJD legado selecionado, preservando o mesmo World.
O probe barato retorna true nas duas fases, esvazia o slot e termina com postura0.
O despacho é do host de teste; ainda não é o scheduler/queue completo do jogo.
As fases e o orçamento global aparecem no JSON. O loop após Stand é registrado
conforme a ordem efetiva do BHAV, sem forçar uma pose visual de pé.

Acrescente `--main-probe --hold-ticks 120` à sequência para executar também
a Main da cadeira, acordá-la pela notificação ao sentar e testar o conforto
enquanto o avatar permanece sentado. O JSON separa as duas VMs e informa
conforto, espera e orçamento compartilhado. Esse host ainda não implementa
queue geral, decaimento de motivos ou RNG real; veja [a validação](docs/main-validation.md).

`reports/` e os diretórios de build são ignorados pelo Git. Não inclua executáveis,
ISOs, recursos comerciais, saves, extrações ou bancos de análise do original no Git.

Para identificar e decodificar os recursos da animação adulta da cadeira:

```sh
./build/odh-strings '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 129 1 3
./build/odh-strings --iff '../referencias/The Sims/GameData/UIText.iff' 148 14 0
./build/odh-animation '../referencias/The Sims/GameData/Animation/Animation.far' adult-chair-sit-new.cmx.bcf A2O-sit-left --cfp xskill-A2O-sit-left.cfp > reports/chair-animation-cfp.json
python3 tools/compare_cfp.py '../referencias/The Sims/GameData/Animation/Animation.far' xskill-A2O-sit-left.cfp reports/chair-animation-cfp.json
```

Essas consultas preservam texto bruto e bits float32; não reproduzem os ossos ou
executam eventos. A seleção de adulto é explícita: a idade ainda não foi
inicializada no probe anterior. `odh-strings` suporta formatos 0..-3; `odh-animation`
suporta BCF binário com seções skeleton/appearance vazias. Ambos retornam 2 em erro.

`odh-strings --iff` lê um `STR#` de um arquivo IFF solto sem extrair ou copiar
o recurso; como o modo FAR, ele mostra os bytes originais e não converte o
idioma para Unicode. `odh-pose` lê o skeleton binário, aplica os canais de um
clipe/CFP e compõe as transformações dos ossos. A seleção dos recursos é
explícita. Pode exportar um SVG de diagnóstico; isso ainda não desenha as meshes,
roupas ou o cenário do jogo.

```sh
./build/odh-pose '../referencias/The Sims/GameData/Animation/Animation.far' adult-skeleton.cmx.bcf adult --animation '../referencias/The Sims/GameData/Animation/Animation.far' adult-chair-sit-new.cmx.bcf a2o-sit-left xskill-A2O-sit-left.cfp --frame 80.5 --svg reports/pose-adult-80_5.svg > reports/pose-adult-80_5.json
python3 tools/compare_pose.py '../referencias/The Sims/GameData/Animation/Animation.far' adult-skeleton.cmx.bcf reports/pose-adult-80_5.json reports/chair-animation-cfp.json
```

O verificador compara campos brutos exatamente e poses numericamente; o CFP do
relatório deve ter passado pelo gate `compare_cfp.py` acima. Detalhes e limites
estão em [pose-validation.md](docs/pose-validation.md).

`odh-mesh` lê uma appearance/BMF e aplica skinning CPU à pose. Exporta JSON e,
opcionalmente, OBJ com vertices/UVs/normals; ainda sem texturas ou renderer de jogo.

```sh
./build/odh-mesh '../referencias/The Sims/GameData/Animation/Animation.far' b001mafit_01.cmx.bcf b001mafit_01 xskin-b001mafit_01-PELVIS-BODY.bmf adult-skeleton.cmx.bcf adult --animation adult-chair-sit-new.cmx.bcf a2o-sit-left xskill-A2O-sit-left.cfp --frame 80.5 --obj reports/mesh-body-80_5.obj > reports/mesh-body-80_5.json
python3 tools/compare_mesh.py '../referencias/The Sims/GameData/Animation/Animation.far' b001mafit_01.cmx.bcf xskin-b001mafit_01-PELVIS-BODY.bmf reports/mesh-body-80_5.json
```

`preview_mesh.py OUTPUT.png REPORT...` produz uma prévia diagnóstica sem
texturas usando Pillow no host. Contratos e gates em
[mesh-validation.md](docs/mesh-validation.md).

`odh-render` desenha o avatar texturizado em C++ com buffer de profundidade.
Recursos de cada parte e textura são selecionados explicitamente; a câmera
fica fixa entre frames. Output PPM e JSON permitem validar imagens no Linux.

```sh
./build/odh-render '../referencias/The Sims/GameData/Animation/Animation.far' adult-skeleton.cmx.bcf adult '../referencias/The Sims/GameData/Textures/Textures.far' --part b001mafit_01.cmx.bcf b001mafit_01 xskin-b001mafit_01-PELVIS-BODY.bmf ExtremeMlgt_01.bmp --part c001ma_ross.cmx.bcf c001ma_ross xskin-c001ma_ross-HEAD-HEADB.bmf C001MAlgt_Ross.bmp --animation adult-chair-sit-new.cmx.bcf a2o-sit-left xskill-A2O-sit-left.cfp --frame 160.75 --lit --output reports/render-native-160_75.ppm
```

O renderer ainda está isolado do World/playback e da janela/input; câmera e
fidelidade visual original continuam por validar. Decoder BMP, limites,
texels conferidos e resultados estão em [render-validation.md](docs/render-validation.md).

## Documentação

- [Arquitetura e próximos experimentos](docs/architecture.md)
- [Perfil dos formatos suportados e lacunas](docs/formats.md)
- [Campos de colisão do OBJD](docs/object-collision-validation.md)
- [Geometria de rota sobre arquitetura salva](docs/route-geometry-validation.md)
- [Comparação da rota no cômodo com Simitone](docs/same-room-routing-simitone-comparison.md)
- [Segmentação geométrica de salas salvas](docs/room-map-validation.md)
- [Preparo de pisos/paredes salvos com aliases explícitos](docs/prepared-architecture-validation.md)
- [Resolução explícita dos aliases FLRm/WALm](docs/architecture-alias-validation.md)
- [Candidatos geométricos de obstáculos por sala](docs/room-obstacles-validation.md)
- [Validação do primeiro protótipo](docs/validation.md)
- [Validação das tabelas de interação](docs/ttab-validation.md)
- [Núcleo da VM e limites de validação](docs/vm-validation.md)
- [Entidades, slots e probe da cadeira](docs/world-validation.md)
- [Dados SLOT e recurso de roteamento](docs/slot-validation.md)
- [Suspensão da VM para serviços](docs/services-validation.md)
- [Cálculo de percurso na malha](docs/route-validation.md)
- [Percurso ligado à VM e ao World](docs/routing-validation.md)
- [Snap, definição do objeto e ocupação](docs/snap-validation.md)
- [Recursos de animação e próximo contrato da VM](docs/animation-validation.md)
- [Skeleton, interpolação e transformações da pose](docs/pose-validation.md)
- [Appearances, BMF e skinning CPU](docs/mesh-validation.md)
- [BMP e rasterização texturizada CPU](docs/render-validation.md)
- [PALT, SPR2 com alpha/Z e grupos DGRP](docs/sprite-validation.md)
- [Projeção e composição CPU de avatar/objeto](docs/composition-validation.md)
- [World/playback e preview Linux X11](docs/live-view-validation.md)
- [Execução silenciosa dos frames/eventos](docs/playback-validation.md)
- [Teste de tipo e pedidos de som](docs/object-sound-validation.md)
- [Seleção HIT e limite do perfil FreeSO](docs/hit-selection-validation.md)
- [Jump HIT no executável distinto e probe literal explícito](docs/hit-jump-binary-validation.md)
- [Variáveis de origem dos pedidos de som no perfil Simitone](docs/sound-source-variable-validation.md)
- [Callback HIT por origem/campo e diferenças no executável fornecido](docs/hit-source-callback-validation.md)
- [Argumentos HIT explícitos e consulta à origem do World](docs/hit-invocation-validation.md)
- [Saída PCM Linux opcional e validação do backend](docs/linux-audio-validation.md)
- [Probe XA para saída Linux com fila limitada](docs/xa-play-validation.md)
- [Lifecycle persistente HIT: NoteOn, WaitSamp e End](docs/hit-thread-validation.md)
- [Origem de som ligada ao World com geração e leitura viva](docs/sound-source-binding-validation.md)
- [Coleção de threads de som e cancelamento por remoção do dono](docs/sound-thread-collection-validation.md)
- [Factory de eventos, tracks e inicialização HIT](docs/sound-factory-validation.md)
- [Catálogo XA com fontes FAR/avulsas e proveniência](docs/xa-patch-catalog-validation.md)
- [Mixer PCM portátil, limites e consumo por nota](docs/pcm-mixer-validation.md)
- [Transporte PCM compartilhado e cancelamento na saída Linux](docs/pcm-transport-validation.md)
- [Ownership de notas HIT e integração da factory XA com BHAV](docs/pcm-hit-owner-validation.md)
- [Áudio diagnóstico no loop BHAV Linux](docs/bhav-audio-validation.md)
- [Evidência estática dos argumentos nativos de PlaySound](docs/hit-native-caller-arguments.md)
- [Resolução HOT/caller e uso no probe](docs/hit-caller-resolution-validation.md)
- [Evidência estática dos controles HIT e ordem de inicialização](docs/hit-native-controls-evidence.md)
- [Controles HIT brutos antes do primeiro tick e perfil ArgStyle2](docs/hit-native-controls-validation.md)
- [Verificação do retrigger nativo e defaults 50–62](docs/hit-native-retrigger-validation.md)
- [Evidência estática do retrigger HIT nativo](docs/hit-native-retrigger-evidence.md)
- [Lifecycle nativo: execução, áudio corrente e Clock](docs/hit-native-controller-lifecycle-evidence.md)
- [Controller HIT nativo: verificação e limites do subconjunto](docs/hit-native-controller-validation.md)
- [Catálogo nativo de entradas e chamadas entre arquivos HIT](docs/hit-native-entry-map-validation.md)
- [Banco proprietário HIT/HOT e namespace HSM nativo](docs/hit-resource-bank-validation.md)
- [Objetos globais HIT: Track/Patch, colisões e origens distintas](docs/hit-object-catalog-validation.md)
- [Binding global de Patch e backend compartilhado por geração](docs/hit-global-patch-binding-validation.md)
- [Controles nativos de áudio antes do wrapper](docs/hit-native-audio-controls-validation.md).
- [PCM reutilizável e Patch XA global](docs/hit-reusable-pcm-validation.md).
- [Ordem de descoberta dos grupos no executável original](docs/hit-native-load-order-evidence.md)
- [Backend original: pausa, retomada e compartilhamento](docs/hit-native-engine-backend-evidence.md)
- [Opcode HIT 69 nativo: registro de tracks, fade e relógio](docs/hit-native-group-kill-evidence.md)
- [Identidade e ciclo de TrackID no HIT nativo](docs/hit-native-track-identity-evidence.md)
- [Tabela portátil de contribuições HIT por TrackID](docs/hit-source-contributions-validation.md)
- [Evidência estática das contribuições nativas por TrackID](docs/hit-native-source-contributions-evidence.md)
- [Reset de postura e loops persistentes](docs/reset-validation.md)
- [Snap de direção e retorno do BHAV da cadeira](docs/facing-validation.md)
- [Sequência de sentar e levantar no mesmo mundo](docs/stand-validation.md)
- [Referências e atribuição](docs/references.md)

O contexto, decisões e handoffs também são mantidos na OMM, escopo
`OpenDollhouse`. Revalidar informações que dependam de arquivos ou revisões.

## Licenciamento

A licença do código do ODH ainda será definida. Não presumir que a licença de uma
referência se aplica a este projeto. Reutilização futura de código de terceiros
requer verificar a licença dos arquivos e dependências. Dados comerciais do jogo
devem ser fornecidos pelo usuário e não são distribuídos com o ODH.
