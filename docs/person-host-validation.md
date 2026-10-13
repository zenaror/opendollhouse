# Host de execução do avatar e gates de inicialização

Checkpoint de 2026-10-13. `odh-person` liga um avatar e seu semiglobal a uma VM
permanente, com Sleep e InteractionQueue no mesmo relógio. Executa o Main real
ou uma rotina privada selecionada explicitamente e preserva o primeiro fault.
O avanço posterior de scope26 está em [tuning-validation.md](tuning-validation.md).
É um host de diagnóstico sem janela; não substitui o preview e não representa
um avatar/lote completamente inicializado.

## Recursos e uso

O host carrega owner1 para os BHAVs privados do avatar e seu semiglobal;
globais são compartilhados. Um alvo opcional usa owner2. As entradas FAR são
exatas, e GLOB128 Pascal deve corresponder ao semiglobal. Recursos com BHAVs
fora de seu papel, duplicações, OBJD ausente, master/multitile e OBJf do avatar
não são aceitos. O perfil do construtor exige OBJD138/ObjectType2 (campo+18).
A definição de uma ação não é uma factory completa do alvo: o World constrói
apenas seu tipo/GUID e identidade para a fila.

```sh
./build/odh-person --avatar '../referencias/The Sims/GameData/Objects/Objects.far' 'People\TemplatePerson.iff' 128 --semiglobal '../referencias/The Sims/GameData/Global/Global.far' PersonGlobals.iff --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff
```

`--routine 4097` inicia essa rotina privada em vez do Main da definição. Não
executa automaticamente Init/entry8/body strings. `--action FAR ENTRY OBJD ID`
oferece uma ação explícita à fila; não avalia TTAB/eligibility nem presume que
o Main a alcançou. `--flags WORD` e `--age YEARS` são overrides explícitos de
fixture, registrados no JSON; não são valores extraídos de uma pessoa/saved lot.

```sh
./build/odh-person --avatar '../referencias/The Sims/GameData/Objects/Objects.far' 'People\TemplatePerson.iff' 128 --semiglobal '../referencias/The Sims/GameData/Global/Global.far' PersonGlobals.iff --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff --routine 4097
```

O estado registra initialização incompleta, arquivos/GUID/owners/root, trace,
contexto da stack, estado final selecionado, ticks/dispatches, fila e completion.
Sleep solicita ciclos RNG ao callback, mas o backend RNG continua ausente.
Somente primitivas0/17 e o kernel Expression/chamadas estão ligados neste host;
serviços ausentes permanecem unsupported, sem sucesso substituto.

O host chama observe antes de run e após serviço/execução, conforme o contrato
da fila. Avança tick uma vez, passa o mesmo relógio a Queue e Sleep, e retoma
execução. Não reinicia a VM para cada ação. Limites:10.000 dispatches por run
completo, ticks configuráveis0..4096 (padrão256), capacidade FIFO32 e limites de
frame/owners do kernel. Trace é limitado pelo orçamento de dispatches.

Exit0 significa probe terminado sem fault/budget/ação pendente; o resultado
booleano de uma ação ainda está no JSON e não comprova interação correta.
Exit3 indica runtime fault, falha do host, budget ou ação oferecida incompleta,
mesmo se a rotina Main selecionada tiver retornado. Exit2 indica uso/recurso
inválido. O JSON conserva o status real da VM; não troca waiting/yielded/fault
por um sucesso artificial para encerrar o probe.

## Construtor parcial e ordem da referência

Fonte fixa FreeSO `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL2:
[VMEntity](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Entities/VMEntity.cs),
[VMAvatar](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Entities/VMAvatar.cs),
[OBJD](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.files/Formats/IFF/Chunks/OBJD.cs).

A referência cria ObjectData80 zero e seta ChairFacing (Flags8=256). O avatar
começa com PersonData101 e MotiveData16 zero e Contained3; base.Init executa
entrada0 e entrada8 e empilha Main. Depois, Avatar.Init inicializa animações,
body strings, idade/gênero, motivos100 exceto SleepState, e NeatPersonality1000.
A BodySTR200 deste TemplatePerson indica idade27; ela é aplicada após base.Init
na referência, portanto não deve ser antecipada ao executar seu Init nativo.

O host reproduz somente um subconjunto antes dessa pipeline: bancos101/16/3,
raw ObjectData0..78 quando não são getters derivados, Flags256. ObjectId,
container/slot/count são resolvidos pelo World já existente. Direction1,
Lockout25, Parent26, Room29, CurrentValue41, UseCount62 e a extensão79 não recebem
zeros fabricados; dependem de contexto/semântica ainda não ligados. A revisão
identificou Room/CurrentValue/UseCount inicialmente declarados como raw0; isso
foi corrigido e os testes verificam faults para esses seis getters omitidos.

Também não há atributos dinâmicos da referência: GetAttribute fora do Count
retorna0 e SetAttribute expande, enquanto World possui bancos fixos e recusa
índices ausentes. Esse limite é explícito. O objetivo de reprodução fiel ainda
requer resolver esses acessos, os efeitos de SetValue e a pipeline completa,
sem tratar o perfil parcial como inicialização do original.

## Estrutura nativa e execução observada no ODH

TemplatePerson.iff:8568 bytes, SHA256
`3b602335406c772fc9ec2b081daf6e7248d0d07795658ec06be85322bb71ca2e`.
OBJD128/version138 contém Main em+10=`4096`, Init em+86=`4097`, Load em+94=`4098`,
BodyStringID+38=`200`, UsesFnTable+44=`0`. Init4097 chama8192 e depois4100/traits.
Executar8192 ou seu primeiro trecho não completa Init4097.

Com os recursos externos e o perfil acima, as três execuções abaixo produzem
JSON byte idêntico entre Debug e ASan/UBSan:

| Probe | Dispatches / ticks | Fronteira observada |
|---|---|---|
| Main4096 | 6 / 1 | Após primeiro Sleep, global320/pc0 lê scope6/index10: lote atual não fornecido |
| Init4097 | 10 / 0 | Person8192/pc8 lê scope26/index16516 (`0x4084`): tuning global não ligado |
| Main com Sit de DiningChairs oferecido | 6 / 1 | Mesmo fault de lote; queue1, ação não iniciada e completion ausente |

Init executa Person8192 nos PCs `0,3,2,1,6,4,5,7`, escrevendo:

| Campo | Estado após esse trecho |
|---|---:|
| Flags8 | 264 (ChairFacing256 + CanWalk8) |
| Weight27 / SupportStrength28 | 150 / 20 |
| RotationNotches23 | 1 |
| FlagField2/word40 | 32 |
| RoomPlacement30 | 0 |
| Autonomy/person36 | 50 |
| Hunger/motive7 | 50 |
| Age/person58 | 0 (body strings ainda não aplicadas) |

Esses são efeitos de BHAV nativo executado pelo ODH, não efeitos observados no
executável original. Lote/zoning não foram inventados para superar a fronteira.
A oferta de Sit não prova execução de Sit: o Main para antes do AllowPush.

## Próximas dependências identificadas

A leitura de referência e dos recursos localiza tuning necessário a Init:
`0x4084`→GlobalBCON257[4]=1 e `0x4087`→GlobalBCON257[7]=7. A referência divide
Tuning em faixas private/semi/global e consulta o recurso correspondente ao
owner; o resolver do probe anterior da cadeira só fornecia private BCON.
Esse era o limite deste checkpoint inicial. O avanço posterior liga resolver26
por owner com BCON explícitos e ausência diferente de zero; o Init agora alcança
o contexto de lote, conforme [tuning-validation.md](tuning-validation.md).

8325/init motives chama global321, que usa lote atual e GenericTS1Call/28 para
zoning. A área de lote/globals deve vir de contexto explícito. A árvore4100 de
traits precisa RNG8 e PersonDataByTemp30, além de locais/parâmetros. Main/idle
precisa ainda dos serviços e recursos alcançados por suas branches reais.

Não basta atribuir valores de lote, random ou atributos para fazer a execução
passar: cada ampliação deve ter contrato e prova separados. O rastreio anterior
de [Main/idle](person-lifecycle-validation.md) e o contrato de
[fila/retorno](interaction-validation.md) continuam válidos.

## Testes e limites

O teste `person_cli` constrói somente FAR/IFF/BHAV/OBJD/GLOB sintéticos e cobre
private→semiglobal→global, owner1 Main→owner2 ação→owner1 mesma instrução no
tick seguinte, resultado false ignorado pelo Main, Sleep/deadline/sentinela,
queue pendente/AllowPush0, budgets de ticks e dispatches, overrides e rejeições
de scopes/roles/tipo/nome/opções/recursos. Getters derivados desconhecidos falham
com PC/contexto conservados. Dados comerciais não são fixtures versionadas.

CTest Debug e ASan/UBSan:41 testes passam, um X11 é skipped neste sandbox. LSan
desligado (`detect_leaks=0`), sem prova de ausência de vazamentos. O preview,
renderer e o antigo host da cadeira não foram alterados por esta unidade.

Não há lote inicializado, Init completo, original executado, áudio/RNG backend,
avatar idle correto, render conectado a este host ou validação de consoles.
O desenvolvimento continua rumo ao Linux funcional, com essas fronteiras abertas.
