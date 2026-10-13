# Snap, binding e ocupação — 2026-10-13

`odh_snap` implementa um perfil inicial do snap46 Literal para ocupar um
recipiente. Ele permite avançar o BHAV real da cadeira até a animação44,
ainda ausente. Não comprova sentar visualmente ou compatibilidade completa.

O objeto é ligado por OBJD selecionado explicitamente: prefixo de 42bytes
retorna versão/GUID/AnimationTableID/SlotID, nas versões136/138/139/140/141/142.
O corpo restante é opaco, sem validação completa. O loader confirma um OBJD
e um SLOT de ID correspondente; duplicatas ou ausência falham antes de executar.
Binding inclui geração da entidade e identidade da VM, evitando reutilizar
outro objeto com o mesmo ID.

Perfil: Literal Mode3, flags4 e tailzero, SLOTv10 tipo3 com offsetzero,
flags/scoreszero e SnapTargetSlot>=0. O slot de armazenamento tipo0 precisa
de offsetzero e height7. Outros modos/geometrias/alturas dão unsupported.
O flag FootprintExtension é reconhecido no operando, mas não cria footprints
ou a política espacial do original. Falhas de ocupação retornam false.

`World::place_in_slot` mantém posição física e vínculo juntos. Destino ocupado,
índice inválido, ciclo e vínculo a outro recipiente falham sem alterar posição
ou relações. Repetir no mesmo slot é idempotente. Direção do ocupante é
preservada. Contenção posicionada propaga x/y/nível quando o recipiente muda,
inclusive aninhada, por lista iterativa; alocação ocorre antes da mudança.
Detach deixa a última posição física. Transferência entre recipientes e
geometrias não zero ainda não estão implementadas neste perfil.

Height7 é classificação de altura do SLOT, disponível nos dados. Não somar
arbitrariamente sete unidades à posição física. Offset/altura visual,
skeleton/personagem, multi-tile, colisões e política completa de snap ainda
faltam. A referência FreeSO foi consultada; original executável não foi testado.

## Validação

CTest13/13 passou em Debug e ASan/UBSan. A suíte snap/prefix tem59checks
sintéticos: offsets do prefixo, truncamentos, ocupação/idempotência, recusa
atômica, geometria/modeunsupported, IDreutilizado, binding e propagação aninhada.
A CLI integra OBJD/SLOT/BHAV sintéticos. LSan continua não validado por ptrace.

Dados fornecidos: DiningChairs.iff, OBJD16808, versão138, GUID0x26BFBB29,
SlotID128 e AnimationTableID129. SLOT128/type3/index0 (registro absoluto1)
aponta SnapTargetSlot0; armazenamento type0/index0 tem offsetzero e height7.
Identidades dos recursos estão nos checkpoints anteriores.

Probe construído com BHAV1001 real: roteamento33ticks já validado; snap muda
a posição física de(48,64) para(64,64), liga caller1 ao recipiente2/slot0 e
preserva direção2. O BHAV escreve postura1 e avança até opcode44 em1003PC8.
São18instruções totais, três frames, saída3 e diagnóstico esperado
`unsupported primitive 44`. O teste permanece em cena ODH construída, sem
animação/janela, save original, execução do Sims.exe.bak ou hardware.

JSON Debug e sanitizadores idênticos, stderr vazio. Reports ignorados:
`reports/chair-snap-probe.json` e `reports/chair-snap-probe-sanitize.json`.

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run --world-probe --route-probe --snap-probe --object-def 16808 --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff
```

Próximo: resolver tabela de animações129 e os recursos do personagem usados
pela primitiva44; executar animação/retomada e depois composição Linux.
Fidelidade de tempos/rotas, sistemas e persistência continuam pendentes.
