# Continuidade atual

Retomada autorizada da meta integral OpenDollhouse. OMM não está disponível
nesta sessão; este checkpoint precisa ser sincronizado com ela quando o
conector voltar. Não declarar OMM atualizada. Ler AGENTS.md, este arquivo,
docs/architecture.md e as evidências recentes antes de continuar.

Base anterior: effe3eb9420df0a10b43bbe4e8e4c18ee84eba58 (factory HOT/HIT).
Novo checkpoint: catálogo XaPatchCatalog, CLI odh-xa-patch, tests e evidência
em xa-patch-catalog-validation.md; identificar hash com git log. Não houve
novas tarefas para OpenCode; o usuário informou sua remoção e descartou IA local.

Catálogo recebe fontes FAR/loose explícitas; basename ASCII/slashes,
ambiguidades sem precedência adivinhada, limites globais de índice/nomes,
decoder bounded por entrada e proveniência. Sem extração ou cache de PCM.
Paths ficam absolutos; detectar tamanho diferente não é checksum de conteúdo.
PCM retornado owned; ainda falta orçamento agregado/mixer.

Testes finais:104 configurados,103pass/1X11skip em Debug e ASan/UBSan,
LSan off.31checks C++ + wrapper CLI.12patches nativas com encodedSHA/PCM
metadata iguais às referências; factory->catálogo->FAR->XA->SDLdummy compara
54296 samples de3212, conclui instância e stoponce. Argumentos/Jump/choices
fixtures explícitos, World construído; não audibilidade/original/Main/hardware.
Logs reports/xa-patch-build-final-ctest.log e
reports/xa-patch-build-asan-final-ctest.log; detalhes nas fontes de evidência.

Próximo: mixer/backend com memória PCM agregada e lifetime por nota; integrar
serviço de som ao loopWorld real sem bypass. Depois callerargs/ArgStyle/units,
controllers, loops/CallReturn/owners/dedup/globalgroup69 e campos4/13/14.
Savedframes105/137/queues/clock ainda dormentes, sem reInit; câmera/picking,
input/lote mínimo, SLOT8/failuretrees/needs/inventory/save, SDK/CC e ports
continuam na meta integral. Linux primeiro, Simitone/submoduleTS1 principal.

Originais externos e refs/reports/builds preservados. Artefatos de build na
raiz e antigos mixer/runners não versionados continuam sem integração;
não git add -A, não cleanup genérico. Checkout/remote devem ser revalidados.

Quando OMM voltar: get_handoff no scope OpenDollhouse, mesclar novo checkpoint
com fontes existentes preservando questões/história e usar expected_sha256
atual. Histórico anterior está na OMM e no handoff datado de2026-10-14,
que é um snapshot anterior e não substitui este estado mais recente.
