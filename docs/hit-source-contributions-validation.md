# Tabela portátil de contribuições HIT por TrackID

O componente `odh_hit_source_contributions` fornece armazenamento explícito
de pares raw `(TrackID, R1)` e agregação pura de controles já resolvidos. Ele
é uma fundação de biblioteca: ainda não está conectado a um controller
persistente, ao World ou ao caminho de áudio. A tabela e o agregador não
implementam grupos, provider espacial, fade, Clock, ownership PCM ou ports.

## Tabela de pares

`HitSourceContributions` recebe capacidade obrigatória entre 1 e 65.536 pares.
O limite superior é de alocação do ODH, não um limite atribuído ao jogo. O
construtor reserva essa capacidade; cópia e movimento são desativados para que
a capacidade reservada não seja perdida. `add` compara ambos os valores raw:
uma duplicata retorna `already_present`, um par novo é inserido e, se a tabela
estiver cheia, um par novo retorna `capacity_refused`. A duplicata continua
idempotente mesmo quando cheia. Após a construção, as operações normais de
consulta/mutação, inclusive a recusa por capacidade, não alocam. A construção
de exceções por entrada inválida fica fora dessa garantia. A tabela exige um
único proprietário de execução; não oferece sincronização entre threads.

Há consultas por par e contagem por TrackID. `copy_track` primeiro confere o
buffer de saída e só então escreve, preservando a ordem determinística de
inserção do ODH. Buffer curto ou nulo para resultado não vazio lança
`FormatError` sem escritas parciais; resultado vazio aceita ponteiro nulo com
capacidade zero. `erase` remove o par exato; `clear_track` remove os pares de
uma chave e retorna quantos foram removidos. `remove_one_source` remove só a
primeira entrada daquele R1 na ordem de inserção e devolve o TrackID afetado.
O orquestrador deve repetir a operação explicitamente para os demais tracks e
pode aplicar efeitos entre chamadas. Essa ordem pertence à tabela ODH, não
reproduz buckets/encadeamento do hash nativo.

O armazenamento aceita os domínios completos `uint32_t` e `int32_t`, inclusive
zero e valores extremos; não valida IDs de World nem sinal de R1. Também não
faz chamadas de callback, efeitos, lookup de fonte ou vínculo de geração.

## Agregação de amostras raw

`aggregate_hit_source_controls` recebe `(external_volume, pan)` completos e já
resolvidos, ambos `int32_t`. Não consulta providers nem tenta preencher uma
amostra ausente. Para cada entrada, calcula volume como o máximo assinado com
valor inicial zero; soma os volumes e os produtos `(pan - 512) * volume` com
wrap de palavra de 32 bits. A divisão interpreta numerador e denominador como
assinados, trunca em direção a zero, e o resultado de pan é `quociente + 512`
com wrap de 32 bits. Não há clamp ou conversão DSP. A contagem é limitada a
65.536; uma contagem acima desse teto ou ponteiro nulo para entrada não vazia
lança `FormatError`.

Com zero amostras, o resultado é volume 0/pan 512. Se a soma de volumes der
zero após wrap, o volume máximo ainda é retornado e pan é 512. A divisão
`INT32_MIN / -1` lança `FormatError` para evitar overflow indefinido em C++ e
trap; não se inventa um resultado para esse caso. Valores negativos participam
das somas de pan, mas não reduzem o máximo abaixo de zero. O chamador que não
consegue resolver uma fonte positiva deve decidir como recusar antes de chamar
o agregador: resíduos de provider nativo não são um fallback que esta função
possa reproduzir. Fórmula e limites da resolução estão em
[evidência estática das contribuições](hit-native-source-contributions-evidence.md).

O ajuste de volume feito pelo jogo depois da agregação, a geometria do provider,
os efeitos após remover uma contribuição e os lifecycles do controller ficam
fora desta API.

## Validação e limites

Testes diretos C++17 com warnings-as-errors e ASan/UBSan passaram 529 checks.
LSan ficou desativado sob ptrace. No CTest oficial, Debug configurou 114 testes:
113 passaram, um teste X11 foi pulado e não houve falhas (13,01 s). ASan/UBSan
teve os mesmos resultados (25,28 s), com LSan desativado. A configuração sem
SDL/X11 passou 8/8 testes (0,62 s). Logs de configure/build/CTest estão em
registro arquivado não distribuído.
O teste desta API chama-se `hit_source_contributions`.

Um oráculo independente Python de precisão arbitrária conferiu os bits signed
de 32 bits, os wraps e a divisão truncada. Debug e ASan/UBSan coincidiram em
20.263 casos cada; cada execução também confirmou o único trap esperado
`INT32_MIN / -1` e o limite de 65.536 amostras. Seed `0x57488f`; recibo em
registro arquivado não distribuído, script e driver
em registro arquivado não distribuído e
registro arquivado não distribuído.

Auditoria estática independente confirmou 1.367 linhas e 3.684 bytes de
instruções selecionadas contra a imagem identificada em
registro arquivado não distribuído. O original não
foi executado. A evidência e a aritmética nativa correspondentes estão em
[hit-native-source-contributions-evidence.md](hit-native-source-contributions-evidence.md).
A revisão independente do core/API, do preflight de cópia e da evidência
estática está concluída, sem ressalvas materiais após a correção de copy/move.

Esses checks exercitam a API portátil e a aritmética sobre amostras completas.
Não verificam execução do jogo original, integração com o áudio do World,
semântica de grupos, compatibilidade com The Sims 1 ou funcionamento de
New 3DS, Vita ou Android.
Não foi feito novo probe de áudio da cadeira: esta API ainda não está ligada
ao host de áudio/World, então tal probe não exercitaria o componente.
