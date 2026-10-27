# Resolução dos argumentos de caller/evento

O runtime pode derivar a tripla bruta de PlaySound a partir da origem selecionada
e dos overrides HOT, sem exigir valores de fixture. Isso implementa a camada
demonstrada em [hit-native-caller-arguments.md](hit-native-caller-arguments.md),
não ArgStyle, controles espaciais ou fidelidade completa da inicialização HIT.

## Recursos e resolução

HotEvent preserva três argument_overrides signed int32 opcionais, correspondentes
a EventMapping campos CSV2/3/4 após `=`. Campos finais omitidos são zero
conhecido; zero explícito também é conhecido. Literal decimal e equate com sinal
usam o mesmo domínio int32, sem conversão intermediária unsigned. Campo
explicitamente vazio, símbolo não resolvido ou número fora do domínio fica
nullopt e registra unsupported_reason; não é convertido em zero silencioso.
O sexto campo continua sem interpretação. O perfil estrito não afirma reproduzir
todas as entradas toleradas pelo parser do binário.

Contagens de registros, limites de texto/linha, overwrite e unsupported_records
continuam valendo. Overwrite substitui a linha inteira e é contado, inclusive
quando uma linha válida substitui outra não resolvida. A estrutura adiciona
três slots fixos por evento; logical_bytes continua medindo input serializado,
não RSS ou footprint de std::optional/metadata/alocadores.

resolve_hit_arguments(event,caller) aplica cada override não zero somente à
sua posição; zero conserva caller. Não modifica event/caller e rejeita qualquer
override ausente ou motivo de unsupported. O helper puro não seleciona tipo de
evento/track. play_sound_arguments(event,source) fornece caller
`[int32(source_int16),0,0]`. Source já deve ser a entidade selecionada/resolvida;
o helper não implementa sentinela de cache -2 nem a API geral do wrapper nativo.

## Uso no probe

--audio-caller-args substitui --audio-args R1 R2 R3 como opção mutuamente
exclusiva. O modo continua opt-in: Jump, chooser e controles DSP precisam ser
fornecidos explicitamente, pois a camada espacial/ArgStyle não está concluída.
O perfil JSON informa argument_mode=play_sound_caller e args=null; cada
sound_request registra hit_arguments com a tripla preparada naquela submissão.
Isso não é prova de execução de NoteOn, que continua em tick posterior.

A coleção valida binding/geração antes de construir a factory. O serviço
valida o nome/perfil com resolve_hit_event antes da cópia limitada e lookup
ASCII-casefold no HOT. Então usa request.source na resolução dos argumentos.
Não faz alias de origem para12 ou para outro ID. Overrides de R1 alteram o
registro bruto, mas não transferem ownership/binding. Se bytecode consultar
uma origem diferente do binding suportado, a leitura falha explicitamente.

O observer consulta a tripla imediatamente após submit; a última tripla é
limpa no início de cada tentativa e não é reutilizada para pedidos ausentes,
rejeitados antes da factory ou não resolvidos. Requests sem chamada ao sink,
como FWAV ausente, recebem null. O modo explicit_tuple conserva a tripla
fornecida como fixture; não aplica overrides adicionais à tripla, mas ainda
recusa um registro selecionado marcado unsupported. O caminho silencioso não
ganha campos de áudio nem muda sua saída anterior.

## Evidência

Parser:75 checks de recursos HIT/HOT/HSM, incluindo limites signed, equates,
campos omitidos/vazios, valores inválidos em cada posição, overwrite e budgets.
Resolvedor:11 checks puros com tripla imutável, overrides independentes,
extremos int16/int32, zero e registros não resolvidos. CLI adicional verifica
caller versus StackObject, argumento1 sobrescrito sem trocar ownership,
overrides negativos/INT32_MAX, ausência de alias em GetSrcDataField, erro antes
de gerar PCM, FWAV ausente sem recibo stale e exclusão entre as duas opções.

Suítes Debug e ASan/UBSan:112 configurados,111 aprovados e um X11 ignorado.
O CLI foi repetido em ambos após ampliar o caso de override da origem. Builds
observados sem warnings; LSan desativado. Sem SDL/X11, quatro testes de recursos,
argumentos, probe silencioso e rejeição de áudio passaram. Logs em
registro arquivado não distribuído,build-asan}-ctest.log,
registro arquivado não distribuído,-asan}-final.log e registro arquivado não distribuído

Cena nativa da cadeira, nas mesmas condições do checkpoint de áudio: remover a
tripla manual e usar --audio-caller-args. EventMapping chair_sit_metal na linha4786
do HOT é kSndobPlay,3410,0,0,0,0; origem2 deriva[2,0,0]. Debug/ASan retornaram0,
sem fault,30208frames gerados/submetidos/consumidos no SDL dummy. A simulação
mantém94dispatches/662ticks; PCM esvaziado com HIT ainda pendente, sem forçar
ticks de World ou conclusão. Comando e recibos em registro arquivado não distribuído
Controles unidade/centro, Literal32 e escolha0 continuam explícitos. Não há
afirmação de audibilidade, saída original, saved Main ou hardware portátil.

## Próxima camada

Track.volume e controles vol/extvol/pan não são os argumentos brutos do caller.
Nova evidência de inicialização antes do primeiro script e composição de volume
está em [hit-native-controls-evidence.md](hit-native-controls-evidence.md).
Ainda faltam controles/controller banks/shared providers, geometria/visão,
ArgStyle, escala do backend, grupos/ducking e relógio geral do lote. Não
substituir essas camadas pela tripla bruta recém-resolvida.
