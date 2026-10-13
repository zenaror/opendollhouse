# Entidades, slots e cadeira — 2026-10-13

`odh_world` introduz estado de entidades compartilhado com a VM. É um avanço
para a interação; faltam lote, scheduler, roteamento, animação e renderização.

## Estado e invariantes

IDs positivos int16 identificam objetos e avatares. Layouts de bancos são
explícitos na construção, com valores iniciais zero. Scopes 0/1 acessam
atributos, 3/4 palavras do objeto, 16/20 slots e 18/19 person data, escolhendo
caller ou stack object conforme a referência. Callee permanece no contexto,
sem ser confundido com essas identidades.

Palavras de objeto são declaradas explicitamente e lidas/escritas como estado
bruto. Isso não implementa efeitos associados a propriedades como direção,
gráfico ou movimento. Ausência de uma declaração retorna unsupported, não zero.
ObjectId (11), ContainerId (2), SlotNumber (3) e SlotCount (67) são derivados
e recusam escritas diretas. Sem recipiente, ContainerId=0 e SlotNumber=-1,
conforme o perfil FreeSO; comparação com o original permanece pendente.

Um slot existente e vazio retorna 0. Slot inexistente, entidade ausente e person
data em um objeto não avatar retornam unsupported. Attach/detach mantém ambos
os lados da relação, rejeita slot ocupado, vínculo duplicado, autorreferência
e ciclos. Remover entidade libera seus vínculos. Attach não muda postura
automaticamente; postura e relações são etapas distintas de uma interação.

Os slots aqui são armazenamento de ocupação. Não são os registros geométricos
de roteamento de um chunk SLOT. OBJD/SLOT, posição, footprint, multi-tile,
motivos e inicialização real ainda precisam ser integrados.

Extensão posterior: bancos de motivos14/15 e definição21 explícitos, orçamento
e clipping estão em [main-validation.md](main-validation.md). A inicialização
real e o decaimento de motivos continuam pendentes.

Limites ODH configuráveis: 4.096 entidades, 65.536 palavras no total, 4.096
palavras por banco. A faixa de índices int16 também limita os bancos. São
limites de proteção; não são RAM medida nem limites atribuídos ao TS1/3DS.
World deve sobreviver às VMs ligadas aos callbacks; seu endereço é estável.
O modelo não é thread-safe.

## Checks

CTest 7/7 passou em Debug e ASan/UBSan. A suíte world possui 56 checks
sintéticos de identidade/scopes, bancos, estado vazio/ausente, ocupação,
remoção, ciclos, limites e ligação à VM. A integração CLI cobre estados
construídos e rejeição de opções inválidas. LSan permanece não validado
(`detect_leaks=0`, limitação de ptrace do sandbox).

## BHAV real com estado construído

A entrada `DiningChairs.iff` em `referencias/The Sims/GameData/Objects/Objects.far`
teve SHA-256 revalidado:
`cffff7b4e8b5d1b31351c32a2f8656f50dbf72a82217bf38b4828853fc6f90c7`.
O probe usa caller 1, cadeira 2, outro avatar 3, um slot na cadeira e bancos
person data de 88 palavras nos avatares. Esse layout é do experimento ODH,
não evidência da inicialização de entidades no original.

Teste BHAV `0x1002`, resultados ODH observados:

| Estado construído | Retorno | Instruções |
|---|---|---:|
| Vazia, manual, postura 0 | true | 3 |
| Vazia, autônomo, postura 0 | true | 4 |
| Vazia, autônomo, postura 1 | false | 4 |
| Ocupada pelo avatar 3 | false | 2 |
| Ocupada pelo caller, postura 1 | true | 1 |

A ação BHAV `0x1001`, cadeira vazia, agora chega ao PC 0 de `sit core`
(`0x1003`) após quatro instruções e três frames. Para com
`unsupported primitive 45`. Na referência fixa, 45 é `goto_routing_slot`,
44 é `animate` e 46 é `snap`. O operando observado é data=1, scope=2
(Global), flags=0. `VMMemory.GetSlot` da referência busca SLOT ID100 no Global
e a segunda entrada de tipo3 (índice1). Isso orienta a próxima investigação;
não comprova o comportamento do original.

Debug e ASan/UBSan produziram JSON idêntico para os cinco casos e a ação,
stderr vazio. Reports locais ignorados: `reports/chair-world-matrix.json`,
`reports/chair-world-matrix-sanitize.json`, `reports/chair-world-action.json`
e `reports/chair-world-action-sanitize.json`. Não houve execução de Sims.exe,
sentar/levantar completo, janela ou teste de hardware.

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1002 --run --world-probe
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1002 --run --world-probe --autonomous --posture 1
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1002 --run --world-probe --occupant 3
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run --world-probe
```

## Caminhos após mudança do usuário

A instalação foi movida para `../referencias/The Sims`, fora do checkout Git,
e os exemplos foram atualizados. O manifesto completo das classes de input
do inspector foi recalculado e comparado byte a byte com o anterior:
1.089 arquivos, 2.710.890.759 bytes, SHA do manifesto
`174db55df3257fca3107a5269faabd143fc3d2b9025164fe9076175bfa2ff667`.
`Sims.exe.bak` também conservou SHA
`32857c25e00594a9bc5d700f7b373b6df59ab76ec51b36aa1d30f3dd973eff58`.
O manifesto não cobre exe, ISO ou todas as classes da instalação.
