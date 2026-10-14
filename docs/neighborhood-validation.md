# NBRS, enumeração de vizinhos e PersonData do bairro

Checkpoint de 2026-10-13. `odh-person --neighborhood NBRS_IFF` lê o bairro sem
gravar em arquivos originais. O Main nativo já seleciona uma identidade de
vizinho e lê seus dados mesmo sem avatar correspondente instanciado no World.
Relacionamentos e vínculo do caller ao vizinho real seguem pendentes.

## Fonte e leitura estrutural

FreeSO `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL2, consultado no clone
local. Caminhos relativos a `TSOClient/`:

- `tso.files/Formats/IFF/Chunks/NBRS.cs:29–53,122–179`: layout e leitura.
- `tso.files/Utils/IoBuffer.cs`: strings como sequência de bytes/characters.
- `tso.content/TS1/TS1NeighbourProvider.cs:267–277`: ordenação/continuação.
- `tso.simantics/Primitives/VMSetToNext.cs:60–65,263–279,299–311`: modo5.
- `tso.simantics/Engine/VMMemory.cs:132–134`: scope32 NeighborPersonData.

`read_nbrs` preserva pad/version/SRBN, ordem serializada e extensão de cauda.
Marker0 é um placeholder de quatro bytes, sem ID ou record serializado.
Marker1 tem versão4 ou10, nome C string bruto, byte de padding quando seu
comprimento é par, campos desconhecidos, person mode, ID signed16 e GUID.
Versão10 também preserva unknown3. PersonMode>0 guarda exatamente80 palavras
na versão4,256 na versão10; outros modos conservam banco ausente.

As relações conservam key count, key e todos os valores int32 do arquivo.
Não são estreitadas para signed16 neste leitor. Key count diferente de1,
layout desconhecido, count negativo, chave duplicada ou truncamento dão erro.
O count declarado deve ser inteiramente atendido; o reader FreeSO pode parar
quando o stream acaba, mas ODH não transforma um count truncado em dado completo.

Containers62/63 foram observados no corpus;73 é o perfil do writer da referência,
testado com fixtures sintéticas. Não se afirma haver container73 nativo nesta
instalação. Bytes de pad/campos desconhecidos são preservados, sem exigir zero.

Limites configuráveis padrão: payload1MiB, bytes decodificados cumulativos512KiB,
4096 entradas,256 bytes por nome/64KiB de nomes cumulativos,16384 relações e
65536 valores de relação. Tamanho/contagens são conferidos antes de alocação;
somente nomes/person data/valores entram no budget de bytes decodificados.

Leitura compilada dos nove `Neighborhood.iff`, sem copiar fixtures comerciais:

| Origem | Versão | Entradas | Ativas / placeholders | PersonData80 | Relações / valores |
|---|---:|---:|---:|---:|---:|
| UserData | 62 | 49 | 33 / 16 | 31 | 22 / 27 |
| UserData2 | 63 | 49 | 23 / 26 | 13 | 2 / 4 |
| Template e UserData3–8 | 63 | 49 cada | 21 / 28 cada | 11 cada | 0 / 0 |

Todos têm cauda zero; versão10/person256 só foi validada sinteticamente.
UserData NBRS1 tem6734 bytes. SHA256 do Neighborhood.iff:
`e585615fed1f9ddfb15ff8f5ebe7a09ea48a1369e9a3106f8331d3d9f5dde323`.
Um parser Python independente confirmou as contagens, consumo total, ID11,
nome `maid`, person mode5 e **PersonData61=0**. Esse valor corrige uma hipótese
anterior de família4; a leitura atual não usa aquela hipótese.

## Snapshot de runtime e SetToNext

`NeighborContext` possui os dados carregados, com índice estável ordenado por
ID signed16 e lookup somente de records ativos. Duplicação de ID ativo é erro.
Placeholders têm ID efetivo0 no índice de runtime da referência; isso é um
default identificado do runtime, não um campo inventado no registro estrutural.
Sua presença e ordem relativa são preservadas, inclusive o caso incomum de um
record ativo0 seguido de placeholder0.

Se current existe no lookup ativo, a próxima identidade é a posição seguinte
no índice completo. Caso contrário, é a primeira identidade estritamente maior.
Não há wrap. O provider retorna−1 quando acaba; o primitive retorna false para
qualquer resultado negativo, sem escrever o alvo. Portanto IDs negativos podem
estar no índice sem serem publicados por esse primitive.

Opcode31/search type5 enumera vizinhos; não é Person1 e não percorre entidades
do World. Flags com bit80 usam owner/data codificados; flags antigas sem bit80
forçam StackObjectID10/data0. GUID/local não selecionam o resultado no modo5.
Alvos temp/argumento/local/StackObject e memória externa explícita são suportados,
incluindo remapeamento PersonDataByTemp. Writes rejeitados permanecem faults;
outros search types não receberam um sucesso artificial.

A VM agora recebe `PrimitiveOutcome::stack_object_write`. Só branches síncronos
podem publicá-lo. Outputs de frame/temp/context são validados antes de qualquer
publicação. Caller, Callee e code owner permanecem intactos. O ID pode pertencer
a NBRS sem ser uma entidade válida do World; não há lookup de entidade para
autorizar o setter. WAIT/call/unsupported com esse output são recusados.

## Scope32 e procedência dos zeros

NeighborPersonData usa o ID de StackObject e o índice do operando. O runtime
FreeSO aloca88 palavras: record4 serializa80 e deixa80–87 zero; record10 serializa
256 e só usa as primeiras88. ODH preserva os dados completos no reader e aplica
esse perfil na consulta, distinguindo `serialized` de `legacy_zero_extension`.
Um banco presente só admite índices0–87; os demais permanecem unsupported.

Com bairro carregado, vizinho ausente ou banco explicitamente ausente retornam
zero pela política null-conditional da referência, inclusive para índices que
seriam inválidos em um banco presente. O log identifica
`missing_neighbor_reference_zero` ou `absent_person_data_reference_zero`.
Sem bairro carregado, scope32 e enumeração permanecem unsupported. Isso distingue
recurso não fornecido de um bairro carregado e vazio. Scope32 não tem writer.

O snapshot é imutável nesta etapa. Relações lidas são preservadas, mas não
criadas ou modificadas, nem persistidas silenciosamente nos saves.

## Gate nativo e próxima dependência

Adicionar ao comando de [avatar-startup-validation.md](avatar-startup-validation.md):

```sh
--neighborhood '../referencias/The Sims/UserData/Neighborhood.iff'
```

TemplatePerson com House01/zoning, `--initialize --seed 1`: Init179 instruções,
Main14/1 tick, total193. Antes, Main parava no opcode31 em11 instruções.
Agora current0 seleciona11; scope32/index61 lê0 do record `maid`; Main vai para
Relationship opcode26 em PersonGlobals8211 pc1, ainda unsupported. O trace
preserva StackObject11 nesse frame. JSON nativo Debug/ASan coincide byte a byte.
Relatório ignorado: `reports/neighbor-main-native.json`.

O caller continua um template anônimo com Person31=0; selecionar vizinho11
não o transforma no caller. A referência de relações usa Person31 do caller
como origem. `VMRelationship.cs:63–97` dereferencia o vizinho de origem; uma
origem0 ausente gera exceção nessa referência, não false ou uma nova relação.
É necessário vincular o avatar real antes de implementar esse caminho como
execução válida. Não se cria vizinho0 nem se atribui maid11 ao caller para passar.

O vínculo correto segue o GUID do avatar: `VMContext.cs:1474–1484` chama
`InheritNeighbor` depois da construção e antes dos ticks de Main.
`VMAvatar.cs:949–961` restaura PersonData e configura Person31 com a identidade
real, preservando campos específicos do runtime. A escolha de personagem,
seu recurso/appearance, herança e backend de relações são as próximas unidades.

## Verificação e limites

3087 checks sintéticos de reader,105 de outputs de StackObject e129 de runtime
do bairro passaram. Cobrem truncamentos, limites, versões, campos brutos,
placeholder/order/signed IDs, ausência/zero extensão, todos os bancos de alvo,
rejeição atômica e leitura scope32 depois de output real da VM. CLI cobre recursos
ausentes/duplicados, versão legada dos operandos, seleção/exaustão e origem de zeros.

Debug e ASan/UBSan:52 testes CTest passaram;1 X11 pulado. LeakSanitizer desativado
devido a ptrace. Leitura estrutural, runtime ODH e algoritmo da referência são
evidências distintas; não houve execução do original ou teste de hardware.
