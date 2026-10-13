# Suspensão da VM para serviços — 2026-10-13

Extensão posterior: completion agora admite output validado em argumentos/locais,
e `run_generation()` identifica reinícios. O contrato e os74 checks atuais de
serviço estão em [playback-validation.md](playback-validation.md); abaixo está
o checkpoint inicial do mecanismo de suspensão.

O kernel agora permite que uma primitiva externa devolva true, false,
unsupported ou wait. Isso prepara a ligação ao roteamento/animação, mas não
implementa essas primitivas, um scheduler ou passagem de tempo do jogo.

`wait` conserva o PC, frames, argumentos, locais e temps; gera um ticket por
instância de VM e registra instrução/contexto em `pending()`. `run()` durante
a espera retorna `waiting` sem repetir o handler nem consumir instruções.
O controlador consulta o pedido pendente e agenda o trabalho real.

`complete(ticket,result)` aceita uma conclusão correspondente uma única vez,
segue a aresta true/false da instrução e pode retornar ao pai. Uma próxima
chamada `run()` executa a instrução seguinte. A conclusão não conta como uma
nova instrução BHAV e não chama novamente o serviço. Faults de retorno, como
duas arestas FD, continuam explícitos.

Tickets não reiniciam em `start()`. Reiniciar invalida o pedido pendente;
respostas antigas/duplicadas não mudam a nova execução. Tickets são locais à
instância, não IDs globais. VM tem endereço estável e não pode ser copiada ou
movida. O backend deve cancelar seu próprio trabalho e conferir o pedido antes
de aplicar mudanças ao mundo; invalidar um ticket não reverte efeitos externos.
O callback agora recebe `VmDispatch` com identidade da VM e ticket reservado
antes de despachar; isso permite associar um job à origem sem aceitar pedidos
de outra VM ou concluir um trabalho antigo.
Callbacks não podem reentrar em start/run/complete. O contrato é de um thread;
não há sincronização para concluir pedidos de outro thread diretamente.

Um handler ausente ou unsupported continua produzindo fault. A primitiva 2
permanece interna e opcodes >=256 continuam sendo chamadas BHAV. O novo handler
não transforma serviços desconhecidos em sucesso. O probe comercial da cadeira
continua parando em 45 enquanto não houver um serviço de roteamento implementado.

CTest 10/10 passou em Debug e ASan/UBSan. A suíte services tem 23 checks
sintéticos: espera em rotina filha, preservação de contexto e temps, retomada,
retorno false, tickets antigos/duplicados, reinício, unsupported, FD e reentrada.
Os callbacks do teste são sintéticos; não validam percurso ou ticks TS1.
LSan continua não validado (`detect_leaks=0`, limitação de ptrace).

Próximo avanço: posição/footprints e geração de destinos a partir do SLOT
identificado, percurso e controlador de ticks, ligados a esse contrato.
Sprites, renderização Linux e cenário completo continuam pendentes.
