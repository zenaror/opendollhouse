# Appearances, BMF e skinning CPU — 2026-10-13

Extensão posterior: texturas BMP e rasterizador CPU em
[render-validation.md](render-validation.md). A evidência abaixo é do skinning.

O núcleo lê appearances binárias e meshes BMF e transforma seus vértices pela
pose já validada. Corpo e cabeça reais passam pelo pipeline, incluindo pesos
de blend. É geometria CPU exportável, ainda sem texturas, compositor/janela de
jogo ou integração com World/playback. A prévia em PNG é uma ferramenta de
diagnóstico do host, não a versão Linux funcional inteira.

## Leitores e campos brutos

`read_bcf_appearances` aceita BCF binário com skeleton0 e animation0. Cada
appearance preserva nome Pascal, Type/Zero int32 e bindings com bone, mesh_name,
CensorFlagBits/Zero. Seleção de roupa, tom de pele e censura não é executada.
Bindings estão embutidas; não se confunde esse layout com binding TSO por IDs.
Limites cumulativos:4096 appearances/bindings e4MiB de texto; negativos e
extensões impossíveis são recusados antes da alocação. BCF misto não é inferido.

`read_bmf` preserva skin/texture name, nomes de ossos, faces, bone bindings,
UVs floatbits, blend_data(weight int32 seguido por OtherVertex int32),
vertex_count2 e posição/normal floatbits de vértices reais e de blend.
Blend vertices não têm UV armazenado; a estrutura expõe bits zero nesse campo.
NaN/infinity, índices/ranges/pesos assinados e vertex_count2 permanecem brutos
nesta etapa. Cada contagem primária é validada contra payload, limite e
max_size antes de alocar; regiões UV/transform e blend_data/transform são
consideradas juntas. Extensão restante é reportada.

`read_skn` lê o perfil textual TS1 com os mesmos campos de mesh. Os registros
de mistura vêm como OtherVertex seguido por weight e são convertidos para a
estrutura comum; coordenadas e pesos continuam brutos para validação posterior
no skinning. Contagens, nomes, truncamento e dados inesperados no fim são
limitados ou recusados. O renderer aceita CMX/SKN/BMP soltos sob
`GameData/Skins`, validando cada malha contra o osso CMX e o rig escolhido.
Busca em pastas de expansão e regras de prioridade ainda não estão validadas.

Limites por leitura:4096 nomes/bindings,131072 faces,65536 real vertices e
65536 blend vertices,4MiB texto. Não são orçamento global de outfit, RAM medida
ou limites atribuídos ao TS1/3DS. CMX é aceito apenas no perfil de appearances
já descrito; outros segmentos CMX e meshes TSO não são lidos.

No corpo original, vertex_count2=687, real=545, blend=142. Na cabeça,56/56/0.
FreeSO.Read ignora vertex_count2, enquanto Write escreve somente real count.
ODH preserva esse campo e não impõe igualdade com real ou real+blend; não se
estabeleceu uma regra geral para esse metadata.

## Skinning e perfil explícito

`skin_mesh` é puro: recebe Mesh e SkeletonPose sem alterar inputs. Usa matrizes
afins3x4 coluna do sampler. Positions e normals BMF convertem(-x,y,z).
Os vértices são bone-local; não aplica inverse bind extra. Cada bone binding
atribui a matriz do osso a seus ranges real/blend. Nomes usam ASCII casefold,
com nomes de pose ambíguos ou bones ausentes recusados. BoneIndex negativo é
inválido; índice além do último nome usa o último nome, como Math.Min da
referência, e fica registrado em clamped_binding_indices.

Ranges com count0 ignoram first, inclusive sentinelas arbitrárias; ranges
ativos exigem início não negativo e extensão válida. Exige cobertura completa
e disjunta; overlaps ou vértices sem dono são unsupported. Essa é uma restrição
ODH explícita, não uma regra demonstrada do original. Cada nome de bone do mesh
precisa resolver, mesmo que não seja usado por um binding ativo.

BlendData[i] usa BlendVertex[i], o dono secundário e OtherVertex. Registros
repetidos para o mesmo OtherVertex são substituídos em ordem, último vence,
como Prepare. Peso w=weight/32768, perfil aceito0..32768. Não faz clamp de
pesos fora dessa faixa nem assume extrapolação válida.

```text
P = (1-w) * primaryMatrix * realPosition
  + w * secondaryMatrix * blendPosition
```

Sem blend, usa somente a posição primária. A normal segue vsVitaboy:
normal primária zero vira(0,1,0); transforma pela parte3x3 primária sem
normalizar, inverse transpose ou blend. Blend normals ficam preservadas, mas
não são usadas por esse shader de referência. Todos os componentes de input e
transforms, inclusive campos de blend não usados, exigem finite no perfil ODH.
Overflow aritmético, face/range/OtherVertex inválido ou budget excedido causa
FormatError antes de retornar resultado.

## CLI, OBJ e prévia

`odh-mesh` seleciona explicitamente um binding de uma appearance, um BMF e um
skeleton no mesmo FAR. Entradas e nomes de appearance/skeleton/clipe são exatos;
ambiguidades são recusadas. Binding mesh_name+'.bmf' deve corresponder ao entry
por ASCII casefold, skin_name precisa corresponder ao binding e bone resolver
na pose. Não monta outfit completo ou faz fallback de recursos automaticamente.
CFP deve corresponder ao xskill+'.cfp'; --frame exige animação. Parsing, seleção
ou falha de escrita retorna2/stderr sem JSON de sucesso.

```sh
./build/odh-mesh '../referencias/The Sims/GameData/Animation/Animation.far' b001mafit_01.cmx.bcf b001mafit_01 xskin-b001mafit_01-PELVIS-BODY.bmf adult-skeleton.cmx.bcf adult --animation adult-chair-sit-new.cmx.bcf a2o-sit-left xskill-A2O-sit-left.cfp --frame 80.5 --obj reports/mesh-body-80_5.obj > reports/mesh-body-80_5.json
python3 tools/compare_mesh.py '../referencias/The Sims/GameData/Animation/Animation.far' b001mafit_01.cmx.bcf xskin-b001mafit_01-PELVIS-BODY.bmf reports/mesh-body-80_5.json
```

JSON inclui appearance/bindings, pose, todos os campos brutos BMF e vertices/
faces transformados. Declara renderer=none/textures_decoded=false. OBJ opcional
exporta positions/UVs/normals e faces1-based preservando winding, sem MTL ou
extração de texturas. O export só acontece após cálculo e validação.

`preview_mesh.py OUTPUT.png REPORT...` combina até16 relatórios, desenhando
triângulos sem texturas com Pillow do host. Projeção X-Z/Y vertical e painter
sorting são diagnósticos provisórios, sem Z-buffer/câmera/composição comprovados
do original. Não são o renderer portátil. PNGs rest/80.5/160.75 com corpo+cabeça
foram inspecionados e mostram a deformação da superfície até sentar.

## Inputs e gates

Recursos comerciais externos em Animation.far:

| Recurso | Bytes | SHA256 |
|---|---:|---|
| b001mafit_01.cmx.bcf |83|c8ee99707020197d1004cce7755a0ad3afdc61dcc45cc286ad19efc1509828bb|
| c001ma_ross.cmx.bcf |78|c037aaa11c0f8b0814b8c394f7efdab03ab9b168e74e314f6677f274e5484f06|
| xskin-b001mafit_01-PELVIS-BODY.bmf |29832|1f77d331d9833efc6196fd176c001a7236df9b52731849179d2a75f1bb602635|
| xskin-c001ma_ross-HEAD-HEADB.bmf |3048|811d4741c92da182c0c9bea95f2330f9726cdc5a247bf145c7c414ead3c5c0e4|

Corpo19bones/19bindings,545real/142blend/606triângulos; pesos3771..28086.
Cabeça1bone/1binding,56real/0blend/98triângulos. Ambas meshes sem tail e sem
binding clamps. Combinadas601vertices/704triângulos. Skeleton/BCF/CFP continuam
os hashes e gates de [pose-validation.md](pose-validation.md).

CTest26/26 passou em Debug e ASan/UBSan. Mesh376 checks sintéticos cobrem
truncamento completo, contagens maliciosas/assinadas, bits/metadata e limites.
Skinning106 checks cobrem geometria analítica, pesos0/0.5/1, primary normals,
zero normals, casefold/clamp, último blend, ranges/overlap/coverage, finite,
overflow e budgets. CLI cobre seleção, recurso errado, opções e OBJ/falhas.
LSan permanece desativado por ptrace, sem validação de leaks.

`compare_mesh.py` lê BCF/BMF diretamente em Python, confere campos brutos
exatamente e calcula skinning em double usando as matrizes da pose. Os bones e
matrizes exportados foram comparados com os relatórios do sampler já cobertos
pelo oracle independente compare_pose. Tolerância1e-4 absoluta+1e-5 relativa;
não é uma execução FreeSO/TS1 ou comparação de GPU.

Corpo/cabeça em repouso e frames80.5/160.75 passaram. Maior erro observado
4.7459586394182907e-7. Controles negativos com bit bruto e posição adulterados
foram recusados. Corpo/cabeça80.5 no build sanitizado retornaram0/stderr vazio;
JSON e OBJ byte a byte iguais ao Debug. Relatórios mesh-*-comparison.json,
JSON/OBJ/PNGs ficam somente em reports ignorado; nenhum asset é versionado.

## Referências e continuidade

FreeSO fixo4c6b3e8f5835b228723caea3c9f683c62f244f73, MPL2.0 declarada:
[Mesh.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.vitaboy.model/Mesh.cs),
[Appearance.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.vitaboy.model/Appearance.cs),
[Avatar.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.vitaboy.engine/Avatar.cs),
[Vitaboy.fx](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.content/ContentSrc/Effects/Vitaboy.fx).
Implementação ODH própria; não importou engine/shader C# como backend.
Sem original executado, fidelidade visual comprovada ou hardware validado.

O próximo teste é repetir a entrada de lote com um Sim criado no CAS, usando os
recursos originais completos (cabeça, corpo e mãos), e conferir a imagem. O
teste comercial executado neste checkpoint montou o mesmo corpo 8 vezes para
exercitar bindings diferentes; rasterizou 4.975 amostras, mas não é uma roupa
completa nem valida gameplay ou semelhança visual. Inventário de expansões e
seleção de roupas continuam pendentes.
Scheduler geral/queue/RNG/decay/áudio/lote/persistência continuam pendentes.
A meta funcional inteira segue ativa, com ports posteriores ao Linux.
