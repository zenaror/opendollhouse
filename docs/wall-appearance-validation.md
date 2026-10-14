# Associações arquitetônicas salvas

Checkpoint de 2026-10-14. `read_objd_wall_appearance` e
`derive_wall_appearance` preparam estilos/recortes de objetos arquitetônicos a
partir dos campos reais do World salvo. `odh-wall-appearance` expõe bindings,
aplicações e proprietários/markers de cada face. Ainda não carrega máscaras,
desenha paredes ou constrói rooms. Essa associação é a entrada da próxima
composição de paredes; o snapshot de terreno/pisos/objetos não muda.

## Contrato

Referência FreeSO `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0:
`OBJD.VERSION_142_Fields`/`Read`, `VMGameObject.PostPositionChange` e
`VMEntity.DirectionToWallOff/SetWallStyle`. Implementação própria e perfil
explicitamente limitado, não prova do comportamento original.

Binding OBJD parcial136/138: GUID no byte28, WallStyle no92. Em138, sprite-base
no162; extent164. Em136, o campo não existe nesse perfil: extent94 e sprite-base
absente, mesmo quando o payload possui mais bytes. Reserved/tails do recurso
não são reinterpretados como campos novos. O helper valida prefixo/version e
retém remaining_bytes; não declara o OBJD inteiro validado.

O host parte das entidades do World restaurado e lê os valores runtime word40
FlagField2 e word13 WallPlacementFlags. Arquitetônicos são objetos livres e
posicionados com bits14(janela)/15(porta). Avatares, contidos e fora do mundo são
ignorados para aplicação. Não filtra por Hidden ou por visibilidade do sprite.
Não chama Init/NewBodyInit, nem muda posições, bancos ou definições. Recurso é
resolvido pelo GUID concreto/OBJD exato, com identificação validada após leitura.

Direções em oito índices:0/2/4/6→N/E/S/W→side0/1/2/3; diagonais usam north como
fallback observado. Bits0..3 de placement são front/right/behind/left; outros
bits não fazem escrita de estilo. Somente side0(TR) e side3(TL) recebem override;
sides1/2 são registrados como ignorados. Tile é floor(x/16),floor(y/16), com
limites de grade/story. O segmento pode estar ausente: a referência ainda
escreve os campos nesse tile; isso é marcado no relatório, sem criar uma parede.

WallStyle1..21 referencia estilo global;0 é uma escrita de clear, retida com
proprietário. Valores22..255 usam seis sprites do recurso do objeto: base+i,
up Far/Medium/Near e down Far/Medium/Near. IDs preservam o wrap uint16 da
referência. O modelo usa GUID concreto e IDs de recursos em vez de criar números
dinâmicos de sessão ou alterar OBJD. Preallocated styleIDs>=256 são rejeitados
neste perfil; SpriteID0 é um campo real, não evidência de máscara disponível.
Binding136 dinâmico sem sprite-base também falha explicitamente.

A ordem de entrada é explícita: o adapter de save mantém ordem dos records.
Última escrita de estilo vence. Uma janela não limpa um marcador Door anterior;
uma porta o atualiza conforme style!=0, inclusive clear. O proprietário do estilo
é separado do proprietário/generation do marcador Door para evitar misturar
identidades. Não foi provado que essa ordem reproduza todas as reconstruções do
original; o corpus nativo abaixo não apresentou conflitos. Saída ordenada por
level/y/x/segment, com aplicações em ordem de entrada. Raw grids de paredes
permanecem intactos: sem normalização de estilos base ou reconstrução de rooms.

Limites: até4096 inputs, grades1..64 por eixo/duas stories, grids de paredes
exatos. IDs positivos, GUID/generation não zero e sem duplicatas. A quantidade
de faces é limitada por dois lados por input e pela grade. Adapter antes de
alocar checa World/record count, cache até256 bindings,256KiB de source/asset
provenance e4096 chunks por IFF. Usa leitores FAR existentes; esses limites são
de estrutura/dados e não RSS ou performance de New3DS. Não descarta entidades
fora da tela ou estado que não participe de paredes.

## Validação

85 testes configurados:84 passaram e1 janela X11 pulada em Debug e ASAN/UBSAN.
Depois do ajuste de limites do adapter, o teste afetado e os três probes nativos
foram executados novamente. LSan desativado por ptrace; não houve leak check.
Sem avisos de compilação.551 checks sintéticos: prefixos136/138, truncamento de
cada byte,128 combinações direção/placement, fontes dinâmicas, wrap de IDs,
clear/conflitos, marcadores Door independentes e generations, ausências/limites/
identidades e preservação dos grids.

Python independente leu os OBJD das fontes reais e confirmou todos os campos de
binding. Usou os snapshots World e arrays já validados nos checkpoints anteriores
para reconstruir separadamente aplicações, faces/proprietários, referências de
sprites, marcador Door e ordem. Todos os valores coincidiram; JSON Debug/ASAN é
idêntico. Essa etapa não redecodifica ObjM inteiro como um novo oracle de save e
não carrega/compara os pixels de máscaras; isso pertence ao checkpoint
[SPR#](wall-resource-validation.md) e à integração visual seguinte.

| Casa | Entidades | Bindings | Aplicações | Faces | Door | Máscaras de objetos |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
|01|60|0|0|0|0|0|
|07|182|10|36|18|6|12|
|03|267|12|88|44|8|36|

Não houve sobrescritas nem segmentos ausentes nas três casas. House07 produz
12 referências dinâmicas de janelas e6 estilos globais de portas. Os membros
opostos têm sides ignorados conforme o contrato;36 aplicações não significam
36 faces. Sete inputs comerciais tiveram hashes iguais antes/depois, sem mudanças.

JSON House07 SHA256:
`59a9af82a62b70cbda9ec1d143f9b65650992da7713cb5a87a69f179df4aa674`.
House03:
`34364f1aa9a279b6726f2ccd02cbf568a077c3057ca7d4d9fb145a74776002e2`.
Evidência local ignorada: `reports/verify-wall-appearance-native.py`,
`wall-appearance-native-validation.json`, `house*-wall-appearance.json` e logs
`wall-appearance-*.log`. Nenhum asset comercial versionado.

## Reprodução e próximo gate

```sh
build/odh-wall-appearance '../referencias/The Sims/UserData/Houses/House07.iff' --object-definitions '../referencias/The Sims/GameData/Objects/Objects.far' --object-definition-file '../referencias/The Sims/UserData/Characters/User00000.iff' --object-definition-file '../referencias/The Sims/UserData/Characters/User00001.iff' --neighborhood '../referencias/The Sims/UserData/Neighborhood.iff'
```

`wall_appearance_prepared=true` é separado de `masks_resolved=false`,
`walls_rendered=false` e `rooms_loaded=false`. Próximo gate: carregar SPR# de
padrões/estilos/overrides, aplicar alpha das máscaras e integrar geometria e
profundidade das paredes. Depois rooms/obstáculos/portais e execução/interação
Linux. Meta integral ativa; não há lote jogável ou prova de consoles/original.
