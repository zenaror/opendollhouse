# Relacionamentos TS1 em memória

O [checkpoint seguinte de bootstrap](bootstrap-validation.md) remove o fault
de GameEdition registrado abaixo e documenta o novo gate nativo.

O runtime mantém uma cópia própria das relações de NBRS e matrizes de objetos
transitórios. Carregar o bairro não modifica o IFF comercial, e esta unidade não
grava saves nem restaura relações de objetos de ObjM.

## Referência e operandos

FreeSO main `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0, consultada
no clone local. `VMContext.cs:244–256` registra opcode24 legado e26 moderno;
`VMRelationship.cs:20–141,149–159,214–288` define o fluxo e os operandos.

Moderno: RelVar byte0, direção byte1, Flags byte2, Local byte3, VarScope uint16
LE bytes4–5, VarData signed16 LE bytes6–7. Legado: GetSet byte0, RelVar byte1,
índice de parâmetro byte2, direção byte3 e Flags byte4. O legado sempre usa
Parameters como variável, e Args0 como identidade nas direções2/3.

Direções0/1 são caller→stack e stack→caller;2/3 são stack→local e local→stack.
Com UseNeighbor(bit1), caller usa Person31 e stack/local são IDs de vizinhos,
sem exigir entidades World correspondentes. Sem a flag, os IDs resolvem entidades
World; dois avatares com NeighborIDs positivos também escolhem a matriz de bairro.
Demais combinações usam relações entre objetos.

Objetos ausentes sem UseNeighbor retornam true sem acessar a variável, seguindo
a referência. Com UseNeighbor, origem ausente é erro: o código de referência
desreferencia o registro, e ODH informa diagnóstico. O alvo é uma chave;
não precisa existir como registro de vizinho.

Bit0 FailIfTooSmall retorna false sem criar/grow quando faltam par ou índice.
Sem a flag, inclusive uma leitura cria o par e completa a lista com zeros.
RelVar é byte, mas caudas importadas maiores que256 são preservadas, sem
inventar um truncamento na carga. Chaves de relação brutas int32 são preservadas.
Valores int32 de disco tornam-se signed16 pelos16 bits baixos, como a referência.

SetMode moderno é `((Flags>>2)&1)|((Flags>>4)&2)`:0 lê,1 escreve,2 soma,
3 apenas garante par/índice e retorna true sem acessar a variável. GetSet legado
tem a mesma política, inclusive valores>=3 sem acesso variável. Bit3 é ignorado
nessa referência; não é o bit de incremento. Escrita/soma limitam[-100,100],
salvo bit6 NeverClamp. A soma faz wrap signed16 antes do limite.

Bit7 NeverPersist não muda a rota TS1; persistência TSO não está implementada.
O multiplicador de incremento é explicitamente1: corresponde ao fallback sem
DynamicTuning da referência. Não é uma interpretação comprovada de tuning do
jogo original ou de categorias TS1.

## Perfil limitado do ODH

A primitiva suporta temporários, argumentos, locais e StackObjectID, além da
memória externa fornecida pelo host, incluindo BCON e PersonData indireto.
VarScope>255 é recusado quando a variável é usada. FailSmall e SetMode>=3 não
validam variáveis que a referência não acessa.

A leitura de relacionamento na referência ignora o booleano retornado por
SetVariable. ODH informa erro para um destino não suportado/recusado e não
publica crescimento de relações nesse caso. Isso é uma diferença explícita
do perfil limitado, necessária para não disfarçar memória ainda não implementada
como execução bem-sucedida.

O estado limita fontes4096, pares16384 e valores65536 no total. Matrizes de
objetos usam geração da origem; não herdam dados de um novo objeto que reutiliza
o mesmo ID. Gerações antigas continuam contadas até descartar o estado. O alvo
é ObjectID convertido para uint16, seguindo a referência. O índice reverso
MayHaveRelToMe e a limpeza de relações ao excluir entidades seguem pendentes;
não há integração com persistência ou restauração ObjM.

Leituras/no-ops sem crescimento não alocam armazenamento de relações. Escritas
ou crescimento de um par existente preparam uma cópia da lista. A criação de
par/origem prepara uma cópia temporária de toda a matriz limitada; nesse caminho
a matriz duplicada precisa ser considerada no consumo máximo.

Validação, orçamento e alocação antecedem publicação. Callbacks de escrita
recusada devem manter sua memória intacta; callbacks não podem reentrar ou mutar
o estado de relacionamentos; observers não podem lançar.
O estado e World precisam viver mais que o callback/VM. Esses limites tornam
as alocações previsíveis; não são uma medição de consumo real do New3DS.

O host liga o backend quando recebe `--neighborhood`. JSON inclui contexto,
matriz, origem/alvo, direção, SetMode, valores antes/depois, criação/crescimento,
sucesso e contagens de estado. Não fornecer bairro mantém a primitiva sem backend,
em vez de fabricar um save vazio.


## Gates nativos no Linux

Comando base: [avatar-neighbor-validation.md](avatar-neighbor-validation.md),
House01/zoning/`--initialize --neighborhood --seed 1`. Os personagens são
visitantes, sem família ativada; não são restaurações das respectivas casas.
Seed1 é fixture do host. Os IFF privados e NBRS são recursos externos originais.

Bob, `User00000.iff`, GUID `0x32aa2056`, vizinho48/família1:

- Init180; Main84/1 tick; total264, antes254.
- PersonGlobals8211 pc1 lê RelVar0 de48→47 para Temp0:55. Não cria/grow.
  A rotina de relacionamentos conclui e retorna ao Main.
- Contagens permanecem33 fontes/22 pares/27 valores.
- Main para em PersonGlobals8193 pc26, leitura scope6/index20 GameEdition,
  ainda unsupported. Não há sucesso completo de Main.

`User00003.iff`, GUID `0x29753b90`, vizinho45/família3:

- SHA256 do personagem:
  `bdb3b1784ae419bbfe83bb066fab83ed23a473b7fceebda9b347901eef641159`.
  No comando base, trocar somente `--avatar-file` para esse arquivo.
- Init180; Main85/1 tick; total265.
- NBRS original não contém45→44. PC1/read/FailSmall retorna false sem crescer.
  PC5 escreve20 via Tuning scope26/index8583 (`0x2187`).
  O resolver seleciona BCON8195/index7 semiglobal. O novo par/lista existe
  somente no estado próprio; contagens33 fontes/23 pares/28 valores.
- Main chega à mesma leitura GameEdition em8193 pc26.

Leitura estrutural independente em Python confirmou o par original Bob48→47
`[55,0]` e a ausência de relações de44/45. Hash do NBRS original, conferido
antes e depois dos dois probes:
`e585615fed1f9ddfb15ff8f5ebe7a09ea48a1369e9a3106f8331d3d9f5dde323`.

Relatórios completos Debug/ASan coincidem byte a byte:

| Personagem | Arquivos ignorados em reports/ | SHA256 do JSON |
|---|---|---|
| Bob | bob-relationship-native.json, bob-relationship-native-asan.json | 47be2a1c2d14038702dc2acdcab5a9cdf7fdeba85459b011c9bdc3dba458e70d |
| Vizinho45 | family3-relationship-native.json, family3-relationship-native-asan.json | 59d59688adb54a5a5b95457ac6ff4de340349315eeaa7fde08b14b364217cc4f |

Seed final16610512366814951206 nos dois gates. Saída3 é a dependência
GameEdition não suportada; não é conclusão bem-sucedida do cenário.

## Verificação e pendências

112 checks do helper cobrem os dois operandos, todas as direções, roteamento
neighbor/object, chaves int32/low16/caudas, SetMode3, clamp/wrap, FailSmall,
ausência de origem/alvo, memória/BCON/PersonData indireto, limites e gerações.
Testes de falha de alocação verificam que a criação de par não chama o writer
externo nem publica estado parcial. Outputs de VM são exercitados também
com uma VM real do ODH, separadamente do dispatch direto do helper.

Fixtures CLI verificam leitura de par salvo, set/add, reverse, crescimento,
origem ausente, opcode legado, output inválido sem criação e mode3 com
variável deliberadamente inválida e não usada.

Debug e ASan/UBSan:56 testes passaram e1 X11 foi pulado,57 configurados.
LeakSanitizer foi desativado por ptrace. Não houve execução comparativa do
original, lote jogável, restauração ObjM/persistência completa ou hardware.

Próximo contrato: GameEdition/global20 e sua derivação do bootstrap/instalação.
Não atribuir um valor por suposição para passar a próxima branch. Ativação FAMI,
aparência/integração gráfica do personagem e carregamento real do lote permanecem
necessários para a primeira versão Linux.


Contrato seguinte localizado em leitura estática: FreeSO `VM.cs:223–229`
define global20=255 no Init da VM, como política de todas as expansões.
`VMTS1ActivatorNew.cs:423–433` também sobrescreve20=255,17=4 e25=4 depois
de copiar SIMI na importação de IFF; o activator antigo faz o mesmo.
Isso não identifica expansões realmente instaladas nem prova a máscara do
original. Restore de VMMarshal em `VM.cs:919–925` atribui os globals salvos
sem esses overrides, portanto é outro contrato. `VMExpression.cs:185–186`
numera flags a partir de1: o gate pc26 testa máscara0x01, não0x02.
Implementação de um perfil de bootstrap explícito e seu avanço nativo ficam
para a unidade seguinte.
