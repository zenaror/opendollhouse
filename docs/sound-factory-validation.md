# Factory de eventos HIT

Checkpoint de 2026-10-14. resolve_hit_event liga SoundRequest.name a
Hot.events/Hot.tracks e à entrada HIT; make_hit_sound cria uma
SoundThreadInstance compatível com a coleção existente.

## Contrato

Perfil explícito: um grupo HOT, evento tipo1 e track de bytecode não looping.
Nome é copiado somente após validar tamanho (256 bytes padrão, configurável
1..65536), sem NUL, com lowercase ASCII. Eventos/tracks ausentes, campos
unresolved, unsupported_reason e entrada fora de [16,ENTP ou fim dos bytes)
produzem FormatError. Versões HIT1.0/1.8, região e endereço são validados.

Uma hitlist inicial precisa existir, ser suportada e não vazia. Patch inicial
não zero precisa existir e não ser looped. Patch0 com hitlist válida é aceito:
resolve não escolhe nem consome RNG. HitThread aplica SetTrack inicial com o
HitChooser explícito. O patch inicial não é necessariamente o patch final
de NoteOn: bytecode pode alterá-lo.

Esta política é um perfil de grupo único suportado, não equivalência a todo
fallback do Simitone/original. Não resolve prioridades entre arquivos, simple
sounds sem bytecode, música/TV, dedup, owners compartilhados ou loops.

HIT/HOT/World são emprestados e devem viver mais que a instância.
Backend, chooser, limites, Jump profile e HitArguments são explícitos.
A factory não deriva ArgStyle, unidades, volume/pan/rate ou argumentos corretos
a partir de fixtures. O owner deve ter stop idempotente/noexcept e RAII.

Binding deve corresponder à fonte e à geração/profile da submissão; zero no
campo0 é válido. Reader é capturado por valor e consulta bancos vivos, sem
Entity pointer nem fallback ao array zerado. Recheck de geração após o chooser
inicial recusa remoção/IDreuse durante inicialização.

Guard de owner faz stop em falhas de resolução, validação, chooser e construção,
enquanto a função ainda possui o owner; após transferência a instância assume
cleanup. Criação pode chamar chooser em SetTrack, mas não executa HIT nem
inicia nota. Playback só começa em tick posterior.

O orçamento de nome e os limites de source binding/HitThread são aplicados.
PCM agregado e mixer não são implementados por esta factory.

## Referência técnica

Simitone4906db3/FreeSO submodulef86bbd3, caminho TS1 em
TSOClient/tso.sound/HITVM.cs209–212: Event.TrackID -> GetTrack -> SubroutineID.
Linhas254–261 constroem HITThread, definem PC e SetTrack antes de Sounds.Add.
HITThread.SetTrack e os leitores existentes documentam hitlist/patch inicial.
As branches TSO de HSM/ENTP fallback não foram importadas como TS1.
Licenças e revisões completas em references.md.

## Validação

63 checks sintéticos registrados no CMake, sem GoogleTest:
nome/case/budget, mapping/track unresolved e unsupported, versão/code/ENTP,
patch/list/loop, resolução sem escolher, chooser explícito/ausente/throw/index
e budget, cleanup, binding errado/stale, remoção durante chooser, lifecycle
da coleção, patch trocado pelo bytecode e leitura de banco vivo após factory.

Debug e ASan/UBSan:103 configurados,102 aprovados,um X11 ignorado no ambiente.
Sem warnings. O teste da factory foi repetido em ambos após adicionar a
checagem de banco vivo (sem alteração da implementação).
LSan não validado: ASAN_OPTIONS=detect_leaks=0.

Verificador privado reports/sound-factory-native.cpp:
SHA256 8bd36077a7abc1215917af4806e3dff7989bd21d088398451d7f9a70a7f1c2c1.
Usa generated HSM/HOT/HIT reais, World construído com avatar1/PersonData66,
idade27/gênero0/neatword7=300; não é retomada de entidade salva.
SoundRequest hungry_sated_vox ->track3979/start13699; args[1,1024,0],
Literal32 Jump e choices0/0 explícitos ->patch3212.
Backend valida a patch e lê a cópia XA privada previamente identificada,
não implementa ainda um loader genérico de filenames de patch.
Group1 tem registry vazio explícito antes da nota, não efeito global no lote.

XA SHA256 e68b39e613b2500e40796346fb6cef6343ae97dda5eea2ae0debc3f9f6dcbd64,
mono22050Hz/54296frames, consumida pelo SDL dummy; instância concluiu e
owner fez stop uma vez. Debug e ASan/UBSan passaram.
Recibos: reports/sound-factory-native-dummy.json e
reports/sound-factory-native-asan.json. Ticks dependem do host, não relógio
determinístico da simulação. Main não retomado; audibilidade não verificada.

## Retenção e próximas etapas

Arquivos inválidos anteriores preservados em reports/factory-before-takeover.
Artefatos do build na raiz, mixer incompleto e runners antigos permanecem
não versionados e não integrados; não são evidência de factory/mixer funcional.
Não fazer git add -A nem descartar esses arquivos numa limpeza genérica.

Pendentes: loader de patches/XA e mixer com budget agregado; integração dos
ticks ao loopWorld/resident Main, callerargs/controllers/unidades/ArgStyle,
fallbacks e precedência, loops e demais opcodes. A factory não libera Main
por sucesso artificial. Ver sound-thread-collection-validation.md e
hit-thread-validation.md para lifecycle e limites atuais.

