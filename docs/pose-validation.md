# Skeleton e pose de animação — 2026-10-13

Extensão posterior: appearances/BMF e skinning estão em
[mesh-validation.md](mesh-validation.md). A evidência abaixo permanece do sampler.

O núcleo agora lê skeletons binários BCF e calcula poses a partir de um clipe e
seu CFP, com interpolação e composição hierárquica. `odh-pose` expõe as matrizes
e pode exportar um desenho de ossos. Isso prepara o desenho do personagem;
meshes, skinning, roupas, texturas, compositor de lote e integração ao relógio
de playback ainda faltam. O desenho de diagnóstico não substitui o renderer
Linux funcional solicitado.

## Perfil do leitor

`read_bcf_skeletons` é separado de `read_bcf_animations`. Aceita coleção binária
com skeletons e contagens appearance/animação iguais a0; BCF misto e CMX textual
continuam unsupported. Skeleton tem nome Pascal e contagem int16 de ossos.
Cada osso preserva nome, parent, grupos de propriedades ordenados, bits float32
de XYZ/XYZW, flags CanTranslate/CanRotate/CanBlend e bits WiggleValue/Power.
Não converte os eixos nem elimina NaN/infinity nessa leitura estrutural.

O detalhe de registros vazios é relevante no adulto original: name e parent
são lidos, mas name vazio significa um registro descartado sem campos seguintes.
Esse registro não conta no total declarado de ossos. O contador declarado
avança apenas para ossos não vazios; registros descartados têm budget separado.
Essa interpretação foi corrigida durante revisão antes da validação/publicação.

Limites cumulativos da coleção:64 skeletons,4096 ossos decodificados,
4096 registros descartados,65536 unidades de propriedade (grupos+pares) e4MiB
de texto. Contagens negativas e extensões impossíveis são recusadas antes da
alocação. Contagem zero é permitida; bytes após as seções são reportados na API.
A árvore é validada somente ao calcular a pose.

## Cálculo da pose

`sample_pose` não altera os recursos. Converte translation para(-x,y,z) e
rotation para(x,-y,-z,-w), conforme os loaders Skeleton/CFP da referência.
Pose usa float32 e matriz afim3x4 row-major para vetores coluna:
local=translation*rotation e global=parentGlobal*local. É a transposta equivalente
da composição row-vector `(rotation*translation)*parent` de Skeleton.cs.
Posição absoluta ocupa as colunas3 dos três primeiros rows.

Nomes são exatos e únicos; NULL é reservado como sentinela de parent raiz.
Exige uma raiz, parents existentes e nenhuma componente cíclica. Composição é
iterativa: aceita árvore fora de ordem e profundidade4096 sem recursão C++.
NaN/infinity em transformações usadas, overflow aritmético ou hierarquia inválida
causam FormatError antes de retornar a pose. Wiggle e flags são preservados,
mas não há simulação de wiggle ou filtro dos canais por Can flags.

Uma pose pode usar somente os transforms de repouso ou um clipe com peso1.
Valida pareamento animation/CFP, tamanho exato dos sete planos, finite dos valores
e ranges de cada canal. Limite4096 motions e total `3*T+4*R` até1048576 amostras;
texto usado pelo sampler até4MiB. Motions em ordem podem substituir o mesmo
osso; bones ausentes são listados e ignorados como no RenderFrame de referência.
Os canais não mencionados mantêm o repouso fornecido, não a pose anterior.

Frame contínuo não negativo se divide em floor/fraction. Cada motion segura seu
último sample quando termina; frame máximo da coleção é max(FrameCount).
O status completed segue o limite inteiro usado por RenderFrame, sem executar
eventos ou modificar o relógio BHAV. Frame negativo contínuo é clamp0/fraction0/
completedtrue: regra da API ODH, pois RenderFrame recebe int frame e fraction
separados. Valores não finitos ou fora de int32 são recusados.

Translation interpola linearmente; quaternion usa slerp pelo arco curto, com
sinal antipodal e ramo linear quando dot absoluto>0.999999. Não normaliza os
quaternions automaticamente. Frame inteiro preserva o endpoint bruto convertido;
essa preservação e rejeições de overflow são limites explícitos ODH. Não se
afirma igualdade bit a bit com matemática do runtime XNA/.NET nem com TS1.
Blend de peso parcial, gestos, carry e headseek continuam pendentes.

## CLI e imagens de diagnóstico

```sh
./build/odh-pose '../referencias/The Sims/GameData/Animation/Animation.far' adult-skeleton.cmx.bcf adult --animation '../referencias/The Sims/GameData/Animation/Animation.far' adult-chair-sit-new.cmx.bcf a2o-sit-left xskill-A2O-sit-left.cfp --frame 80.5 --svg validation-output/pose-adult-80_5.svg > validation-output/pose-adult-80_5.json
python3 tools/compare_pose.py '../referencias/The Sims/GameData/Animation/Animation.far' adult-skeleton.cmx.bcf validation-output/pose-adult-80_5.json validation-output/pose-animation-cfp.json
```

Sem --animation, amostra repouso. --frame exige animação; --svg é opcional.
Entradas FAR e nomes de skeleton/clipe têm seleção exata, ambiguidades são
recusadas. CFP precisa corresponder ao xskill selecionado mais `.cfp` por ASCII
casefold; o entry FAR permanece exato. Campos brutos e pose aparecem no JSON.
Erro de parsing/seleção/output retorna2, stderr e nenhum JSON de sucesso.

SVG usa uma projeção diagnóstica provisória do planoX-Z comY vertical,
autofit640x640, linhas parent-child e joints; texto XML escapado. Não possui
unidades/câmera isométrica comprovadas do original, meshes, Z ou chair sprite.
Os SVGs adulto0/80.5/160.75 foram rasterizados localmente com ImageMagick e
inspecionados; a progressão mostra mudança de postura até a posição sentada.
Essa observação confirma a utilidade do desenho, não equivalência visual ao jogo.
Arquivos derivados permanecem em reports ignorado, sem assets no Git.

## Evidência verificada

CTest23/23 passou em Debug e ASan/UBSan. Skeleton166 checks sintéticos cobrem
truncamentos, contagens assinadas, registros vazios, propriedades, campos brutos
e limites cumulativos. Pose81 checks cobrem hierarquia/matrizes/sinais,
interpolação/antipodal/endpoints, canais ausentes, limites e overflow.
CLI cobre seleção, erros, SVG escapado e falha de escrita.
LSan permanece desativado por ptrace, sem validação de leaks.

Recursos do Animation.far externo:

| Recurso | Bytes | SHA256 |
|---|---:|---|
| adult-skeleton.cmx.bcf |1937|04bacd45b07c742248ee1df348ab640ada3c153bb4ac3b912d8db07d7c38af75|
| child-skeleton.cmx.bcf |1937|94fbd3b438ad0654b58aca77d830c2be3e201facf10a594bae90f2662cc80759|

Ambos contêm29 ossos e foram consumidos até o fim, sem tail. O clipe adulto
a2o-sit-left continua com27 motions,161frames,322 translations e4347 rotations.
BCF/CFP conservam os hashes anteriores de [animation-validation.md](animation-validation.md).
Nesta unidade o gate compare_cfp conferiu novamente todos os18354 floatbits,
CFP SHA58a5488653645d22ebe858098159c9896a7eba0b745d5483f2098a9c0380074a,
decoded SHAb9c4785b581b67e649626e3f3944996bbab0107dc61e5c052f69ba767f8c091d.

`compare_pose.py` lê o skeleton diretamente do FAR em Python e compara os
campos brutos exatamente. Calcula transforms com doubles independentes a partir
do relatório CFP já verificado; compara translation/rotation/matriz/posição com
tolerância absoluta1e-4 + relativa1e-5. Não é comparação contra uma execução
FreeSO/TS1. Controles negativos com bit bruto e posição adulterados foram recusados.

Repouso adulto/infantil e frames adultos0,80.5,160.75 passaram sem bones ausentes.
Maior erro absoluto observado7.326748701075303e-7. Child repouso e adulto80.5
também passaram no build sanitizado, exit0/stderr vazio, JSON e SVG byte a byte
iguais ao Debug. Relatórios pose-*-comparison.json e pose-cfp-comparison.json
guardam a evidência local. Nenhum original foi executado, nem hardware validado.

## Referências e próximos passos

FreeSO fixo4c6b3e8f5835b228723caea3c9f683c62f244f73, MPL2.0 declarada:
[Skeleton.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.vitaboy.model/Skeleton.cs),
[BCF.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.vitaboy.model/BCF.cs),
[CFP.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.vitaboy.model/CFP.cs),
[Animator.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.vitaboy.engine/Animator.cs).
Ramo matemático consultado em
[MonoGame Quaternion.cs v3.8.1](https://github.com/MonoGame/MonoGame/blob/v3.8.1/MonoGame.Framework/Quaternion.cs),
[Ms-PL declarada pelo projeto](https://github.com/MonoGame/MonoGame/blob/v3.8.1/LICENSE.txt); isso não identifica a versão exata ligada ao FreeSO
fixado. Implementação ODH própria, sem importar runtime C#.

Próxima unidade: appearances/meshes BMF/SKN e skinning/texturas, ligar poses ao
playback/World e compositor Linux com cena reproduzível. Sprite da cadeira,
profundidade/oclusão/câmera e janela/input também faltam. Scheduler geral,
queue/RNG/decay/áudio/lote/persistência seguem pendentes. A meta inteira está ativa.
