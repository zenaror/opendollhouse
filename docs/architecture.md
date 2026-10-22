# Arquitetura em investigação

ODH pretende manter um núcleo C/C++ compartilhado, com renderizadores e integração
de sistema específicos por plataforma. C++17 é a escolha do primeiro protótipo;
toolchains e orçamentos dos consoles ainda não foram validados.

Decisão de 2026-10-13: desenvolver e validar primeiro uma versão funcional Linux,
antes de implementar os ports New 3DS/Vita/Android. A meta ativa inclui leitores,
VM, renderização, interação e lote mínimo, não apenas o inspector host atual.
Os constraints do New 3DS continuam considerados na arquitetura; a execução em
Linux não comprova viabilidade ou desempenho nos portáteis.

Decisão de planejamento de 2026-10-22: a primeira build Linux jogável deve
permitir escolher o idioma do jogo. Priorizar os grupos de texto presentes nos
dados instalados, guardar uma escolha simples e usar inglês como fallback para
uma frase ausente. A validação inicial será uma checagem rápida de textos
principais da interface; não exige auditoria exaustiva de todas as traduções.
O arquivo `GameData/UIText.iff` da instalação de referência contém 74 tabelas
STR# no formato -3 e grupos com códigos 1–20; isso comprova disponibilidade de
textos nesses grupos, não cobertura completa de cada idioma. O
[inventário](language-resource-inventory.md) registra o arquivo, a contagem e
amostras das codificações. Os parsers continuam guardando os bytes originais;
na borda Linux, o helper converte esses bytes para UTF-8 e PangoCairo desenha
uma faixa curta sobre a cena, usando fontes instaladas no sistema. A interface
deve exibir os grupos disponíveis na instalação (códigos 1–20), sem reduzir a
lista aos idiomas de implementação mais fácil. Os
consumidores atuais também fixam o inglês em alguns caminhos, então a seleção
deve chegar aos textos da interface e às interações antes do milestone jogável.
O preview Linux aceita `--language 1..20` e mostra o rótulo da ação selecionada
no título e em uma faixa desenhada dentro da cena. Isso é uma escolha temporária
de execução para teste, não uma preferência persistida nem um seletor dentro do
jogo; a interface jogável ainda precisa de opção própria.

Decisão de planejamento de 2026-10-20: após a primeira versão funcional Linux e
os ports prioritários New 3DS/Vita/Android, manter Windows e macOS como alvos
futuros de build. Incluir Windows desktop e macOS para Apple Silicon (arm64) e
Intel (x86_64); toolchains, empacotamento e backends ainda precisam ser definidos.
O usuário não dispõe de máquinas Windows ou Mac para validar essas builds; elas
exigirão CI ou colaboradores com esses sistemas e não serão declaradas funcionais
com base apenas na compilação Linux.

Decisão de planejamento de 2026-10-21: incluir Nintendo Switch como possível
build futura, após o candidato Linux e os ports prioritários. Toolchain,
permissões de distribuição, backends e validação em hardware não foram
investigados nem confirmados; a meta não implica compatibilidade atual.

Decisão de entrada de dados de 2026-10-14: o motor lê assets fornecidos pelo
jogador, sem depender de executável original ou realizar checagem de CD.
Priorizar a árvore de dados copiados/extraídos tanto no Linux quanto no
armazenamento dos portáteis. ISO é uma fonte de preparação; a imagem local
contém cinco CABs de instalação `Setup/data1.cab`–`data5.cab`, sem FAR soltos.
Importação dos CABs/leitura direta da ISO é trabalho pendente, separado do
runtime. O preparo deve preservar a fonte, produzir arquivos com caminhos e
identidades verificáveis e reportar recursos ausentes. Isso não distribui
dados comerciais com o ODH nem exige um leitor óptico no dispositivo alvo.

O núcleo deve poder executar testes sem janela, GPU ou áudio. A interface envia
ações lógicas. Simulação, scheduler e renderização têm relógios e responsabilidades
separados. Resources resolve origem/precedência e fornece representações com cache
limitado; Audio recebe eventos; Platform abstrai I/O, entrada e serviços.

O preparo de arquitetura salva possui um tipo separado e grids limitados:
[pisos/paredes preparados](prepared-architecture-validation.md) recebem aliases
de recursos explícitos, flags térreas, road flip e crop antes da segmentação.
O [catálogo explícito](architecture-resource-catalog.md) e a
[integração de materiais externos no render](external-architecture-renderer-validation.md)
resolvem arquivos loose/FAR e pintura por zoom. O
[snapshot CLI de cena salva](saved-scene-external-architecture-validation.md)
compõe pisos/paredes/objetos reais de House23 sem avatar e admite preview
explícito de pisos sem restaurar World. O limite de retenção ainda bloqueia
House30; pool/water também continua fora do render. Entidades/portais vivos ainda
faltam para transformar os obstáculos candidatos em runtime de lote.
Os mapas FLRm/WALm podem ser ligados a catálogos explicitamente ordenados por
[resolução de aliases](architecture-alias-validation.md); a descoberta e
precedência dos próprios assets de instalação seguem externas a essa API.
Uma [grade candidata de rota por sala](saved-room-route-grid-validation.md)
combina pertencimento geométrico e obstáculos fornecidos em um orçamento
limitado; RoomObs/WallObs têm geradores separados e runtime/portas ainda
impedem usar a grade como clearance.
O diagnóstico House07 agora também liga cada ponto SLOT à descoberta ordenada
e filtrada pela sala geométrica de overlaps de footprints de entidades e ao
classificador de GetAvatarPlace
([evidência e limites](saved-avatar-place-discovery-validation.md)); BHAVs,
obstáculos estáticos/dinâmicos registrados e clearance runtime continuam
pendentes.
O [índice RoomInfo](room-info-obstacle-index-validation.md) define registro,
movimento e reclassificação owner-keyed dos footprints estáticos/dinâmicos;
continua sem política completa de clearance.
`World::Entity::static_footprint` representa a classe runtime da entidade e
`Routing` troca o ator para dinâmico no primeiro passo, restaurando-o ao sair
da rota conforme o perfil Simitone fixado. O índice RoomInfo não é persistido:
cada consulta o reconstrói a partir do World vivo, mas o caminho de clearance
da rota ainda não chama essa consulta.
O builder de snapshot salvo agora espelha a composição de obstáculos de
`VMRoutingFrame.AttemptWalk` até a etapa anterior ao `VMRectRouter`: WallObs,
RoomObs, faixas de limite, flags, margem de footprint e EP5 bilateral. Ele
falha fechado diante de entidade/porta sem geometria runtime comprovada e não
alimenta Route45/Snap46.
[`query_world_room_info`](world-room-info-validation.md) reconstrói o índice
diretamente do World antes de cada consulta, usando resolvers explícitos para
salas e KillTimeout de avatares. O saved-world preview consulta candidatos
usando mapa de sala congelado e estado live do World; o auditor classifica
somente overlap entre entidades usando flags e IDs de EntryPoint. Para EP5,
executa em World temporário apenas uma forma read-only estritamente allowlisted;
outras formas ficam unsupported. Não avalia arquitetura/portais nem decide a
posição final.
A API ainda não alimenta Route45 ou Snap46.

Saída PCM Linux inicial em `odh_linux_audio`, opcional via `ODH_SDL_AUDIO`,
com fila limitada e SDL2 carregado em runtime. Validação dummy e entrega curta
ao PulseAudio estão em [linux-audio-validation.md](linux-audio-validation.md).
Probe XA->PCM Linux com cursor e fila limitada em
[xa-play-validation.md](xa-play-validation.md). A ligação ao scheduler do jogo
continua pendente. Lifecycle HIT portátil
inicial (NoteOn/WaitSamp/End) e integração isolada com PCM estão em
[hit-thread-validation.md](hit-thread-validation.md); isso ainda não retoma Main.
Leitura viva dos bancos da origem usa ligação protegida por ID/geração em
[sound-source-binding-validation.md](sound-source-binding-validation.md).
Coleção portátil com ticks explícitos, capacidade limitada e cancelamento por
remoção/IDreuse em [sound-thread-collection-validation.md](sound-thread-collection-validation.md).
Factory de perfil HOT/bytecode explícito em
[sound-factory-validation.md](sound-factory-validation.md), validada também com
recursos nativos e estado World construído. Catálogo host de patches XA em
FAR/arquivos explícitos em [xa-patch-catalog-validation.md](xa-patch-catalog-validation.md).
Mixer portátil com orçamento PCM agregado, handles por nota e confirmação de
consumo em [pcm-mixer-validation.md](pcm-mixer-validation.md), validado isoladamente
com HIT e duas XA nativas em SDL dummy. Transporte compartilhado, prefixos e
cancelamento em [pcm-transport-validation.md](pcm-transport-validation.md), com
sink Linux opcional e core independente de SDL. Ownership por thread e factory
XA em [pcm-hit-owner-validation.md](pcm-hit-owner-validation.md), com execução
BHAV sintética ligada à coleção/transporte. O probe BHAV tem integração Linux
diagnóstica em [bhav-audio-validation.md](bhav-audio-validation.md), com argumentos
e controles explícitos; a cena construída da cadeira pode emitir PCM dos dados
nativos pelo loop existente. Derivação bruta do caller/evento opcional em
[hit-caller-resolution-validation.md](hit-caller-resolution-validation.md).
O perfil opcional fresco ArgStyle2 instala controles brutos antes do primeiro
script em [hit-native-controls-validation.md](hit-native-controls-validation.md),
com entradas espaciais explícitas, defaults/controllers e flags iniciais.
Locais desconhecidos falham ao serem lidos; o estado bruto não aplica DSP.
Nesse perfil, opcode69 também falha explicitamente: o callback legado recebe
um byte de grupo, enquanto a evidência nativa exige resolução de variável,
registro por TrackID, fade e retry por Clock. O contrato investigado está em
[hit-native-group-kill-evidence.md](hit-native-group-kill-evidence.md).
Pesquisa estática posterior confirmou que o parser HOT cria e registra um
controller por TrackID, que o dispatch de evento tipo 1 procura/reutiliza esse
objeto e que um multimap separado mantém contribuições `(TrackID, R1)`.
Admission pode recusar antes da atualização; inicialização pode atualizar
controles sem reiniciar PC/bancos, e remoção de fontes não equivale à destruição
do controller. Isso não define ownership por pedido nem o ciclo completo de
remoção/reload. Ver [identidade e ciclo de TrackID](hit-native-track-identity-evidence.md).
Na imagem nativa, o cleanup de `cXObject` chama a remoção da fonte; remoção
normal, fontes especiais e refresh estão descritos na evidência estática de
contribuições. Isso não significa que o runtime ODH esteja integrado a esse
ciclo.
O builder fresh conhece os bancos HIT e `HitThread` já oferece uma atualização
local sem reset, descrita em [hit-native-retrigger-validation.md](hit-native-retrigger-validation.md).
A implementação seguinte é um `NativeHitController` portátil com bancos
separados 1–16, 17–47 e 50–62, execução pelo Clock explícito e callbacks para
o binding de áudio corrente. Contratos, subconjunto de opcodes e limites estão
em [hit-native-controller-validation.md](hit-native-controller-validation.md);
a pesquisa estática correspondente está em
[hit-native-controller-execution-evidence.md](hit-native-controller-execution-evidence.md).

O controller não integra o manager persistente por HOTTrackID, World ou host de
áudio do probe. O caller ainda precisa resolver admissão, entrypoint e gates,
manter os tempos de vida e fornecer a cadência do Clock. Call7 agora recebe um
resolvedor explícito de chave para arquivo HIT e PC e usa pilha limitada com
identidade de recurso. O `HitEntryCatalog` indexa `TrackData` de grupos em ordem
fornecida pelo caller; definições posteriores substituem as anteriores. O perfil
HOT `native_entry_map` não exige resolução local e conta substituições internas;
o perfil Simitone continua como padrão. A descoberta nativa percorre raízes na
ordem de chamada e arquivos na ordem devolvida por Win32, sem sort interno.
Isso não determina uma ordem concreta de arquivos no Linux; uma sequência
explícita preserva a precedência escolhida. Contrato e testes em
[hit-native-entry-map-validation.md](hit-native-entry-map-validation.md).

O `HitResourceBank` possui os HIT/HOT, mantém estáveis seus endereços e
constrói o catálogo depois de acumular HSM → HIT → HOT em cada grupo.
O perfil HSM `native_namespace` preserva caixa e mantém a última definição
de cada nome exato; cada HOT retém os números resolvidos quando foi lido. Os streams de
entrada só precisam viver durante a construção. Limites por arquivo e
agregados não medem toda a memória alocada; contrato e validação em
[hit-resource-bank-validation.md](hit-resource-bank-validation.md).

O banco também possui `NativeHitObjectCatalog` em `objects()`. Track e Patch
compartilham um namespace de chaves no original; o leitor nativo conserva
registros por linha para que colisões entre tipos sigam a ordem processada.
`resolve_global_track_event` separa HOT do EventMapping, HOT do Track vencedor
e HIT da entrada. A Patch pode vir de outro HOT; usar o HOT do Track como
namespace de Patch não é contrato nativo. O lookup local anterior de
`HitEntryCatalog::resolve_event` continua explícito para diagnósticos.
Ordem, ownership e limites do índice global estão em
[hit-object-catalog-validation.md](hit-object-catalog-validation.md).

`NativeHitController` oferece agora `NativeHitPatchSource::global_catalog`,
separado do perfil local padrão. A inicialização distingue Patch adquirida,
zero, ausente e tipo incorreto sem voltar ao HOT local. Dois callbacks de
controles preservam a aplicação antes/depois de copiar o ID26 efetivo da
Patch. O `NativeHitPatchRegistry` possui um backend por Patch ativa/geração,
com referências de binding e recurso separadas e views protegidas por geração.
Pausa solicita Stop; resume não reinicia áudio. O adapter concreto e o
manager ainda precisam ligar essa infraestrutura ao host Linux, às fontes
World e ao Clock. Contratos, capacidades e validação em
[hit-global-patch-binding-validation.md](hit-global-patch-binding-validation.md).
O [backend nativo de áudio](hit-native-engine-backend-evidence.md) inspecionado
para buffers na pausa e não reinicia áudio no slot de retomada. Wrappers da
mesma patch podem compartilhar esse backend. Ainda falta integrar um adapter
com ownership e conversão de controles comprovados; não presumir pausa por
congelamento de cursor nem exclusividade por controller. A
[pesquisa de ownership](hit-native-backend-ownership-evidence.md) identificou
um caminho espacial com dois aliases, um crédito explícito e dois releases;
o desequilíbrio estático não é reproduzido como política de memória ODH.
Opcode1/+194, backend de vozes/DSP, provider espacial e comportamento geral de
IDs não entram no subconjunto. Não tratar conclusão de binding/nota como término
do controller, usar `HitThread::tick` como equivalência de lifecycle, ou mapear
este objeto a uma thread por pedido. Core direto inicial (82 checks), CTest e oráculo sintético
passaram; métricas, logs e limites atualizados estão em
[hit-native-controller-validation.md](hit-native-controller-validation.md). Não
houve probe da cadeira ou teste de backend físico. Resultados Linux não comprovam
compatibilidade nem validam New 3DS, Vita ou Android.

A tabela portátil `odh_hit_source_contributions` agora armazena pares raw
`(TrackID, R1)` com capacidade ODH explícita e oferece agregação pura de
amostras completas, com wrap de 32 bits. O contrato e seus limites estão em
[hit-source-contributions-validation.md](hit-source-contributions-validation.md);
a comparação estática com o original está em
[hit-native-source-contributions-evidence.md](hit-native-source-contributions-evidence.md).
Essa API ainda não integra o controller persistente, o World ou o áudio. Ela
não valida grupos, geometria/provider, compatibilidade ou funcionamento nos
ports.

A VM inicial deverá interpretar os BHAVs. Entidades, roteamento, ocupação,
autonomia, necessidades, relacionamentos e serviços de famílias não se reduzem
ao despacho de opcodes. Não introduzir pausas arbitrárias de execução ou
primitivas desconhecidas que retornam sucesso silenciosamente.

Classic e Extended compartilham mecanismos, com regras, conteúdo habilitado e
persistência identificados. SDK, Studio e formato `.simpack` ficam para interfaces
estabilizadas. Apartamentos não fazem parte do primeiro escopo.

## Experimentos e critérios

1. **Identidade e leitores:** tornar a leitura estrutural reproduzível, preservar
   inputs e diagnosticar variantes não suportadas. Primeiro protótipo implementado.
2. **Interação controlada:** resolver TTAB comprimida e dependências da cadeira
   `DiningChairs.iff`, GUID `0x26BFBB29`. Antes da execução, determinar o significado
   dos operandos, contexts e retomadas usados pelo cenário.
3. **VM:** núcleo inicial de chamadas/expressões com traces implementado;
   entidades/slots, serviços de rota/snap/playback e duas VMs no probe implementados;
   scheduler geral e queue continuam pendentes.
   A VM já aceita aguardar uma conclusão externa sem reexecutar a primitiva;
   o host liga controladores por ticks; passagem geral de tempo ainda falta.
   Traces de instruções, chamadas e estados com fixtures sintéticas e
   cenários originais identificados. Simitone e seu submódulo em caminhos TS1
   são a referência técnica prioritária; FreeSO principal é comparativo.
   Decisões de fidelidade precisam de evidência do original.
4. **Composição gráfica Linux cedo:** sprites com profundidade, personagens
   animados. Skeleton/interpolação/matrizes iniciais implementados em
   [pose-validation.md](pose-validation.md), e meshes/skinning iniciais em
   [mesh-validation.md](mesh-validation.md). BMP/raster texturizado isolado está em
   [render-validation.md](render-validation.md); integração World/playback,
   sprites alpha/Z e DGRP estão em [sprite-validation.md](sprite-validation.md);
   projetor e compositor CPU estão em [composition-validation.md](composition-validation.md).
   Preview ligado à simulação e janela/input X11 estão em
   [live-view-validation.md](live-view-validation.md); inicialização/encerramento
   de poses e cena/lote completo ainda pendentes.
   O objetivo inclui paredes e sobreposição, com capturas e cenas
   reproduzíveis. Conservar
   uma interface que permita experimentar software, híbrido CPU/GPU e geometria
   derivada no futuro backend New 3DS. PICA200 não oferece fragment shader
   programável equivalente ao das APIs modernas; não fazer o núcleo depender dele.
5. **Lote mínimo:** tempo, interação completa, necessidades, compra/construção
   delimitada e persistência ODH. Separar importação de saves, persistência ODH e
   exportação consumível pelo original.
6. **Cobertura:** sistemas clássicos, expansões e conteúdo personalizado por
   cenários/versionamento; depois extensões e ferramentas.
7. **Ports prioritários:** transportar o núcleo validado para New 3DS, Vita e
   Android, implementar backends e controles próprios; medir memória, CPU,
   renderização e carregamento em hardware real.
8. **Builds futuras:** avaliar Nintendo Switch e adicionar Windows e macOS
   arm64/x86_64 após a base Linux e os ports prioritários. Para cada alvo,
   definir toolchain, backend e empacotamento; validar em CI, máquinas dedicadas
   ou hardware compatível. Switch é apenas uma possibilidade de build: suporte
   de toolchain, distribuição e validação em hardware ainda não foi confirmado.
   O usuário não tem equipamento para testes manuais de Windows ou macOS.

Primeira versão Linux candidata a teste: abrir um cenário controlado com recursos
originais externos, exibir a cena, executar uma interação completa (sentar/levantar),
avançar tempo/necessidades no escopo definido e salvar/restaurar estado ODH.
Os resultados e lacunas devem acompanhar a versão; não chamar isso de suporte
completo a TS1 ou às sete expansões. Testes diferenciais contra o original têm
identidade de executável/recursos/save inicial e ações registradas.

Metas sugeridas para o primeiro cenário New 3DS: 30 FPS, p95 de frame até 33,3 ms,
simulação sem descartar ticks e memória até o menor valor entre 96 MiB e 80% do
orçamento medido. São critérios propostos, não resultados ou limites universais.

Riscos principais: oclusão fiel na PICA200; serviços implícitos em BHAV; ordem de
resolução; persistência e viagens; diferenças de edições/expansões; picos de
decodificação. Fora da tela pode-se descartar representação gráfica, preservando
estado e comportamento das entidades.

O cálculo pré-wrapper dos controles nativos está separado da saída PCM:
[contrato e validação](hit-native-audio-controls-validation.md). O perfil global
entrega snapshots completos nos setters19/20, com pedido A-only; registry
preserva placement antes gain e pitch separado. Patch43/26/27 são opcionais
preservados pelo parser nativo, e valores ausentes não viram defaults implícitos.
Backends concretos ainda precisam implementar despacho e transformações DSP.

O transporte possui [recursos PCM reutilizáveis](hit-reusable-pcm-validation.md)
com orçamento até release, replay no mesmo handle, loop e mudança de taxa por
fase consumida. O loader aceita Patch global diretamente. Essas operações dão
base ao backend compartilhado, mas source classifier, dirty/preload, controles
perdidos, loop latch e callback matrix nativos ainda exigem o adapter concreto.
