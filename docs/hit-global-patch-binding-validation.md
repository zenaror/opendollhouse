# Binding global de Patch e backend compartilhado

Unidade de 2026-10-17 sobre `c348744`. O controller agora pode obter a Patch
do catálogo global, separada do HOT do Track. `NativeHitPatchRegistry` possui
uma instância de backend por Patch adquirida e geração; dois bindings da
mesma Patch observam esse mesmo backend. A mudança prepara a integração do
manager persistente e não substitui ainda o host BHAV Linux.

A [evidência de resultados de binding](hit-native-binding-outcomes-evidence.md)
fecha a ordem de teardown, os ramos zero/ausente/tipo incorreto e as duas
aplicações de controles. A fonte é a imagem externa identificada por SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`,
sem execução do original. A auditoria independente em
`reports/hit-global-patch-binding/static-audit.json` conferiu os 25 recortes,
4.070 bytes, mapeamento PE e hashes; o reprodutor está ao lado do recibo.

## Controller: fonte e resultado explícitos

O parâmetro final `NativeHitPatchSource` seleciona `local_hot` por padrão ou
`global_catalog` com ponteiro para `NativeHitObjectCatalog`. O perfil local
preserva o preflight anterior e seu callback numérico. O perfil global não
consulta `hot_.patches` como fallback, nem exige que a Patch esteja presente
para construir o snapshot puro do controller. O catálogo e todos os seus
recursos devem permanecer vivos e imutáveis durante os callbacks e execução.

No perfil global, a inicialização libera o binding anterior antes do lookup,
mesmo se o ID novo for igual. `last_binding_outcome` distingue a última
tentativa de binding, separada de `state.binding` e do estado ativo:

| Resultado | Após initializer e setters do caller |
| --- | --- |
| `zero` | ID50=0, ativo, sem binding |
| `missing` | ID50 solicitado conservado, ativo, sem binding |
| `wrong_kind` | Parada diagnóstica interna limpa ID50/pilha; initializer depois restaura entrada, deadline, ID46 e ativo, sem binding |
| `acquired` | Patch vencedora entregue ao callback e binding adquirido |

O ramo de tipo incorreto modela o callback diagnóstico padrão instalado no
original. Não decrementa um objeto Track sem aquisição, anomalia identificada
no binário. Callbacks de diagnóstico alternativos não são emulados. O
initializer nativo ignora o retorno do helper; essa continuação sem wrapper
não equivale ao caminho LoadL5/ID50, que testa o retorno e pode parar a thread.
Escritas públicas em ID50 continuam fora do subconjunto do controller ODH.

O callback `bind_global_patch(ref,snapshot)` adquire a Patch vencedora e
executa a primeira fase de aplicação de controles; retorna o ID26 efetivo da
Patch. O core grava esse valor em seu banco e atualiza flags, então chama
`apply_global_controls(snapshot)` para a segunda fase. As cópias de estado
têm vida limitada à chamada. A primeira fase pode observar ID26 diferente
do valor da Patch; não colapsar as duas em um apply único. Isso expressa a
ordem das solicitações; fórmulas DSP e o corpo do adapter físico continuam
externos e pendentes. A factory do backend usa a fonte da **Patch**, não
overlays do primeiro controller que a adquire.

Bind deve deixar nenhuma aquisição pendente se lançar. Depois do retorno
bem-sucedido, o controller assume o teardown desse binding pelos callbacks.
Falha da segunda fase tenta parar/liberar o binding adquirido e fixa fault;
reentrada é detectada mesmo quando a exceção é engolida pelo callback.
Nenhum callback pode modificar o controller ou os recursos emprestados.

Restart normal conserva frames Call7 em `+0x1a8`, conforme evidência anterior;
as listas de loop limpas pelo initializer em `+0x1b0/+0x1b4` são distintas.
A parada diagnóstica de tipo incorreto limpa a pilha. Timed-NoteOn1 e o campo
nativo `+0x194` continuam fora do subconjunto; a pesquisa não prova que
binding/stop/restart limpem esse campo no original.

## Registry: identidade, referências e limpeza

`NativeHitPatchRegistry` recebe catálogo global imutável, factory externa e
limites. A factory devolve um `unique_ptr<NativeHitPatchBackend>`; ela é
responsável por rollback da construção parcial. O registry tem exatamente
um owner por geração, inclusive quando a implementação nativa expõe aliases
espaciais e comuns. O backend concreto e capturas da factory ficam sob o
contrato de lifetime do caller; o registry deve sobreviver a todos os leases.

`BindingLease` e `ResourceLease` são move-only e fazem release idempotente
sem lançar. `acquire` compartilha uma geração enquanto houver outro binding.
O último release de binding limpa recursos ainda referenciados uma vez,
destrói o backend e aposenta a geração. A próxima aquisição da mesma Patch
cria geração nova. `load_resources` é explícito: carrega na primeira
referência de recurso e limpa na última; não pressupõe uma carga por binding.
Leases de recurso aposentados podem continuar existindo como tokens locais;
seu booleano não comprova que a geração ainda esteja viva.

`NativeHitPatchView` contém owner, slot e geração, sem cache de ponteiro para
backend. Stop tardio de um wrapper aposentado não afeta uma geração nova.
Se restam bindings da geração, a parada do wrapper continua afetando o
backend compartilhado. Isso permite a ordem release da Patch → destruição do
wrapper sem duas deleções ou acesso a backend aposentado. Views de outro
registry são recusadas; operações além de Stop exigem geração corrente.

Pausa chama Stop do backend concreto. Retomada valida a view e não produz
operação de áudio. NoteOn encaminha uma solicitação sem índice de voz; o
literal é ignorado pelo wrapper estudado. `raw_control` conserva o pedido
bruto, sem converter para Q15 ou inferir pan law, resampling ou número de vozes.
O registry não toca o sink PCM global.

Os slots e a fila de releases durante callbacks são pré-alocados. Reentrada
de operação é erro; cleanup de lease durante callback é adiado até seu
retorno. Tokens já aposentados não ocupam essa fila. Essa distinção evita
que tokens de gerações antigas consumam sua capacidade e façam perder um
release vivo. Cleanup é best-effort, registra falha e não repete deleção do
backend. O contrato exige `clear_resources` seguro após load parcial.

Padrões: 64 patches vivas, 256 bindings e 256 referências de recurso;
hard cap ODH de 65.536 por dimensão. Esses limites não foram encontrados no
original. O armazenamento fixo é contabilizado separadamente dos backends,
capturas da factory, recursos decodificados e overhead do alocador. Não é
medida de RSS nem orçamento global final para New 3DS.
No host x86-64 desta validação: view24 bytes, registry184 bytes e armazenamento
fixo padrão21.176 bytes, somando objeto, 64 slots de72 e512 pendências de32.
Esses tamanhos não são transferidos para os ports sem medir seu ABI.

## Validação e limites

Os testes do controller cobrem Patch global de outra origem, zero, ausência,
tipo divergente mascarando Patch local, duas fases de ID26, restart com
frames e callbacks que falham ou reentram. Os do registry cobrem duas
aquisições compartilhadas, recursos separados, gerações, teardown tardio,
limites, falhas de factory/load/clear e reentrada. A regressão de fila usa
capacidades `{1,1,1}`, acumula tokens de recurso aposentados e libera o
binding vivo durante callback, verificando contagens zeradas e destruição.

`hit_global_patch_binding` integra banco proprietário, resolução
EventMapping A → Track B → HIT C → Patch D e dois controllers. Mostra que a
pausa do segundo binding pode parar a reprodução observada pelo primeiro;
retomada não toca áudio; último release limpa recursos uma vez; rebind do
mesmo ID pode criar geração nova. Falha injetada após acquire usa lease
temporária e não deixa binding pendente antes de o controller fixar fault.
O driver é sintético; suas contagens não são saída física ou vozes nativas.

Logs, snapshots de fonte e probes desta unidade ficam em
`reports/hit-global-patch-binding/`, ignorado pelo Git. O controller passou
154 verificações; registry55; integração29, esta também compilada com
`-Werror -Wconversion -Wsign-conversion -pedantic` em C++17. Houve revisão
independente do snapshot final, incluindo as duas correções de cleanup.

CTest completo Debug: 120 configurados,119 aprovados,1 skip X11,0 falhas
(15,42 s). ASan/UBSan: mesmos resultados (25,87 s), com LSan desabilitado
por `ASAN_OPTIONS=detect_leaks=0`. Sem SDL/X11, os três targets e testes
selecionados de controller/registry/integração passaram (0,02 s); não é
uma rodada completa nessa configuração.


Quatro probes Classic/Legacy × Debug/ASan+UBSan processaram 27 triplas e81
arquivos por caso, após fechar os streams do banco. Das4.494 mappings,178
não são tipo1,38 não resolvem e110 não têm metadata exigida pelo perfil.
Dos4.168 candidatos admitidos,3.744 têm Patch zero,1 ausente e423 adquirida;
nenhum tipo incorreto ou Patch de outro HOT ocorreu neste corpus. Esses
casos são cobertos pelos testes sintéticos. Os423 bindings adquiridos
passaram pelas duas fases de controles, flags e pausa/retomada, sem falhas
de cleanup ou recursos vivos ao final de cada caso. O callback ID26=0 é
política explícita do driver sintético, não valor extraído do backend original.
Birthday conserva ID26=7 quando Patch é zero e chega à falha esperada69 em
PC111, após Call7 para110, sem NoteOn. Logs e receipts, incluindo fontes,
binários e81 hashes de entrada, ficam em `corpus/`; `validation.json`
registra o snapshot final. A ordem lexical do probe é diagnóstica.

Ainda faltam o adapter de áudio concreto, manager persistente por TrackID,
admissão, contribuições/generation World, cadência de Clock e integração do
host. Opcode69 continua recusado e o cenário de aniversário não é concluído.
Não há alegação de compatibilidade integral, áudio físico ou execução nos ports.

A continuação [controles antes do wrapper](hit-native-audio-controls-validation.md)
adiciona pedidos tipados e torna obrigatório o snapshot de escrita19/20 no
perfil global, preservando callbacks numéricos do perfil local. Os resultados
acima continuam sendo o checkpoint histórico desta unidade de binding.
