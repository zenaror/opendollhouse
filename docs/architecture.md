# Arquitetura em investigação

ODH pretende manter um núcleo C/C++ compartilhado, com renderizadores e integração
de sistema específicos por plataforma. C++17 é a escolha do primeiro protótipo;
toolchains e orçamentos dos consoles ainda não foram validados.

Decisão de 2026-10-13: desenvolver e validar primeiro uma versão funcional Linux,
antes de implementar os ports New 3DS/Vita/Android. A meta ativa inclui leitores,
VM, renderização, interação e lote mínimo, não apenas o inspector host atual.
Os constraints do New 3DS continuam considerados na arquitetura; a execução em
Linux não comprova viabilidade ou desempenho nos portáteis.

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
Geometria e agregação espaciais, provider compartilhado, precedência/descoberta,
grupos/owners/ganho espacial e scheduler geral do lote permanecem pendentes.

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
7. **Ports:** transportar o núcleo validado, implementar backends e controles
   próprios; medir memória, CPU, renderização e carregamento em hardware real.

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
