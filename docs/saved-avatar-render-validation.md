# Renderização dos avatares carregados do save

Checkpoint de 2026-10-14. `odh-saved-scene` carrega o World validado de House07 e
renderiza os avatares reais 105 e 137 usando aparência e animação do snapshot.
Preserva as 182 entidades no World. Esta saída é uma imagem CPU estática dos
avatares; a arquitetura e os objetos ainda não são desenhados, e as threads
salvas continuam dormentes. Não é uma comparação visual com o jogo original.

## Recursos e adaptação

`SavedAppearanceResources` recebe FAR de animação e texturas e uma seleção
explícita de skeleton. No modo `odh-saved-scene`, sem uma pasta extra de skins,
indexa nomes internos dos BCF de aparência/animação;
não deduz o nome do arquivo a partir do nome do recurso. Lookup usa ASCII
casefold, conserva a grafia real e rejeita ambiguidades. BCF com skeleton é
ignorado pelo índice de nomes; o renderer valida o rig selecionado. BCF misto
fora dos perfis já implementados é recusado. BMF e BMP também exigem seleção
única nesse modo; ele não faz busca em outros arquivos, precedência de expansões
ou substituição anônima de recursos ausentes.

O host interativo `odh-person` também indexa CMX, SKN e BMP soltos em
`GameData/Skins`. Uma appearance CMX escolhe o SKN pelo nome do binding; o
renderer confere o osso de ligação no rig e lê a textura BMP indicada pelo SKN.
Arquivos são abertos somente para leitura, com limites de contagem e tamanho.
Se o mesmo nome existir nos BCF já indexados, o host os escolhe para preservar
o caminho anterior; CMX solto é usado quando o BCF não contém a appearance.
Essa prioridade é uma escolha atual do ODH, não uma regra do jogo original já
confirmada. Pastas de expansão e prioridade entre cópias soltas ainda não estão
ligadas nesse caminho.

Cada appearance expande todos os bindings. O valor após `=` da textura salva
é o override; os rótulos BODY/HEAD-HEAD/HAND não escolhem o binding. Seguindo
`Outfit.ToTex` e `Avatar.AddAppearance` da referência, o último binding com
textura `x` ou nome compatível após remover lgt/med/drk recebe o override.
Se não houver correspondência, recebe-o o primeiro binding. Os demais usam
a textura declarada no BMF. O perfil atual exige um único `label=value`, sem
múltiplas atribuições ou delimitadores extras; isso é um limite do ODH.

House07 resolve quatro componentes por avatar:

| Avatar | Appearance de corpo | Textura de corpo | Appearance de cabeça | Textura de cabeça |
|---|---|---|---|---|
| 105, Bob | b002mafat_01 | B002MAFatlgt_slob.bmp | c010ma_baldbeard | C010MAlgt_baldbeard01.bmp |
| 137 | b006fafat_01 | B006FAFatlgt_TT1.bmp | c001fa_mercedes | C001FAlgt_Shannon01.bmp |

As mãos salvas são hmlo/hmro e hflo/hfro, todas com HUAOlgt.bmp. A roupa de Bob
substitui o default BMF b002mafatlgt_gpa2. Não se usa a seleção de roupa de um
personagem novo. O ODH lê as mãos explicitamente do save; o novo activator
FreeSO restaura corpo/cabeça, mas não esses campos de mãos diretamente. Esse
comportamento do ODH não deve ser apresentado como reprodução completa do
activator FreeSO.

A string de animação tem oito campos exatos:
`name;priority;speed_milli;normalized_frame;weight_milli;loop;unknown1;unknown2`.
O parser conserva os campos, valida inteiros signed32, frame/peso 0..1000 e
loop 0/1. A posição é `max(Motion.frames) * normalized_frame / 1000`.
Velocidade negativa conserva a direção reversa; não avança tempo neste host.

Nos dois avatares, `a2o-aware1;6;1000;25;1000;0;0;0` resolve
adult-aware1.cmx.bcf e xskill-A2O-aware1.cfp. Com 121 frames, a posição é
**3,025**, não frame25. O peso1000 corresponde a1. A base
`a2o-standing-loop;-10;1000;75;1000;1;1;1` resolve ross-standing-loop.cmx.bcf,
60 frames e posição4,5; fica preservada, com `base_executed=false`. O novo
activator FreeSO não restaura BaseAnimation nesse caminho. O ODH ainda precisa
investigar sua composição no original. Animação carry e peso atual diferente de1
são recusados explicitamente, sem descartar campos silenciosamente.

## Acessórios salvos

O renderer agora aceita os acessórios OBJM cujo vínculo está vazio. O leitor
OBJM do Simitone comenta que esse vínculo costuma estar vazio, e o activator
TS1 transfere o nome do acessório para o avatar. O nome precisa terminar em
`.apr`; o ODH encontra esse nome em BCF/BMF ou, no host interativo, em CMX/SKN,
expande cada vínculo, confere a textura BMP e acrescenta a malha ligada ao osso
indicado. Vínculos preenchidos, recursos
ambíguos ou ausentes e excesso do limite de 16 peças continuam sendo recusados.

No checkpoint de 2026-10-24, o modo manual opcional
`odh-saved-appearance-tests --commercial-skin Animation.far Textures.far
GameData/Skins` abriu recursos originais e rasterizou uma cena CPU. O SKN do
corpo tinha 19 bones, 572 faces, 406 vértices reais e 106 de mistura; a cena de
validação somou 3.248 vértices, 4.576 triângulos e 4.975 amostras sombreadas.
Para exercitar o renderer isolado, os oito componentes BodySTR foram apontados
ao mesmo corpo. Isso confirma leitura, skinning e rasterização de um mesh
comercial; não representa um avatar montado corretamente nem valida a entrada
em lote, a aparência completa ou fidelidade visual.

`odh-saved-appearance-tests` passou 174 verificações. Fixtures sintéticas
agora passam CMX→SKN→BMP pelo renderer, conferem o osso no rig e produzem pixels.
Outra fixture de acessório cobre uma escova sintética.
Separadamente, os arquivos comerciais de `Animation.far` foram lidos pelo
`odh-mesh`: `suit-toothbrush.apr` resolve para uma malha `R_HAND` e o esqueleto
adulto. Uma captura headless real de House07, no armário154, durante a ação4102
Brush Teeth, executou até o clip `a2o-medcab-nocounter-brush-loop2`, frame140.4.
Com `--render-hide-lot-objects --render-size 640 480`, o Sim aparece sem os
móveis cobrindo sua mão; a captura ainda não permite identificar com segurança
a escova como objeto separado.

O replay até o tick1600 terminou `returned_true`, sem diagnóstico, e esvaziou a
fila de interação. Em duas execuções idênticas do mesmo lote, a necessidade de
higiene terminou em96 com Brush Teeth e95 sem a ação; as outras seis necessidades
coincidiram. Isso confirma um efeito observado neste host, sem afirmar paridade
completa do sistema de necessidades. O retorno de processo3 é o limite explícito
`--ticks`, não uma falha do VM. A opção `--render-hide-lot-objects` existe só
para captura diagnóstica; ela não muda o World ou os objetos salvos.

## World, renderer e composição

O host usa `graphics_entity(World,id)` para coordenadas/direção. House07:
Bob105 `(392,440,1,3)` vira tile `(24,5;27,5;0)`, avatar137
`(424,440,1,3)` vira `(26,5;27,5;0)`. A câmera centra na média dos avatares
posicionados: `(25,5;27,5;0)`. Avatares fora do mundo permanecem no World e são
listados, sem imagem. Contenção segue as restrições do adaptador gráfico
existente; contenção sem um SLOT gráfico resolvido não é inventada pelo host.

`SceneRenderer` aceita agora seleção sem objeto (object_far/object_entry vazios,
object_id0); seleções parciais são inválidas. Essa opção permite renderizar um
avatar sem carregar uma cadeira de fixture. O caminho anterior de avatar+objeto
permanece coberto pelo controle de pixels/profundidade do CLI existente.

`merge_raster` combina imagens opacas no mesmo espaço de profundidade: menor
profundidade vence, empate conserva a base, camada com +infinity não cobre.
Valida dimensões iguais, arrays exatos, alpha255, profundidade finita ou +inf e
contadores/orçamentos antes de copiar. Testes/shadings são somados como trabalho
de rasterização, não contagem final de pixels visíveis. Não implementa alpha de
geometria, ordem de materiais transparentes, seleção por pixel ou paredes.

O host limita a16 avatares posicionados. Recursos acumulados:65.536 vértices
renderizáveis,131.072 triângulos,1.048.576 texels RGBA e1.048.576 amostras CFP;
samples de rasterização acumulados limitados a16.777.216. Índices de FAR:8.192
entradas/256KiB de nomes por arquivo; índice de nomes internos:8.192 recursos e
256KiB de nomes+proveniência. Máximo16 bindings finais por avatar. Os decoders e
o renderer mantêm também seus limites por recurso. Viewport até2048 por eixo e
1.048.576 pixels. As texturas/CFP entre os avatares ainda não compartilham cache.
Esses são orçamentos lógicos; RAM máxima e desempenho no New3DS não foram medidos.

As entradas são abertas para leitura. Antes de escrever PPM/depth, o host rejeita
outputs iguais ou aliases por symlink/hardlink de inputs ou do outro output.
Toda a resolução/renderização termina antes da escrita. Uma falha de I/O pode
deixar PPM/depth parcial, mas não JSON de sucesso. Uso/falhas retornam2;
sucesso retorna0 e JSON com IDs/recursos/frames/contagens e limitações.

## Evidência

33 checks sintéticos de resolução/parser, incluindo nomes internos em arquivos
com nome inesperado, vários bindings, último match/primeiro fallback, defaults,
ambiguidades, carry/acessórios, números inválidos e uso após move. Compositor:
873 checks sintéticos totais, incluindo a nova composição por profundidade,
empates, overflow, opacidade e arrays/limites. O teste de SceneRenderer verifica
a saída avatar-only contra o CLI independente com DGRP sintético sem sprites.

Suíte completa Debug e ASAN/UBSAN:74 passaram, zero falharam, um teste X11 sem
execução (75 configurados). Após a proteção de uso do resolver movido, os33
checks de aparência e o host reutilizável passaram novamente nos dois builds.
LSan desativado pela limitação ptrace; sem conclusão de detecção de leaks.

Validação nativa: imagens e depth Debug/ASAN iguais byte a byte em três vistas.
Cada avatar também foi renderizado pelo `odh-render` com seleção explícita dos
quatro componentes/clip/CFP/frame e mesmas coordenadas/câmera. A composição dos
dois controles foi calculada em Python, escolhendo o pixel de menor depth.
Todas as imagens RGB e todos os float32 de depth coincidem exatamente com o
novo host. Esse controle confere integração do loader/resolver/World/renderer;
usa os samplers/rasterizadores ODH existentes, não uma execução original.

| Vista | Pixels cobertos | Samples testados | Shadings |
|---|---:|---:|---:|
| rotation0/zoom3 |7.998|43.192|12.715|
| rotation2/zoom3 |7.749|41.297|12.237|
| rotation0/zoom2 |1.992|10.788|3.157|

Todas:1.071 vértices,1.424 triângulos,720.896 bytes RGBA,29.524 amostras CFP.
PNG das vistas0/2 foi inspecionado: aparecem os dois avatares com corpo/cabeça/
mãos e direção correspondente. O fundo vazio é deliberado neste estágio sem
arquitetura integrada; não prova encaixe visual em piso ou paredes originais.

SHA256 PPM/depth:

- r0-z3: `af84bf3710c772325105ce10ff4b320e0bc8785a64bdb9547deb551c4d056e8f` /
  `87634c27e618057da694b8af9022a706e420439f4c81786bd58a7eeb453f549a`.
- r2-z3: `acb1bb6510a165a903a2bce7424c7279337443237ad29038a2cce7372584948b` /
  `cb49b6e6d1b2a847ff8d71ffc509776d03b5fe272d9120c3659048587a23c620`.
- r0-z2: `214e66164f1cff49efd3b311cd09fc34b84bc764645a8ebe4155b498759194cf` /
  `5bea55da99af4246b713568b2e6c0c8e0e51562f37eecf4702b1953a924889a3`.

Os sete inputs foram conferidos por SHA antes/depois sem mudança. Animation.far:
`acd29d2124d9ede479a586d40122a5896e264447cedd60c08ba54078c48eb35f`;
Textures.far:`d4848bdf01c8e4e01067204d4a5c89011948c7ce7c1bf97b39202eafd002993e`.
Demais hashes estão em [saved-world-validation.md](saved-world-validation.md)
e no relatório local. Argumentos ausentes, opção desconhecida, rotation4,
opção repetida, definições dos personagens ausentes e output alias de House07
foram rejeitados com exit2, sem JSON nem novo PPM; hash de House07 conservado.

Evidência local ignorada: `reports/house07-avatars-r{0,2}-z{2,3}.{json,ppm,depth,png}`
(somente as três combinações acima), `reports/verify-saved-scene.py`,
`reports/saved-scene-native-validation.json`, controles por avatar e logs de build/
CTest. Nenhum asset comercial ou imagem derivada foi adicionado ao Git.

## Reproduzir

```sh
./build/odh-saved-scene \
  '../referencias/The Sims/UserData/Houses/House07.iff' \
  '../referencias/The Sims/GameData/Animation/Animation.far' \
  '../referencias/The Sims/GameData/Textures/Textures.far' \
  reports/house07-avatars.ppm \
  --skeleton adult-skeleton.cmx.bcf adult \
  --object-definitions '../referencias/The Sims/GameData/Objects/Objects.far' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00000.iff' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00001.iff' \
  --neighborhood '../referencias/The Sims/UserData/Neighborhood.iff' \
  --depth-output reports/house07-avatars.depth
```

FreeSO fixo4c6b3e8f5835b228723caea3c9f683c62f244f73, consulta local:
`Outfit.cs:152–163`, `Avatar.cs:108–148,189`, `TS1BCFProvider.cs:44–60,114–129`,
`VMTS1ActivatorNew.cs:328–357,370–375,394–395`, `Animation.cs:98`.
Créditos/revisões em [references.md](references.md). Próximo: arquitetura e
objetos do lote, continuação de VM/ticks, câmera calibrada e interação Linux.
