# Personagem real e herança do bairro

Checkpoint de 2026-10-13. O [gate de relacionamentos](relationship-validation.md)
posterior remove o fault opcode26 e documenta o avanço até GameEdition. O host aceita um IFF privado externo de personagem
com `--avatar-file IFF OBJD`, além do caminho existente `--avatar FAR ENTRY OBJD`.
Os recursos comerciais permanecem fora do checkout e não são modificados.

## Contrato e implementação

Referência: FreeSO main
`4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0, consultada no clone
local em `refs/upstream/FreeSO`. Pontos relevantes:

- `VMContext.cs:1474–1484`: construção/Init e Main empilhado antes da herança
  por GUID; o primeiro tick de Main ocorre depois.
- `VMAvatar.cs:949–961`: cópia de PersonData, retenção de Gender65,
  PersonType32 e VisitorSchedule35, configuração de NeighborId31, classificação
  de família, GreetStatus34=0 e reaplicação de IsGhost68.
- `NBRS.cs:38–46`: mapa GUID construído na ordem serializada, com a última
  entrada ativa de um GUID vencendo, antes da ordenação por NeighborID.
- `VMEntity.cs:77–87`: atributo ausente retorna zero; escrita cresce o banco.
  Capacidade de List não é Count: labels STR256 não preenchem atributos.

`NeighborContext::id_for_guid` e `by_id` resolvem a identidade sem substituir
o caller pelo primeiro vizinho enumerado. A herança ocorre somente no modo
`--initialize`, depois do BodySTR e antes de qualquer instrução Main.
Bairro ausente ou GUID sem correspondência conserva o banco do template;
o JSON explicita essa condição. Probes com rotina explícita mantêm a política
anterior e não restauram o banco salvo.

`inherit_neighbor` valida avatar, GUID, banco anterior com palavras0..68 e
perfil bruto NBRS4/10. Dados salvos80 são projetados para88 com zeros80..87;
dados256 usam as primeiras88 palavras. Sem banco salvo, conserva o banco
anterior inteiro, inclusive a cauda101 do construtor. A publicação usa uma
única substituição atômica e uma cópia própria, com orçamento compartilhado
de World. Identidade, geração, posição, motivos e demais bancos não mudam.

Gender65 e VisitorSchedule35 vêm do runtime pré-herança. PersonType32
prévio não zero é preservado; quando era zero, a família restaurada61 é
comparada numericamente ao ID uint16 da família ativada. Família ausente
produz visitante1. Um valor salvo signed-1 não equivale ao ID65535.
NeighborId31 recebe a identidade real, GreetStatus34 é zerado; ghost salvo
é conservado e informado, sem alegar efeitos de renderização.

O helper admite uma família ativada explícita. Este host ainda passa ausência:
selecionar uma casa não ativa uma FAMI nem transforma Bob em residente.
Ativação de família e restauração do lote são pendências separadas.

## Atributos limitados de TS1

World conserva a política fixa como padrão. O startup do host seleciona
`AttributePolicy::ts1_bounded`: apenas scopes0/1 passam a ler atributos
ausentes como zero, sem alocar; uma escrita cresce com zeros até o índice,
validando limites antes da publicação. A entidade precisa existir.

A cobertura tem índices signed não negativos e limite por banco
`WorldLimits::bank_words` (padrão4096), além do orçamento total. Não implementa
todo o domínio ushort65536 usado pela referência. Escritas recusadas e
exceções de alocação deixam banco e contabilidade intactos. Leituras de
outros scopes não são convertidas em zero. O JSON inclui a política e o
banco de atributos resultante.

## Gate nativo de Bob

Recurso externo `UserData/Characters/User00000.iff`, SHA256
`900835b71807bac87ed0f431167850a5ed666b6008d7c37739042fd94610b7d5`.
OBJD128 versão138/type2, GUID `0x32aa2056`; NBRS identifica vizinho48,
família1, banco bruto80. BodySTR200 adulto/male/age27 contém:

- body: `b002mafat_01,BODY=B002MAFatlgt_slob`;
- head: `c010ma_baldbeard,HEAD-HEAD=C010MAlgt_baldbeard01`.

Essas referências de aparência são metadados, sem resolver seus meshes/texturas.

Reprodução a partir do checkout:

```sh
./build/odh-person \
  --avatar-file '../referencias/The Sims/UserData/Characters/User00000.iff' 128 \
  --semiglobal '../referencias/The Sims/GameData/Global/Global.far' PersonGlobals.iff \
  --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff \
  --initialize \
  --lot '../referencias/The Sims/UserData/Houses/House01.iff' 1 \
  --zoning '../referencias/The Sims/UserData/LotZoning.iff' \
  --neighborhood '../referencias/The Sims/UserData/Neighborhood.iff' --seed 1
```

House01 é um contexto de teste com Bob visitante; não é uma restauração da
família residente de House07. Seed1 é fixture do host, não seed salva original.

Resultado Linux:

- Init4097 retorna true em180 instruções; entrada8 ID0 é pulada.
- BodySTR é aplicado; herança liga caller1 ao vizinho48 e reduz banco101→88.
  Família61=1, PersonType32=1, Gender65=0, VisitorSchedule35=0, Ghost68=0.
  A personalidade salva substitui Neat1000 do pós-body: palavra7 resulta300.
- Main executa74 instruções/1 tick, total254, enumerando11 até47.
  Para em Relationship opcode26, PersonGlobals8211 pc1, ainda unsupported.
  Caller Person31=48 existe no bairro; a origem deixou de ser template0 ausente.
- Banco de atributos permanece vazio: o Init leu os zeros ausentes sem precisar
  crescer. O crescimento é exercitado pelos testes sintéticos, não por esse gate.
- Seed final16610512366814951206. Relatórios Debug/ASan completos coincidem
  byte a byte, SHA256
  `d10466d8d1b0f870531a2775f773b72bfcaf84c4b95f22c6f9d54d0160723b5e`.
  Arquivos ignorados `reports/bob-neighbor-native.json` e
  `reports/bob-neighbor-native-asan.json`. Saída3 representa a dependência
  ainda unsupported, não conclusão bem-sucedida de Main.

## Verificação e próximos gates

94 checks sintéticos de herança;141 de substituição de PersonData;37 da
política de atributos;132 de NeighborContext (incluindo3 novos de GUID).
Cobrem projeção, campos retidos, ausência de dados, comparações signed/uint16,
ownership, limites, rejeição atômica, identidade/geração e demais bancos.
Fixtures CLI cobrem a ordem antes de Main, carga standalone, GUID ausente,
última correspondência serializada, isolamento de probe e crescimento de atributos.

Debug e ASan/UBSan:55 testes passaram e1 X11 foi pulado (56 configurados).
O teste CLI foi corrigido para expor atributos no JSON e repetido em Debug;
a suíte sanitizada completa passou. LeakSanitizer ficou desativado por ptrace.

Próxima dependência: backend de Relationship26 com valores mutáveis próprios,
sem alterar NBRS original. Depois, ativação FAMI, aparência do personagem e
integração do Main/fila ao cenário gráfico. Não houve execução comparativa
do original, restauração completa do save, lote jogável ou teste em consoles.
