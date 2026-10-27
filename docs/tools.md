# Compilação, ferramentas e inspeção

Exemplos executados a partir da raiz do checkout. A pasta externa
`../referencias/The Sims` é apenas um exemplo de instalação fornecida pelo
usuário: ajuste os argumentos à sua pasta. Saídas em `validation-output/` são
criadas ao executar os comandos; não são arquivos necessários ao clone.
Os cenários são experimentais, com os limites descritos em cada documento.

## Build e testes

Requisitos desta etapa: compilador C++17 e CMake 3.16 ou superior. No Linux,
Iconv converte textos antigos do jogo. A janela experimental precisa dos arquivos de
desenvolvimento X11; PangoCairo com Cairo é recomendado para desenhar menus e rótulos usando
fontes do sistema. Em distribuições Debian/Ubuntu, os pacotes são `libx11-dev`
e `libpango1.0-dev`. O build padrão exige X11; sem PangoCairo, usa a fonte bitmap incorporada,
com limitações de alfabetos e acentos. Os scripts
auxiliares usam Python 3.8 ou superior, somente com a biblioteca padrão.

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native -j 2
ctest --test-dir build/native --output-on-failure
```

`-DODH_X11=OFF` compila sem janela e não exige Pango. Com X11 ligado, PangoCairo
é usado quando disponível; sem ele, a janela mantém rótulos com uma fonte
bitmap incorporada. Essa alternativa translitera acentos latinos comuns e
mostra `?` para caracteres de outros alfabetos, então não substitui o suporte
multilíngue completo do PangoCairo. `-DODH_LINUX_TEXT_RENDERER=OFF` seleciona
essa alternativa sem tentar localizar PangoCairo. Nenhum conjunto de fontes
comerciais ou de terceiros é copiado ao projeto. Comandos de janela e seus
limites estão em
[live-view-validation.md](live-view-validation.md).

Os testes geram fixtures sintéticas na memória; não dependem dos dados comerciais.

O gate de integração CAS para lote vago é explicitamente opt-in e não faz parte
do CTest. Ele cria uma família de dois Sims com o catálogo CAS e
`TemplatePerson.iff`, compra um lote base vago pelo preço SIMI, inicia os dois
no runtime, salva após 16 ciclos e retoma em outro processo. O save retomado é
comparado byte a byte com 32 ciclos sem interrupção. O perfil do jogador é
temporário e os dados instalados são verificados por SHA-256 antes/depois.

```sh
cmake --build build/native --target odh-cas-movein-e2e odh-person
python3 tests/cas_movein_e2e.py \
  --runner build/native/odh-cas-movein-e2e --person build/native/odh-person \
  --game-root '../referencias/The Sims'
```

## Inspecionar uma instalação fornecida pelo usuário

Mantenha a instalação fora deste checkout. O exemplo abaixo pressupõe uma pasta
irmã `referencias/The Sims`. Feche o jogo enquanto registra o corpus de referência.

```sh
mkdir -p reports
python3 tools/fingerprint.py '../referencias/The Sims' > validation-output/input-manifest.json
./build/native/odh-inspect '../referencias/The Sims' > validation-output/corpus.json 2> validation-output/corpus-errors.txt
python3 tools/check_census.py validation-output/corpus.json
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
./build/native/odh-ttab '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 128
```

O nome da entrada é exato; o ID aceita decimal ou prefixo `0x`. A ferramenta
rejeita seleção ambígua. Saída 0 indica tabela decodificada ou contagem vazia,
3 indica payload ausente ou versão legada não suportada, 2 indica erro.
O JSON preserva bits restantes e não executa os BHAVs indicados pela tabela.

Para inspecionar um BHAV ou fazer uma execução limitada sem serviços do mundo:

```sh
./build/native/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1002
./build/native/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run
```

O modo `--run` emite trace e status JSON. Sem entidades, a cadeira para com
diagnóstico de scope não suportado, saída 3. Saída 0 indica retorno true/false
da rotina; 2 indica erro de uso/recurso. Há um orçamento de 10.000 instruções,
distinto de ticks do jogo. A biblioteca preserva o estado para retomada ao
esgotar o orçamento. Recursos globais opcionais são fornecidos explicitamente
com `--global ARCHIVE.far ENTRY.iff`; não há busca nem precedência automática.
`--semiglobal ARCHIVE.far ENTRY.iff` carrega um semiglobal ligado pelo GLOB128
Pascal do code owner, com validação do nome e dos scopes. O JSON registra o
vínculo. O [contrato de Main/idle do avatar](person-lifecycle-validation.md)
descreve os caminhos nativos encontrados e os limites da execução atual.
A biblioteca possui [fila limitada e retorno ao Main](interaction-validation.md)
com gate do wrapper global281 nativo; o preview ainda usa o host anterior.
`odh-person` executa o Main ou uma rotina explícita do avatar com owners,
Sleep e fila no mesmo relógio. O [gate do host de pessoa](person-host-validation.md)
registra execução nativa parcial. O [tuning por owner](tuning-validation.md)
carrega BCON explícitos. O [contexto de lote](lot-validation.md) carrega SIMI
e zoning externos; Main e Init atravessam suas primeiras consultas de lote.
Inicialização completa e carregamento da casa como simulação seguem pendentes.
A [importação de entidades salvas](saved-world-validation.md), com
`odh-saved-world`, agora cria um World próprio com IDs, bancos, posições,
contenção e relacionamentos. Os scripts salvos permanecem dormentes; a execução
do lote completo ainda está pendente.
O [host de avatares salvos](saved-avatar-render-validation.md),
`odh-saved-scene`, renderiza corpo/cabeça/mãos e o frame salvo dos dois avatares
reais de House07 nas posições do World, em uma imagem CPU estática. A execução
integral do lote continua pendente. O [plano Linux inclui os arquivos
Downloads](linux-download-resources-validation.md), e House07 da Legacy
já carregou com a família e passou por Sentar/Levantar agendados no runtime;
janela interativa, ciclo contínuo e demais interações seguem pendentes. Com
`--objects`, o
[host compõe os objetos salvos](saved-object-architecture-validation.md),
incluindo contenção por SLOT e gráficos dinâmicos. `odh-architecture` decodifica
as grades de piso/parede e mapas de recursos. Com `--floors FLOORS_IFF`, o
[compositor de pisos e terreno](saved-floor-validation.md) desenha materiais
globais, meios pisos diagonais e aplica altitude a pisos, objetos e avatares.
Com `--terrain-theme 0`, a [base do terreno](terrain-surface-validation.md)
compõe o solo com cores salvas e normais de relevo. Temas são explícitos; grama
em lâminas, iluminação por rooms e execução do lote seguem pendentes.
Com `--walls WALLS_IFF` (junto de `--floors`), o
[snapshot de paredes erguidas](saved-wall-validation.md) aplica padrões e
máscaras reais de portas/janelas, com depth compartilhado e
[junções reais](saved-wall-cap-validation.md).
Com `--architecture-resource FILE` repetível, o
[snapshot resolve FLRm/WALm externos](saved-scene-external-architecture-validation.md)
e renderiza lotes sem avatar; House23 foi verificada com pisos, paredes e objetos
reais. `--architecture-only --floors` oferece preview explícito de pisos sem
restaurar World. Os limites de retenção e o guard de pool/water permanecem ativos.
`--level 1|2 --wall-mode up|down` seleciona o
[andar e as paredes abaixadas](wall-view-validation.md); cutaway por
proximidade e visibilidade runtime seguem pendentes.
O [leitor SPR#](wall-resource-validation.md), `odh-spr`, decodifica os padrões
e máscaras reais de paredes, com seleção IFF/FAR e paleta explícita. A [composição CPU](wall-sprite-validation.md) preserva os recortes das máscaras. `odh-wall-appearance` já prepara os
[overrides de portas/janelas](wall-appearance-validation.md) a partir dos
bindings OBJD e flags salvos, preservando os dados e sem reInit.
O [RNG compartilhado](random-validation.md) fornece Random Number e os
ciclos de Sleep com seed explícita.
Com [PersonData por temp](person-indirect-validation.md), o Init privado4097
retorna true no probe.
O [startup do avatar](avatar-startup-validation.md), via `--initialize`,
liga Init/entrada8/BodySTR ao Main preservando World e RNG. Ainda não resolve
aparências nem restaura o save completo.
O [contexto do bairro](neighborhood-validation.md), via `--neighborhood`,
fornece NBRS, enumeração e PersonData dos vizinhos. Main chega à primitiva de
relacionamentos. O [vínculo do personagem real](avatar-neighbor-validation.md),
via `--avatar-file`, restaura PersonData por GUID antes de Main. O
[backend de relacionamentos](relationship-validation.md) lê e escreve
em cópia própria de NBRS. O [bootstrap explícito](bootstrap-validation.md)
fornece GameEdition e dois globals de runtime. A
[enumeração de entidades](entity-next-validation.md) cobre pessoas, objetos
e GUIDs exatos. As [funções de entrada](entry-points-validation.md) vêm
do OBJD/OBJf da entidade, inclusive o dispatch de startup; Main chega agora
a GotoRoutingSlot no perfil visitante. A [ativação de família](family-restore-validation.md)
seleciona FAMI pela casa e classifica Bob como residente de House07; Main chega
à operação de inventário. A [consulta FindToken](inventory-validation.md)
lê NGBH e ultrapassa esse gate; o Main residente chega à animação44.
O [snapshot salvo](saved-prefix-validation.md) decodifica posições e bancos
iniciais de ObjM e resolve IDs à tabela objt. O [estado comum](object-state-validation.md)
inclui frames, relações e slots, com catálogo OBJD de fontes explícitas.
A [cauda conhecida](saved-tail-validation.md) inclui multitile, portais e
aparência/dados salvos dos personagens. Resolução gráfica da aparência,
publicação das entidades/posições em World e arquitetura seguem pendentes.

Um probe com entidades construídas permite investigar as branches da cadeira:

```sh
./build/native/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1002 --run --world-probe
```

O cenário tem caller 1, objeto 2 e outro avatar 3, com um slot no objeto.
`--occupant 0|1|3`, `--posture 0..32767` e `--autonomous` configuram o estado
inicial. O relatório identifica `ODH_VM_constructed_world`: os BHAVs vêm do
arquivo selecionado, mas as entidades são construídas no ODH. Não carrega
um lote/save nem comprova comportamento do executável original.

O registro de roteamento indicado pela cadeira pode ser consultado em leitura:

```sh
./build/native/odh-slot '../referencias/The Sims/GameData/Global/Global.far' Global.iff 100 3 1
```

Os últimos seletores significam tipo 3 e índice 1 dentro desse tipo. Sem eles,
a ferramenta lista todos os registros. Os valores são brutos; floats são bits
uint32 e campos ausentes da versão são `null`. Saída 0 indica leitura, 2 erro.

Para investigar a ação com percurso e ticks no cenário construído:

```sh
./build/native/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run --world-probe --route-probe --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff
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
queue geral, decaimento de motivos ou RNG real; veja [a validação](main-validation.md).

`validation-output/` e os diretórios de build são ignorados pelo Git. Não inclua executáveis,
ISOs, recursos comerciais, saves, extrações ou bancos de análise do original no Git.

Para identificar e decodificar os recursos da animação adulta da cadeira:

```sh
./build/native/odh-strings '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 129 1 3
./build/native/odh-strings --iff '../referencias/The Sims/GameData/UIText.iff' 148 14 0
./build/native/odh-animation '../referencias/The Sims/GameData/Animation/Animation.far' adult-chair-sit-new.cmx.bcf A2O-sit-left --cfp xskill-A2O-sit-left.cfp > validation-output/chair-animation-cfp.json
python3 tools/compare_cfp.py '../referencias/The Sims/GameData/Animation/Animation.far' xskill-A2O-sit-left.cfp validation-output/chair-animation-cfp.json
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
./build/native/odh-pose '../referencias/The Sims/GameData/Animation/Animation.far' adult-skeleton.cmx.bcf adult --animation '../referencias/The Sims/GameData/Animation/Animation.far' adult-chair-sit-new.cmx.bcf a2o-sit-left xskill-A2O-sit-left.cfp --frame 80.5 --svg validation-output/pose-adult-80_5.svg > validation-output/pose-adult-80_5.json
python3 tools/compare_pose.py '../referencias/The Sims/GameData/Animation/Animation.far' adult-skeleton.cmx.bcf validation-output/pose-adult-80_5.json validation-output/chair-animation-cfp.json
```

O verificador compara campos brutos exatamente e poses numericamente; o CFP do
relatório deve ter passado pelo gate `compare_cfp.py` acima. Detalhes e limites
estão em [pose-validation.md](pose-validation.md).

`odh-mesh` lê uma appearance/BMF e aplica skinning CPU à pose. Exporta JSON e,
opcionalmente, OBJ com vertices/UVs/normals; ainda sem texturas ou renderer de jogo.

```sh
./build/native/odh-mesh '../referencias/The Sims/GameData/Animation/Animation.far' b001mafit_01.cmx.bcf b001mafit_01 xskin-b001mafit_01-PELVIS-BODY.bmf adult-skeleton.cmx.bcf adult --animation adult-chair-sit-new.cmx.bcf a2o-sit-left xskill-A2O-sit-left.cfp --frame 80.5 --obj validation-output/mesh-body-80_5.obj > validation-output/mesh-body-80_5.json
python3 tools/compare_mesh.py '../referencias/The Sims/GameData/Animation/Animation.far' b001mafit_01.cmx.bcf xskin-b001mafit_01-PELVIS-BODY.bmf validation-output/mesh-body-80_5.json
```

`preview_mesh.py OUTPUT.png REPORT...` produz uma prévia diagnóstica sem
texturas usando Pillow no host. Contratos e gates em
[mesh-validation.md](mesh-validation.md).

`odh-render` desenha o avatar texturizado em C++ com buffer de profundidade.
Recursos de cada parte e textura são selecionados explicitamente; a câmera
fica fixa entre frames. Output PPM e JSON permitem validar imagens no Linux.

```sh
./build/native/odh-render '../referencias/The Sims/GameData/Animation/Animation.far' adult-skeleton.cmx.bcf adult '../referencias/The Sims/GameData/Textures/Textures.far' --part b001mafit_01.cmx.bcf b001mafit_01 xskin-b001mafit_01-PELVIS-BODY.bmf ExtremeMlgt_01.bmp --part c001ma_ross.cmx.bcf c001ma_ross xskin-c001ma_ross-HEAD-HEADB.bmf C001MAlgt_Ross.bmp --animation adult-chair-sit-new.cmx.bcf a2o-sit-left xskill-A2O-sit-left.cfp --frame 160.75 --lit --output validation-output/render-native-160_75.ppm
```

O renderer ainda está isolado do World/playback e da janela/input; câmera e
fidelidade visual original continuam por validar. Decoder BMP, limites,
texels conferidos e resultados estão em [render-validation.md](render-validation.md).
