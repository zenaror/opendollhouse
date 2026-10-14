# Núcleo de junções de paredes erguidas

Checkpoint 2026-10-14. `odh_wall_junction` calcula os encontros/terminais das
paredes erguidas e seus três mapas depth. Não altera o snapshot publicado em
6125552: a aplicação dos sprites de junção ao renderer é a próxima integração.
Não cria tampas lisas, rooms ou comportamento original presumido.

## Contrato de referência

Implementação própria baseada no FreeSO
`4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0:
`WallComponent.cs:54–101,123–125,535–591,619–627,727–785,868–873`,
`WallTile.cs:33–46`, `VMTS1Activator.DecodeWalls`,
`TextureGenerator.cs:154,184–187,229–239`.

`standing_wall_junctions` valida duas grades quadradas exatas de até64x64,
rotação0..3 e segmentos conhecidos/exclusivos antes de alocar. Cardinal working
normaliza lados existentes fora dos fences2/12/13/14 para1; lados ausentes0.
TL/TR thick contribuem mesmo quando o campo bruto era3/22. Diagonais só contribuem
quando TRstyle bruto é1/255. Aparência, pintura, doors/windows e altitude não
entram nessa topologia. Dados brutos permanecem intactos.

Contribuições OR por índice linear `off`, largura W:

| Segmento thick | Primeiro endpoint | Segundo endpoint |
| --- | --- | --- |
|TL|off flag8|off+W flag2|
|TR|off flag4|off+1 flag1|
|Vertical diagonal|off flag128|off+W+1 flag32|
|Horizontal diagonal|off+1 flag16|off+W flag64|

Guards preservam explicitamente a particularidade da referência: endpoint+1
na última coluna pode chegar à próxima linha, desde que seu índice ainda caiba
na grade. Isso também vale para diagonais. Não é uma regra comprovada do TS1;
não se corrige silenciosamente para uma guarda geométrica diferente.

A tabela `wall_junction_frame` mapeia46 flags para frame0..45, conforme a fonte.
Zero/outros flags retornam nullopt. `rotate_wall_junction` rotaciona circularmente
os nibbles cardinal/diagonal independentemente. A flag original precisa estar
mapeada; flags nonzero sem mapa são contadas em `unmapped`. As46 chaves são
fechadas nas quatro rotações. Resultado tem ordem level/y/x, com coordenadas,
level1..2, flags original/rotacionada e frame; máximo8192 sites.

Mapas `wall_junction_depth(zoom)` usam configuração20 far32x16,19 medium64x32,
18 near128x64, start263/dx0/dy=-1.525f*(4/2/1), clamp superior255 e ties-to-even.
A geração reproduz o incremento binary32 em cada linha com subtração inteira
exata dos significandos e arredondamento nearest-even. O helper se restringe à
faixa e constantes desses três mapas; não é um emulador geral de floats.
Ele não altera fenv e evita depender do modo de arredondamento do processo.
Uma fórmula fechada double não substitui a sequência de operações float.

Posicionamento fica com o caller: fonte Draw usa level **1..2**, então topo
Z=`level*2.95+GetAltitude(x,y)`, e tile planar `(x-.5,y-.5)`.
Não usar `(level-1)*2.95`, que é a base da parede. Altitude vem de x/y inteiro,
não da posição deslocada. Destinos far(1,80,32,16),medium(2,159,64,32),
near(4,317,128,64); SPR#4096/4097/4098 e frame do mapa. Junções usam alpha do
próprio sprite, sem máscara de objeto/padrão da parede. O batch da referência
renderiza Z_BUFFER antes de WALL, mesmo que Draw enfileire junções depois das
faces. Ordem de empates deve ser respeitada na integração.

## Evidência

20.523 verificações sintéticas passaram em Debug e ASAN/UBSAN. Cobrem todo o
domínio256 de flags,46 chaves ×4 rotações, OR/topologia/endpoints, fences,
diagonais, normalização, spill de borda, unmapped, limites/malformed e8192sites.
Todos os10.752 pixels dos três depth maps coincidem com oracle de incrementos
volatilefloat FE_TONEAREST. UPWARD/DOWNWARD/TOWARDZERO devolvem os mesmos mapas,
sem mudar o modo do caller. Compilação com -Wconversion/-Werror sem avisos.

Os três targets afetados `wall_junctions`, `saved_wall_renderer` e
`wall_sprite_composition` passaram pelo CMake em Debug e ASAN/UBSAN. São88
configurados após a inclusão; a suíte completa não foi repetida nesta unidade.
LSan desativado por ptrace; não houve leak check.

Python independente derivou junções das grades raw transpostas previamente
verificadas em architecture_arrays, e comparou todas as coordenadas/flags/frame
em três casas ×4 rotações. House01=0,House07=49,House03=114 junções por rotação;
todos unmapped0. Todos os bytes dos três mapas coincidem com incremento float32
Python explícito. Dumps Debug/ASAN iguais. Três houses originais tiveram hashes
iguais antes/depois. Não é um novo oracle completo do decoder ObjM/Arry.

SPR4098/frame7 foi inspecionado pelo decoder existente:502,46frames,paleta4096
explícita,128x64,450pixels opacos/7742transparentes. Os46 frames por zoom já fazem
parte do corpus SPR# integral verificado em `wall-resource-validation.md`.
Esta unidade não prova sua colocação/composição visual no lote nem equivalência
GPU/original.

Relatórios ignorados: `reports/junction-export.cpp`, `junction-export[-asan]`,
`verify-wall-junctions.py`, `house*-junction-expected.json`, exports por rotação,
`wall-junction-native-validation.json`, mapasZ e logs. Nenhum dado comercial é
versionado. Próxima integração resolve esses sprites no cache gráfico existente,
posiciona no topo, compõe antes das faces e valida de novo recursos/profundidade
junto aos objetos transparentes. A meta Linux funcional/VM/interação continua ativa.
