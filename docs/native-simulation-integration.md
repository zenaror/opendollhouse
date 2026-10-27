# Fila de simulação compartilhada

`native/simulation` implementa o agendamento derivado do FreeSO fixado em
`7e76c1960252ddd033d8937d5edb47d326f5b69c`. O verdadeiro VMScheduler chama essa
biblioteca por `managed/OpenDollhouse.Simulation`; não há fila gerenciada alternativa.
O Simitone ainda executa as entidades, efeitos gráficos, remoções e aleatoriedade.
A extração substitui um componente utilizado, sem iniciar um segundo motor.

## Identidade e comportamento

Tokens de 64 bits identificam referências, independentemente do ObjectID assinado
mutável. Cada ocorrência mantém a entidade viva. A busca binária consulta IDs
atuais, não reordena listas antigas e conserva as peculiaridades do algoritmo,
inclusive ocorrências repetidas após mudanças de ID. Remoção usa identidade de
referência; não há promessa de equivalência para tipos que redefinem Equals.

O laço consulta a lista atual a cada passo. Uma ação pode inserir na lista ou uma
execução aninhada pode substituí-la. Reset e conclusão retiram agendamentos, mas
conservam a referência atual até outra consulta. O tick removido ao terminar é
o tick atual, que também pode ter mudado. O wrapper conserva comparação unsigned,
overflow e migração do primeiro tick. A carga de save original faz Reset, recria
entidades e agenda tick1 antes de retomar o relógio; nenhum formato novo foi criado.

ABI 1 tem 14 exports C com tipos de largura fixa, ponteiro opaco e erros explícitos.
Nenhum objeto CLR/STL passa pela fronteira. Callbacks não propagam exceções.
SafeHandle conserva callbacks e registro até a destruição, sem GCHandle global
que mantenha a VM viva para sempre. Operações serializadas podem mudar de tarefa
ou thread; concorrência e reentrada pelos callbacks são recusadas. Isso preserva
VM.LoadAsync, além do carregamento síncrono observado pela interface.

## Recursos e limites

O adaptador desktop declara tetos de 65.536 listas vivas, 1.048.576 ocorrências e
64 MiB de alocações nativas contabilizadas. Lista destacada ainda conta no limite.
As estatísticas distinguem agendamentos, listas, ocorrências, tokens e bytes atuais
ou máximos observados. Falha de capacidade é explícita, sem eliminar ações.
Preparar interrupção e reagendar são duas operações, como no adaptador anterior;
uma falha na segunda não promete rollback da primeira.

Bytes nativos cobrem estrutura da fila, nós, controle de listas e vetores. Não
incluem metadados do alocador, pilha, registro C#, recursos das entidades ou gráficos.
O registro gerenciado mantém no máximo ocorrências vivas mais um candidato em
registro; suas tabelas podem manter capacidade até a fila ser liberada. Esses tetos
são proteção desktop, não orçamento aprovado para consoles nem para o jogo inteiro.

Com ODH_VM_DIAGNOSTICS=1, o scheduler registra a primeira execução não vazia e
a cada 256 quadros concluídos. O diagnóstico contém uso nativo e registro gerenciado;
falhas de escrita do log não podem interromper a simulação. Não contar chamadas
nativas a partir desses registros periódicos.

## Reprodução

A receita conjunta está em [native-data-integration.md](native-data-integration.md).
A propriedade absoluta ODHNativeSimulationLibrary deve apontar para o nome sem
versão: libodh_simulation.so, libodh_simulation.dylib ou odh_simulation.dll.
O build copia a biblioteca transitivamente; ausência não ativa fallback. Cada
plataforma precisa de sua própria biblioteca compilada. As fontes managed/native
do checkout acompanham os patches; aplicar apenas o patch FreeSO não basta.

Para os testes diferenciais:

```sh
DOTNET_ROLL_FORWARD=Major dotnet run \
  --project managed/OpenDollhouse.Simulation.Tests/OpenDollhouse.Simulation.Tests.csproj \
  -c Release \
  -p:ODHNativeSimulationLibrary="$(pwd)/build/native-simulation/libodh_simulation.so"
```

## Evidência e alcance

- Host nativo: CTest 2/2, 82 verificações de fila, consumo do header em C e 14 exports.
  ASan/UBSan com detecção de vazamentos passou nas duas entradas.
- Ponte real: 19 cenários diferenciais contra o scheduler original e 13 grupos de
  recursos/ownership, incluindo IDs mutáveis, referências repetidas, carga em
  worker, erros, 128 destruições explícitas e coleta de 64 ciclos de VMs.
  Biblioteca ausente falha. Relatório em
  registro arquivado não distribuído.
- ARM emulado: mesma implementação, fixture com avanço da fila, execução aninhada,
  reset/reconstrução do agendamento, identidade e capacidade. PASS SVC exato e
  captura; 147 configurações iguais e instância/perfil/Xvfb encerrados.
  registro arquivado não distribuído.
- Candidato Linux: registro arquivado não distribuído,
  build e ciclo comprar/sentar/salvar/reabrir em dois processos passaram. Bob
  sentado, objeto139 e saldo585 conservados; seis fases de save/Load, zero erros
  e seis hashes de originais iguais. Ambos os processos carregam e executam a
  mesma biblioteca testada no host, com fontes ligadas ao ARM. Picos observados
  da fila de 9.680/9.440 bytes nativos e 479/458 ocorrências; não memória total.
  Processos, cópia, perfil e Xvfb encerrados; resultado completo e resumos separados.

O teste ARM não carrega uma família comercial nem executa BHAVs, CLR, gráficos ou
áudio do jogo. O avanço e reconstrução são sintéticos; não há save real de família
nem hardware 3DS validado. A biblioteca compartilha comportamento real com o host,
mas o resto do motor e os ports completos continuam pendentes. Relatórios do host,
ARM e jogo são gates separados; nenhum substitui os demais.
