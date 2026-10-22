# Snap, binding e ocupação — 2026-10-13

### Bloqueio de móvel no ponto de chegada da escada — 2026-10-22

`tests/snap_arrival_blockers.cpp` compõe as três etapas usadas pelo teste de
chegada: um retângulo de colisão de móvel entra no retrato de obstáculos, o
mapa marca a coordenada do patamar como ocupada e o validador do Snap devolve
“bloqueado”. A VM retorna falso e mantém o avatar no ponto de partida. O caso
de controle sem móvel mantém a chegada livre e troca o avatar para o outro
piso. CTest: `snap_arrival_blockers` passou.

É uma fixture sintética que testa a ligação entre os componentes locais; ainda
não injeta um móvel real salvo na House03. Não afirma como o executável original
trata esse caso. O teste de integração da House03 segue validando os dois
sentidos sem bloqueadores. Para outro avatar no cômodo, o comportamento host
atual é `unsupported`: “Shoo” ainda não está implementado.

`odh_snap` implementa um perfil inicial do snap46 Literal para ocupar um
recipiente. Ele permite avançar o BHAV real da cadeira até a animação44,
ainda ausente. Não comprova sentar visualmente ou compatibilidade completa.

O objeto é ligado por OBJD selecionado explicitamente: prefixo de 42bytes
retorna versão/GUID/AnimationTableID/SlotID, nas versões136/138/139/140/141/142.
O corpo restante é opaco, sem validação completa. O loader confirma um OBJD
e um SLOT de ID correspondente; duplicatas ou ausência falham antes de executar.
Binding inclui geração da entidade e identidade da VM, evitando reutilizar
outro objeto com o mesmo ID.

Perfil: Literal Mode3, flags4 e tailzero, SLOTv10 tipo3 com offsetzero,
flags/scoreszero e SnapTargetSlot>=0. O slot de armazenamento tipo0 precisa
de offsetzero e height7. Outros modos/geometrias/alturas dão unsupported.
O flag FootprintExtension é reconhecido no operando, mas não cria footprints
ou a política espacial do original. Falhas de ocupação retornam false.

`World::place_in_slot` mantém posição física e vínculo juntos. Destino ocupado,
índice inválido, ciclo e vínculo a outro recipiente falham sem alterar posição
ou relações. Repetir no mesmo slot é idempotente. Direção do ocupante é
preservada. Contenção posicionada propaga x/y/nível quando o recipiente muda,
inclusive aninhada, por lista iterativa; alocação ocorre antes da mudança.
Detach deixa a última posição física. Transferência entre recipientes e
geometrias não zero ainda não estão implementadas neste perfil.

Height7 é classificação de altura do SLOT, disponível nos dados. Não somar
arbitrariamente sete unidades à posição física. Offset/altura visual,
skeleton/personagem, multi-tile, colisões e política completa de snap ainda
faltam. A referência FreeSO foi consultada; original executável não foi testado.

## Validação

CTest13/13 passou em Debug e ASan/UBSan. A suíte snap/prefix tem59checks
sintéticos: offsets do prefixo, truncamentos, ocupação/idempotência, recusa
atômica, geometria/modeunsupported, IDreutilizado, binding e propagação aninhada.
A CLI integra OBJD/SLOT/BHAV sintéticos. LSan continua não validado por ptrace.

Dados fornecidos: DiningChairs.iff, OBJD16808, versão138, GUID0x26BFBB29,
SlotID128 e AnimationTableID129. SLOT128/type3/index0 (registro absoluto1)
aponta SnapTargetSlot0; armazenamento type0/index0 tem offsetzero e height7.
Identidades dos recursos estão nos checkpoints anteriores.

Probe construído com BHAV1001 real: roteamento33ticks já validado; snap muda
a posição física de(48,64) para(64,64), liga caller1 ao recipiente2/slot0 e
preserva direção2. O BHAV escreve postura1 e avança até opcode44 em1003PC8.
São18instruções totais, três frames, saída3 e diagnóstico esperado
`unsupported primitive 44`. O teste permanece em cena ODH construída, sem
animação/janela, save original, execução do Sims.exe.bak ou hardware.

JSON Debug e sanitizadores idênticos, stderr vazio. Reports ignorados:
`reports/chair-snap-probe.json` e `reports/chair-snap-probe-sanitize.json`.

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run --world-probe --route-probe --snap-probe --object-def 16808 --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff
```

Próximo: resolver tabela de animações129 e os recursos do personagem usados
pela primitiva44; executar animação/retomada e depois composição Linux.
Fidelidade de tempos/rotas, sistemas e persistência continuam pendentes.

## Escada House03 com animação — 2026-10-22

O probe `tests/house03_stair_approach.py` agora fornece as coleções comerciais
`GameData/Animation/Animation.far` e `ExpansionShared/ExpansionShared.far` ao
Playback existente. Para o escopo Object da primitiva44, o host primeiro usa o
CodeOwner da VM; em chamadas externas de objeto, quando esse identificador é
apenas a identidade do banco de código, usa o objeto que está executando a
rotina. O perfil multi-tile de animação só aceita as duas definições conhecidas
de `stair.iff` dentro de um grupo com patamar nos níveis1 e2.

Quatro execuções com save real House03 e avatar/Main sintéticos terminaram a
ação do portal (`returned_true`): subida e descida, cada uma com idade fixture
0 e21. Em todos os casos a caminhada fez oito callbacks, o Snap trocou para o
patamar oposto, a animação foi resolvida e registrou14 eventos de passos. Isso
confirma a cadeia de simulação até o retorno da ação nos dois sentidos para
essas idades; não confirma pixels animados, som, colisão geral ou equivalência
com o executável original. Os dados comerciais continuam somente leitura e
fora do Git.

### Pose animada em imagem — 2026-10-22

O mesmo probe agora pode exportar quadros do Playback em `--capture-tick`.
Para o diagnóstico da animação `a2o-stair-climb`, a câmera da imagem de teste
é deslocada até a posição em que a pose aparece; isso não altera a posição do
Sim no World. O teste gera apenas a imagem do Sim, sobre fundo claro, sem
compor escada, móveis ou o restante do lote.

Foram capturados os ticks14,16 e20. Os três passaram pelas verificações de
ator, clipe e desenho, e resultaram em imagens diferentes. A inspeção lado a
lado mostra a pose mudando durante a subida. Os arquivos PPM de diagnóstico
foram mantidos em `/tmp/house03-stair-sequence-tick{14,16,20}.ppm`; o mosaico
para inspeção é `/tmp/house03-stair-sequence.png`. SHA-256, na mesma ordem:
`92406d40ff9724b40d36a97a82302fa149c1c045a649b21fc97f8cbd9e2c253f`,
`0bc23a3299d72b259027338946c165a3274c814bba44053f199ad78f5d5f6a4c` e
`db419312e6e75895e1f521a2040ffcfdc594070c07e02f00191006dca440b6ad`.

Isso demonstra que o Sim da simulação chega ao desenho com poses diferentes.
As imagens acima são do Sim sobre fundo claro; a próxima captura registra a
composição com o lote.

### Sim animado sobre paredes, piso e objetos — 2026-10-22

O compositor agora ignora uma camada de sprite somente quando ela fica
inteiramente fora da tela. Isso reduz o trabalho visual da captura sem remover
objetos do lote ou mudar o estado do World. Os testes sintéticos verificam que
80 camadas fora da tela não gastam a cota de pixels, enquanto as camadas
visíveis continuam sujeitas ao limite original.

O teste de House03 centraliza a câmera no ponto gráfico do patamar usado pela
travessia e agora lê `GameData/walls.iff`. A composição desenha paredes, piso,
objetos e a pose do Sim. A vista da descida seleciona o piso1 para mostrar a
escada; a entidade continua no andar2 no World. Em ambos os sentidos, o teste
exige pixels do Sim no mapa de propriedade que sobrevive à composição de
paredes e objetos. Na subida do tick14, 4103 pixels foram desenhados para o Sim
e 4044 continuaram identificáveis depois da composição.

As seis capturas foram inspecionadas em
`/tmp/house03-stairs-walls-overview.png`. As imagens PPM são
`/tmp/house03-{up,down}-walls-sequence-tick{14,16,20}.ppm`; cada direção passou
na checagem de hashes diferentes entre ticks. SHA-256:

- Subida 14: `c869e77bcb81ddbd75421f8eb40e3b7fd5120261ffac7b14cfa6107843de106e`
- Subida 16: `334b0c6d4eca0a81d0f9411f220830087ba10aadfc1cdf672f3081e8528be6f3`
- Subida 20: `b86d68b731fa6e6637b3133086cbdf86d8039c6db0ac0e295ed547bd48a023bd`
- Descida 14: `9c7230922799d83b67c292f4fe86de034ac3a1df4bd030abbc6db2dd350c5de1`
- Descida 16: `3767c0e33494aaafc951778a9ddbd6834e1f50958db3dce7294998bd9c49779f`
- Descida 20: `48b37e9f6cd702f99707d29100f6ccd81b1429dcae78551634f7d643c7e9fb92`

O teste funcional também percorre os dois sentidos para idades fixture 0 e21,
com oito passos de caminhada, troca de piso e retorno da ação. As novas imagens
mostram poses da subida e da descida num save real House03, com avatar e Main
sintéticos. Isso não demonstra fidelidade visual ao original nem generalidade
para todas as escadas, paredes ou estados de lote.

Repetição das sequências com os dados instalados:

```sh
python3 tests/house03_stair_approach.py --binary /tmp/odh-stair-build/odh-person \
  --game-root '../referencias/The Sims' --capture-tick 14 --capture-tick 16 \
  --capture-tick 20 --capture-portal 34 --capture-output /tmp/house03-up-repeat.ppm
python3 tests/house03_stair_approach.py --binary /tmp/odh-stair-build/odh-person \
  --game-root '../referencias/The Sims' --capture-tick 14 --capture-tick 16 \
  --capture-tick 20 --capture-portal 38 --capture-level 1 \
  --capture-output /tmp/house03-down-repeat.ppm
```

Build completa e CTest: 160/160 passaram; `linux_window_backend` foi pulado por
falta de display. A suíte completa não prova a execução de janela nesta sessão.
