# Banco proprietário de recursos HIT/HOT/HSM

Unidade de 2026-10-17 sobre `6c6ad6a`. `HitResourceBank` é um banco portátil
que possui os dados HIT e metadados HOT, acumula o namespace HSM e constrói
um `HitEntryCatalog` com ponteiros estáveis. A motivação é permitir chamadas
entre buffers sem conservar abertos os streams usados na leitura e sem
resolver um HOT usando símbolos de arquivos ainda não processados.

## Contrato e limites

O caller fornece uma sequência explícita de `HitResourceGroupInput`, cada
qual com Views HSM, HIT e HOT. Cada View pode referenciar um trecho de stream;
o banco não abre arquivos, EXE, ISO ou CD. As Views e streams só precisam
viver durante a construção. Os grupos são processados como HSM → HIT → HOT;
o namespace HSM cumulativo substitui a última definição do mesmo nome exato.
`Foo` e `foo` são nomes diferentes, e `@123` exige um símbolo chamado `123`.
O perfil nativo não usa fallback numérico depois de `@`. A HOT guarda o
valor resolvido naquele instante, sem ser alterada por HSM posterior.
Isso deriva da [investigação do namespace original](hit-native-hsm-namespace-evidence.md).
Os perfis padrão dos leitores mantêm o comportamento Simitone-local anterior.

O banco não pode ser copiado ou movido. Sua vida deve cobrir todo uso dos
HIT/HOT, catálogo, resultados e controllers que os referenciem. O construtor
reserva os grupos antes de preenchê-los e só monta o catálogo depois de
estabilizar os endereços. Uma falha destrói o estado parcial por RAII.
`symbols().first_lines` conserva a linha local da primeira definição de cada
nome; não representa uma linha global nem identifica sozinho o grupo.

Os limites padrão do banco são 16 MiB de extensões HSM+HIT+HOT, 65.536
registros HSM incluindo duplicatas, 32.768 nomes únicos e 1 MiB de bytes
de nomes únicos. Somam-se os limites por arquivo e os do catálogo: 64 grupos,
65.536 linhas TrackData, 16 MiB HIT+HOT, 4.096 recibos de colisão e 256 bytes
por nome de evento. O preflight rejeita extensões e somas acima do limite
antes da leitura/reserva. Limites semânticos são verificados durante o parse
e merge. Esses contadores não medem RSS, overhead dos mapas ou toda a memória
temporária; não constituem orçamento global de áudio para os ports.

O banco conserva a última definição processada de TrackData com sua identidade
HIT. A [descoberta original](hit-native-load-order-evidence.md) usa a ordem de
chamada das raízes e a ordem de enumeração Win32 dentro de cada raiz. O banco
não implementa descoberta automática. Uma ordem lexical escolhida num probe
Linux é política diagnóstica e pode escolher outro vencedor para duplicatas.

## Verificação

O teste sintético `hit_resource_bank` verifica contagem, grafia exata,
substituições, snapshots HOT, catálogo e Call7/Return8 entre recursos depois
da destruição dos streams. Cobre limites por arquivo e agregados, índices
inválidos e overflow de extensões declaradas sem ler/alocar esses conteúdos.
O core compilado com `-Werror -Wconversion -Wsign-conversion -pedantic`
passou 16 verificações. O leitor HSM/HOT passou 106 verificações focais;
houve revisão independente sem defeito concreto nos invariantes pedidos.

CTest completo Debug: 117 configurados, 116 aprovados, um skip X11 e zero
falhas (13,99 s). ASan/UBSan: os mesmos resultados (27,31 s), com
`ASAN_OPTIONS=detect_leaks=0`. Esses resultados não validam LSan nem saída
física de áudio. Logs e configurações ficam em `full-validation/`.
Com SDL/X11 desabilitados, os quatro testes de recursos, catálogo, banco e
controller passaram (0,02 s). A compilação global dessa configuração foi
interrompida para limitar a rodada aos quatro targets pertinentes; o log
intermediário foi preservado. Não há alegação de suíte completa sem SDL/X11.

Os recibos desta unidade ficam em `reports/hit-resource-bank/`, ignorado pelo
Git. `audit_static.py` rederiva o mapeamento PE da imagem SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2` e
confere os três manifestos estáticos desta unidade: 75/75 intervalos,
31/31 hashes de recortes declarados e 6.726 bytes de instruções do backend,
sem inconsistências. O recibo é `root-static-audit.json`; nenhum executável
comercial foi executado.

O probe C++ de corpus usa o TSV explícito do levantamento anterior e fecha
todos os inputs antes de consultar o catálogo ou executar o controller.
Os quatro probes passaram: Complete Collection e Legacy, em Debug e
ASan/UBSan (`detect_leaks=0`), com recibos idênticos. Os hashes dos 81 arquivos
HIT/HOT/HSM foram novamente conferidos iguais entre as duas edições; isso
não transfere endereços ou comportamento entre seus executáveis.
O banco contém 27 grupos, 2.855.204 bytes de fontes, 13.207 registros HSM,
8.431 nomes exatos, 4.776 substituições e 164.314 bytes de nomes únicos.
Os HSM somam 399.395 bytes; HIT+HOT somam 2.455.809 bytes. TrackData conserva
4.363 linhas, 4.264 chaves e 99 colisões entre grupos.

O evento `vox_blowout_candles` resolve chave1340/PC16 e Call7 chave1347/PC110.
O controller registra quatro instruções concluídas e para com **falha esperada
no opcode69 ainda não implementado**, sem efeitos de áudio. Isso valida o
banco e a fronteira de execução; não é sucesso do evento completo nem prova
de reprodução do som. `bank-probe.cpp` e `run.py` reproduzem o cenário.
`validation.json` registra identidades dos inputs, binários e recibos;
`source-snapshot.json` conserva hashes dos sete arquivos de código/build
alterados nesta unidade, separados das fontes comerciais.

## Pendências

Não há novo adapter PCM, manager persistente, admissão/provider ou integração
World nesta unidade. Opcode69 continua sem implementação no controller.
A [pesquisa do backend](hit-native-engine-backend-evidence.md) demonstrou
parada na pausa e retomada sem reinício de áudio, com possibilidade de backend
compartilhado por wrappers da mesma patch. Falta fechar ownership, callbacks,
streaming e controles antes de integrar esse contrato ao host. O trabalho não
comprova compatibilidade integral, áudio físico ou execução nos ports.
