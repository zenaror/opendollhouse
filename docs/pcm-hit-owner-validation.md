# Notas PCM por thread HIT

PcmHitOwner implementa SoundThreadLifetime sobre o transporte compartilhado.
make_xa_hit_sound monta a factory HOT/HIT existente com esse owner e o catálogo
XA, sem repetir lifetimes ad hoc em cada chamador. O serviço do loop principal
ainda não está ligado: esta unidade valida componentes e uma execução BHAV
sintética em World construído.

## Ownership e admissão

O owner tem endereço estável (sem copy/move), empresta PcmTransport e possui
loader, controles explícitos e uma lista de handles. O limite é de1 a256 notas
bem-sucedidas durante toda a vida,64 padrão; reserva a lista antes de admitir
PCM. Handles completos continuam consultáveis e não reciclam o limite da
thread. Não há alocação da lista depois de uma admissão bem-sucedida.

backend devolve callbacks que emprestam o owner: não podem sobreviver a ele ou
reentrar seus callbacks. SoundThreadInstance mantém o lifetime até destruir a
HIT e chama stop antes dessa destruição. Recursos capturados, transporte,
catálogo, HIT/HOT/World e callbacks externos precisam viver mais que a instância.
Tudo ocorre na thread proprietária do transporte.

start recusa owner parado/limite atingido antes de chamar loader. O loader
recebe patch e allowance atual do PCM agregado; o helper chama
catalog.load(hot,patch,allowance) somente durante NoteOn. Em seguida transporte
admite o PCM e o owner guarda o handle, sem alocação posterior dessa lista.
Limite local retorna nullopt, segundo o contrato de failed start HIT; erro de
asset/decoder/transporte propaga e faz HIT falhar. Não há fallback de recurso.
Se loader pedir stop, a checagem posterior impede admissão tardia.

Orçamento PCM e buffer continuam pertencendo ao transporte/mixer. A lista de
handles e seus callbacks são metadata adicional limitada por thread, não
contabilidade de RSS. Coleção também tem limite de threads. Sem cache ou
prova de viabilidade nos portáteis.

## Estados e cleanup

Somente handles pertencentes ao owner são consultáveis. Active/rendered viram
HIT active; PCM consumido ou deliberadamente parado vira HIT complete enquanto
o owner está vivo. Complete aqui significa inativo, não prova de fim natural
audível. Owner parado ou transporte failed viram HIT failed, nunca sucesso.

Referência consultada: Simitone4906db3/submodule FreeSO f86bbd3, revisões completas
em references.md. No submodule, TSOClient/tso.sound/HITThread.cs387–390 trata
SoundState.Stopped como nota inativa; KillVocals183–193 para/dispose as notas.
HITInterpreter.cs160–169 só mantém WaitSamp no mesmo PC quando NoteActive é
true. Isso fundamenta a distinção inativo/erro, não uma política de grupos.

stop é noexcept/idempotente e tenta release de todos os handles, mesmo após
erro no primeiro. PcmTransport libera o alvo antes de propagar falha de flush;
owner registra cleanup_failed e continua liberando os demais. Sink permanece
compartilhado e não é fechado. Destrutor também chama stop.

kill_group é somente o handler explicitamente fornecido. Sem handler, o campo
permanece vazio e opcode69 falha pelo contrato HIT existente. Após owner stop
ou transporte failed, não chama efeitos de grupo. A revisão encontrou e motivou
essa última guarda, testada antes da publicação. Não foi inferido registry,
pertencimento global de grupo ou equivalência ao original. SeqGroupKill na
referência HITInterpreter.cs651–661 tem comportamento parcial e TODO de origem.

## Factory host e execução BHAV

make_xa_hit_sound recebe recursos emprestados e exige chooser, controles,
limites, jump profile, argumentos HIT e handler de grupo explícitos. Retorna
SoundThreadInstance para a coleção existente. A construção não decodifica XA,
inicia playback ou executa HIT. Resolução/geração/cleanup de falhas da factory
base permanecem em vigor. Não converte campos Volume/SampleRate do PlaySound
nem implementa pan/zoom espacial, dedup ou owners compartilhados do evento.

44 checks portáteis: limite antes do loader, allowance corrente, callbacks e
handles estrangeiros, rendered versus consumido, duas origens, loader inválido,
grupos explícitos/ausentes, reentrância, stop durante loader, erro de sink,
cleanup de múltiplas notas após falha de clear e ausência de efeitos de grupo
após falha da saída.

45 checks de integração host com XA sintética em diretório privado: VM executa
opcode23, resolve nome FWAV via callback e submete duas instâncias à coleção.
Retorna true sem esperar som nem decodificar no submit. Ticks HIT fazem NoteOn;
fila fake aceita2frames de blocos8. Remover uma origem limpa mix/fila/sobra,
invalida seus handles e regenera só a sobrevivente. A outra instância termina e
libera PCM/registros. Asset ausente e orçamento PCM restante insuficiente
falham no NoteOn, sem phantom handles ou falha do sink não relacionado.
Argumentos[12,0,0] e controles unidade/centro são explícitos; flags da fixture
desligam pan/zoom e não pedem loop. Não é retomada de Main ou simulação de lote.

Suítes finais Debug e ASan/UBSan:110 configurados,109 aprovados e um X11
ignorado; builds sem warnings. LSan desativado. Os dois testes novos também
passam numa configuração com ODH_SDL_AUDIO=OFF e ODH_X11=OFF. Logs em
registro arquivado não distribuído,-asan}-{final,ctest}.log e
registro arquivado não distribuído,build,tests}.log.

Verificador nativo registro arquivado não distribuído SHA256
f9dcaf51dc6291e9e1ecf48754fea3286ae654a0253fb5fcec8af8a01f1123ee:
substitui o backend/owner ad hoc do checkpoint anterior por make_xa_hit_sound,
com as mesmas XA2756/3212 e saída SDL dummy44100 estéreo. Continua exigindo
args[1,1024,0], Literal32, choices0/0 e grupo1 vazio explícito da fixture.
Não deriva callerargs reais. World permanece construído, sem BHAV nativo Main.
Dois handles antes de destruir instância, um só depois (background externo),
zero após cleanup final. Debug e ASan/UBSan produziram109568frames e
FNV1a64 PCM little-endian8bca194e939e6a8d, iguais ao transporte anterior; detalhes
e recibos registro arquivado não distribuído,asan}.json.

## Continuidade

Próximo: serviço de som com agendamento no loopWorld e política comprovada de
owner/grupo/ganho espacial, sem adotar argumentos de fixture como caller real.
Preservar gates Main/savedframes, CallReturn/loops/controllers/ArgStyle,
precedência e demais requisitos Linux. Consulte a matriz pública de funcionalidades.
