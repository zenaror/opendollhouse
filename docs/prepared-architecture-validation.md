# Preparo de arquitetura salva

`odh_prepared_architecture` expõe `prepare_saved_architecture` em
`include/odh/prepared_architecture.hpp`. Recebe grids crus de `SavedArchitecture`,
dicionários explícitos de aliases de piso/parede e opções de road flip e crop.
Produz `PreparedArchitecture`, um tipo separado que não contém os arrays
originais nem permite confundir as duas etapas no overload de room map.
Os inputs permanecem intactos.

## Referência e ordem

Referência técnica principal: Simitone
`4906db3156616bfaffc10ce0963cf08062975fef`, submódulo FreeSO
`f86bbd34b2112296ed786741f640a0535886d180` (MPL 2.0; ver
[references.md](references.md)). O caminho ativo
`VMBlueprintRestoreCmd.cs:43-56` usa `VMTS1ActivatorNew`; seu
`ConvertArchitecture` (`Utils/VMTS1ActivatorNew.cs:115-162`) chama os helpers
do activator anterior. Isto documenta uma referência de terceiros, sem prova
de equivalência ao jogo original.

O perfil implementa:

1. Normalização de `DecodeWalls`/`DecodeAdvWalls`
   (`Utils/VMTS1Activator.cs:550-608`): em tiles não diagonais, estilos presentes
   fora de {2,12,13,14} viram 1; os ausentes viram 0. Estilos diagonais não são
   normalizados porque os campos também representam pisos.
2. `RemapFloors` (`:397-418`), nas duas histórias com as flags do **térreo
   ARRY8**, como o caminho ativo. ARRY108 não participa. O road flip troca
   10/11; a condição `(original != 0 && !(flag & 32)) || original > 30` e a
   chave byte usam o valor original anterior à troca. Lookup bem-sucedido
   substitui a troca de estrada; lookup ausente a mantém.
3. `RemapWalls` (`:421-479`): padrões inferiores não zero consultam WALm.
   Em tile comum, os dois padrões superiores não zero também consultam WALm.
   Em diagonal, `TopLeftPattern` e `TopLeftStyle` não zero consultam FLRm
   separadamente e sem flags; os demais campos superiores ficam intactos.
   Quando ambos são zero, ambos herdam o piso **já remapeado**.
4. Pools e water substituem apenas o piso térreo por 65535 e 65534,
   respectivamente; bytes 0/255 são inativos e water prevalece. O fallback
   diagonal ocorre antes dessas sobreposições (`VMTS1ActivatorNew.cs:146-154`).
5. Crop do canto superior esquerdo após o preparo. A saída é row-major e
   quadrada; buildability usa `(ground_flag & 32) == 0`
   (`VMTS1Activator.cs:308-365`).

Cada lookup de padrão avançado usa somente seu byte baixo, inclusive 65534 e
65535 antes das sobreposições. Um alias ausente retém o valor corrente do
preparo, incluindo uma troca 10/11 já aplicada pelo road flip, como a fonte.
Os diagnósticos contam tentativas e misses por família no source completo
64x64, inclusive a região que o crop descarta. `floor_aliases_resolved()` e
`wall_aliases_resolved()` preservam a leitura estrita: retornam true somente se
todas as tentativas encontraram um valor. Isso inclui tentativas de IDs globais
ou de IDs sem declaração, cujo fallback mantém o byte original pela regra
`TryGetValue` de `VMTS1Activator.RemapFloors` (`:397-418`).

Para o room map preparado, `floor_identity_unambiguous()` é a medida distinta:
o preparo conta referências que usam um ID declarado em FLRm sem resolução
final. IDs nunca declarados mantêm identidade pelo fallback e não tornam o mapa
ambíguo; IDs declarados e nunca resolvidos continuam bloqueando completude
quando usados. Uma consulta à grade-base não conta como identidade usada se
uma diagonal tem campos explícitos `TopLeftPattern`/`TopLeftStyle` ou se pool/
water substitui o piso térreo antes do room fill. Os campos diagonais explícitos
continuam contando separadamente. Duplicatas seguem `BuildFloorDict` (`:482-497`): uma resolução
bem-sucedida sobrevive a outras duplicatas sem match, e um ID só fica
declarado-não-resolvido quando nenhuma entrada sua resolveu. Nenhum desses
predicados prova fidelidade dos nomes, completude do runtime ou clearance de
rota.

## Entrada, IDs e limites

O perfil exige source 64x64 nas duas histórias, flags térreas e grids de
pool/water presentes com 4096 células. Isso segue o stride 64 dos helpers de
crop e evita inventar flags ou overlays quando um chunk necessário falta.
`target_size` aceita 1..64; tamanhos maiores, source menor, extents errados,
bits de parede desconhecidos e ambas as diagonais num tile são rejeitados.
Outros metadados salvos permanecem fora desta etapa: altitude, grama,
contenção/objetos, iluminação, roofs, câmera e seleção da casa.

Os dicionários podem ser montados pela camada
[de resolução FLRm/WALm](architecture-alias-validation.md). O caller ainda
seleciona os chunks 1/0 e fornece catálogos explícitos; a descoberta dos
arquivos `.flr`/`.wll` permanece fora da API.

`WorldFloorProvider.InitTS1` e `WorldWallProvider.InitTS1` atribuem IDs dinâmicos
a partir de 256 em ordem de enumeração, com sobrescrita de nomes duplicados.
A enumeração de arquivos não é ordenada universalmente. ODH precisa de um
catálogo e precedência registrados; estes IDs numéricos não são identidades
originais TS1 nem valores Simitone universais. O perfil ODH aceita IDs de alias
256..65533 e reserva 65534/65535 para water/pool, rejeitando colisões com esses
sentinels e com padrões internos. Essa faixa limitada é uma política ODH.

`ArchitecturePreparationLimits.retained_bytes` limita o tamanho lógico do
objeto retornado e seus payloads. O processamento percorre os 8192 tiles fonte
sem guardar uma cópia intermediária completa, e a saída tem no máximo dois
grids 64x64 de pisos/paredes e um grid de buildability. O limite não mede RSS,
overhead de allocator ou memória de recursos externos.

## Room map preparado e lacunas

`generate_prepared_room_map` compartilha somente o flood fill de
[room-map-validation.md](room-map-validation.md). Consome os grids preparados
diretamente: não repete normalização, remapeamento, fallback, overlays ou crop.
Isso importa quando um fallback diagonal ficou zero antes de uma piscina
substituir o piso do tile. A API crua `generate_saved_room_map` conserva seu
comportamento e continua com `floor_resources_resolved=false`.

No mapa preparado, esse campo copia `floor_identity_unambiguous()`: fallback de
IDs sem declaração é identidade preservada, enquanto alias FLRm declarado e
sem resolução final só bloqueia quando é referenciado. O contador estrito
`floor_alias_misses` continua disponível para auditoria. Isso não atesta nomes,
conteúdo do catálogo, recursos de parede, iluminação ou equivalência runtime.
A segmentação e os
[obstáculos candidatos](room-obstacles-validation.md) continuam sem estado vivo
de entidades, portas, grupos multitile e portais. Nenhuma das APIs aceita um
destino ou dirige Routing45.

## Validação sintética

`tests/prepared_architecture.cpp` cobre limites do predicado de piso, flags
térreas nas duas histórias, road flip antes de lookup pelo original, aliases
avançados de byte baixo, remapeamento por campo de parede/diagonal, fallback e
ordem de overlays, crop/imutabilidade, misses fora do crop e limite lógico de
saída. Casos integrados demonstram mudança de partição sentinela após mapping,
estabilidade entre IDs dinâmicos comuns e ausência de fallback duplicado.
O teste anterior `room_map` permanece inalterado e verifica o perfil cru.
Não foi executado um comparador diferencial C# nem um teste do jogo original.
Em 2026-10-20, os 88 checks do teste passaram, assim como o build Debug completo
e a suíte de 133 CTests: zero falhas e um teste de janela X11 pulado sem DISPLAY.

```sh
cmake -S . -B build-prepare -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DODH_X11=ON -DODH_SDL_AUDIO=ON
cmake --build build-prepare -j8
ctest --test-dir build-prepare --output-on-failure
```
