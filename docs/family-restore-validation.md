# Família ativada e contêiner de entidades salvas

O [checkpoint de inventário](inventory-validation.md) posterior remove o
gate51 abaixo e registra o avanço até AnimateSim44 e a auditoria das posições.

Checkpoint de 2026-10-13. O host pode selecionar a família pela casa correta,
publicar seu ChunkID em global9 e usar CurrentFamily na herança do personagem.
Também lê e conserva os blocos comprimidos ObjM do lote. Não restaura ainda
as instâncias, posições, arquitetura ou a família inteira em World.

## Referência e FAMI

FreeSO main `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0,
consultado localmente em `refs/upstream/FreeSO`:

- `FAMI.cs`: prefixo, membros e SelectWholeFamily;
- `TS1NeighbourProvider.cs:95–100,202–206`: índice da família pela casa;
- `VMTS1LotState.cs:19–24`: ActivateFamily escreve global9 com ChunkID e
  guarda CurrentFamily. Global9 não recebe zero nessa operação;
- `VMTS1LotState.cs:30–53`: VerifyFamily é uma etapa distinta, com criação
  de membros ausentes, posicionamento na caixa de correio e permissões;
- `VMAvatar.cs:949–961`: herança e classificação residente/visitante.

O reader próprio lê reserved/version, assinatura IMAF, seis campos signed32,
contagem signed32 e GUIDs uint32. Versão e metadados brutos são conservados;
não se inventa whitelist. A extensão restante é registrada, sem interpretar
a cauda opcional da referência. Limites padrão:64KiB payload e256 membros,
validados antes da alocação; contagem negativa ou truncamento é recusado.

FamilyContext possui snapshot imutável de até4096 famílias/65536 membros,
com limite256 por família. ChunkIDs duplicados são recusados. A chave de casa
usa signed low16 e a última família na ordem serializada vence, sem ordenar
por ChunkID. FamilyNumber é outro identificador; não substitui ChunkID.

`--activate-family` exige `--initialize`, `--lot` e `--neighborhood`.
O host limita cumulativamente os membros antes de ler cada FAMI, seleciona
a família pelo número do lote e exige GUID do avatar tanto nos membros
quanto no índice NBRS. Ausência ou inconsistência é erro explícito.
Global9 recebe signed low16 de ChunkID antes de Init; a herança recebe
esse mesmo ChunkID antes de Main. Escrita posterior em global9 muda a
palavra, sem trocar automaticamente CurrentFamily. O JSON separa proveniência
`family_activation`, `runtime_bootstrap` e `lot_context`.

Sem a flag, permanece o perfil anterior de visitante. Não há SelectWholeFamily,
VerifyFamily, RuntimeSubset, spawn coletivo, permissões ou posicionamento na
caixa de correio. Cada gate abaixo constrói somente um avatar.

## ObjM: mapa e blocos próprios

Referências: `OBJM.cs:669–719` e `IffFieldEncode.cs`. O FourCC IFF é `ObjM`,
case-sensitive; a assinatura interna é MjbO. O host seleciona ObjM1 do lote,
recusa duplicação e informa null quando ausente em fixtures SIMI antigas.

O reader valida header LE, compression1 e tabela MSB-first de campos signed
de largura5/8/13/16, convertidos para uint16. Os campos são valores diretos,
não deltas. ID0 encerra a tabela; IDs duplicados são recusados no perfil
limitado, enquanto a referência sobrescreve. Interrupt equivale ao teto da
posição em bits dividido por8, inclusive nas fronteiras exatas de byte.

Cada bloco tem skipOffset signed LE32 relativo ao byte12 do payload. O
reader valida offsets negativos, regressivos e fora do payload, copia bytes
e conserva o offset após a palavra skip, inclusive paridade para o futuro
decoder. Limites padrão:16MiB payload,4096 IDs e8MiB de bytes próprios.
São limites configurados, não medição do orçamento do New 3DS.

Ordem dos blocos não determina ObjectID: ele fica dentro da instância.
Não se associa bloco à entrada de mapa por índice. Cauda desconhecida é
registrada como extensão. JSON explicita `instance_state_decoded:false` e
`world_entities_restored:false`; nenhum bloco vira entidade neste checkpoint.

Leitura de três saves externos, Debug e ASan/UBSan:

| Casa | Versão | IDs/blocos | Bytes próprios | Cauda |
| --- | --- | --- | --- | --- |
| House01 | 62 | 60 | 4357 | 7 |
| House07 | 62 | 182 | 15109 | 7 |
| House03 | 62 | 267 | 20013 | 7 |

## Gates Linux de residentes

### Main integrado de toda a família (2026-10-23)

Sobre a revisão base `52e8306`, com mudanças ainda não commitadas, o gate
headless curto iniciou os Main dos dois moradores reais já presentes em
House07 no mesmo World e relógio. Contra os dados em
`/home/rafael/_PROJETOS/TheSims1/referencias/The Sims`, as threads 105/GUID
`850010198` (`--resident-id 48`) e 137/GUID `844195932`
(`--resident-id 47`) completaram 32 ticks cada; ambas passaram com
`scheduler_entities=2` e as posições dos moradores preservadas. Os quatro
arquivos originais protegidos mantiveram seus SHA256 no gate. Tentativa de
salvar a sessão multi-residente foi rejeitada antes de qualquer escrita.
Build `odh-person` concluído e `git diff --check` passou. O CTest anterior
registrou 201 aprovados e 1 ignorado (`linux_window_backend`, sem display).

Este gate demonstra Main integrado dos dois residentes presentes nesse
cenário. O resultado é curto e headless: não valida UI/visuais, autonomia,
roteamento, Snap/postura dos extras, residentes ausentes ou jogabilidade
completa. Somente os arquivos comerciais cobertos pelos hashes desse gate
tiveram fingerprint; não se mediu nem se deve inferir uma contagem total de
inputs. O checkpoint de save/resume posterior está documentado abaixo.

### Dois moradores carregados no mesmo lote (2026-10-23)

O teste opcional `tests/house07_paired_main_smoke.py`, executado com
`/tmp/odh-host-integration/odh-person` e os dados instalados em
`/home/rafael/_PROJETOS/TheSims1/referencias/The Sims`, confirmou que os dois
moradores já presentes no save House07 podem ter seus recursos de personagem
verificados e executar seus próprios Main no mesmo World e relógio. Os IDs
105/GUID `850010198` e 137/GUID `844195932` ficaram `waiting` após 32 ticks,
com 235 e 232 instruções registradas, respectivamente. As duas posições e a
contagem de 182 entidades permaneceram iguais. A tentativa de associar o
segundo morador ao arquivo de personagem errado foi recusada.

Este gate é uma base real para ativar a família inteira: os dois avatares já
estavam salvos no lote e cada Main foi iniciado pela opção de teste
`--headless-second-saved-main`. Esse checkpoint anterior não prova autonomia
contínua, controles individuais, criação de moradores ausentes, posicionamento
em lote vazio ou o caminho normal do launcher; ainda não há
`VerifyFamily`/spawn coletivo nesse fluxo. Resultado observado: processo
terminou com código 3 pelo limite deliberado de 32 ticks, sem falha do host.

### Save/resume dos dois moradores House07 (2026-10-23)

O teste integrado `tests/house07_household_runtime_cli.py` agora grava e retoma
a família inteira com os dois moradores House07 em processos novos. Para cada
morador selecionado como principal, a execução retomada e salva foi byte a byte
igual à continuação contínua pelo mesmo número total de ticks. O teste também
rejeitou arquivos de sessão sem a seção de household, com roster divergente e
com conteúdo truncado/malformado antes de alterar o arquivo salvo válido.
Os arquivos comerciais incluídos no fingerprint antes/depois mantiveram seus
hashes. Os testes focados passaram 11/11 em Debug e 11/11 em Release. A
validação completa Release também concluiu o build e o CTest com 206 testes:
205 passaram, 1 foi ignorado (`linux_window_backend`, por falta de display) e
0 falharam.

O snapshot do conjunto de arquivos IFF de personagens usados pelo household
tem budget cumulativo de 64 MiB. Se o conjunto ultrapassa esse limite ou um
arquivo muda enquanto é lido, a execução falha antes da escrita do save.
Esse limite protege a leitura e o pinning dos recursos; não é evidência de
consumo de memória total nem de suporte a qualquer conjunto de personagens.

O gate demonstra continuidade determinística desse perfil House07 e build,
não jogo completo. Postura/acessório de morador adicional, roteamento em curso,
dynamic code owners e residentes ausentes continuam sem suporte no save/resume.
UI e janela X11, interação visual com os dois moradores e uma sessão longa
permanecem pendentes.

```sh
./build/odh-person \
  --avatar-file '../referencias/The Sims/UserData/Characters/User00000.iff' 128 \
  --semiglobal '../referencias/The Sims/GameData/Global/Global.far' PersonGlobals.iff \
  --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff \
  --initialize --activate-family \
  --lot '../referencias/The Sims/UserData/Houses/House07.iff' 7 \
  --zoning '../referencias/The Sims/UserData/LotZoning.iff' \
  --neighborhood '../referencias/The Sims/UserData/Neighborhood.iff' --seed 1
```

Repetir com User00001.iff testa o membro47 separadamente. UserData possui
seis FAMI; chunk1/version7/HouseNumber7/FamilyNumber5/Budget665 contém
Bob GUID0x32aa2056 e membro47 GUID0x3251685c. Ambos herdam família61=1,
PersonType32=0 (residente); gênero65 é0 e1, respectivamente.

Ambos executam Init180+Main108=288 instruções/1 tick. Param na primitiva51
de inventário, PersonGlobals8664 pc0, operandos `[3,6,17,13,56,41,86,78]`.
Call chain:8193 pc4 →8283 pc0 →8664 pc0. Exit3/host_failed=false;
seed final16610512366814951206. O novo gate decorre da família ativada;
não indica que roteamento ou restauração espacial tenham sido concluídos.

Relatórios completos Debug/ASan coincidem byte a byte, ignorados em
`reports/{bob,member47}-resident-native[-asan].json`:

- Bob SHA256 `a15b03c5091b555918706265bffa32584ddaa3856626af2547640330160b0636`;
- membro47 SHA256 `490534a45f645f2097e7d6377ef29807d89b2dfd8a89c4e147b5c7b86a2f96fc`.

Originais conferidos antes/depois e preservados:
Neighborhood SHA256 `e585615fed1f9ddfb15ff8f5ebe7a09ea48a1369e9a3106f8331d3d9f5dde323`;
House07 SHA256 `5b162c81135612acfa65628438df2246cd25ad8cb15e2646a28bb32e38de5133`.

## Checks e próxima integração

107 checks FAMI e271 checks sintéticos ObjM cobrem ownership, assinatura,
contagens/limites/truncamentos, mapeamento de casas, signed keys, offsets e
fronteiras de bits. Executar odh-objm-tests com os três arquivos acima produz
1298 checks totais; essa contagem inclui recursos nativos, não é a contagem
do teste sintético CTest. CLI cobre dependências da flag, família ausente,
casa divergente, GUID não membro, ordem de ativação e global9 mutável sem
troca de CurrentFamily. Debug e ASan/UBSan:61 testes passaram e1 X11 foi
pulado (62 configurados); após integração final do contêiner, person_cli
foi repetido em ambos e FAMI/ObjM em ASan. LSan desativado por ptrace.

O gate51 é FindToken, tipo6/GUID0x4e562938, inventário de Caller.Person31.
NGBH é um recurso separado de NBRS/ObjM. Ausência de inventário ou token
nessa operação retorna false sem escrever temps na referência; recurso
não carregado não pode ser tratado como inventário vazio. Implementar leitura
NGBH e consulta limitada é a próxima dependência de Main residente.

Para a restauração espacial, o prefixo OBJM contém footprint, X/Y/Level,
atributos,8 temps,68 ObjectData e5 extras. ObjectData11 determina ObjectID;
depois IDToOBJT deve resolver objt/OBJD antes do restante variável da instância.
As coordenadas são conservadas pela rota VMTS1ActivatorNew; ainda não estão
decodificadas aqui. Aparência, arquitetura, Main/fila/render e lote Linux
jogável seguem necessários. Sem comparação com execução do original ou hardware.
