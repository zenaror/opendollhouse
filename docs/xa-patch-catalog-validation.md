# Catálogo de patches XA em fontes FAR e avulsas

XaPatchCatalog é um componente host de leitura seekable. Recebe fontes FAR v1
e XA avulsos explícitos; não procura pastas nem escolhe prioridade entre arquivos.
A resolução usa o basename do HotPatch.filename com separadores Windows/POSIX
e lowercase ASCII. O prefixo do filename HOT não vira um caminho de filesystem.

Referência: Simitone4906db3/FreeSO submodulef86bbd3,
TSOClient/tso.content/TS1/TS1Audio.cs218–270: GetSFX usa Patch.Filename;
GetAudioFrom normaliza e consulta XASounds pelo filename, via TS1SubProvider.
O catálogo ODH rejeita ambiguidades em vez de importar precedência ainda não
comprovada. Não afirma equivalência ao provider ou ao original.

## Contrato

Constrói índice limitado sem decodificar payloads. Registra todas as entradas
FAR no orçamento, mas só indexa candidatas XA. Duplicatas de basename são
ambíguas mesmo com caminhos/case distintos ou misturando FAR/loose. Uma
duplicata não impede consultar outra chave única; ordem das fontes não resolve
o conflito. Patch ausente, extensão não XA, nome inválido ou asset ausente
falham explicitamente.

load(hot,id) abre novamente a fonte selecionada, verifica seu tamanho e passa
somente o subview da entrada ao decoder existente. Não extrai arquivo nem
copia o arquivo FAR inteiro para a memória. Offset, comprimento, nome da
entrada e fonte são devolvidos junto do PCM owned. Para loose, offset0 e
entry vazio. Paths de fonte tornam-se absolutos na construção, permanecendo
ligados ao arquivo após mudança de working directory.

As fontes precisam permanecer imutáveis: mudança de tamanho é detectada,
mudança de conteúdo de mesmo tamanho não é garantida. Não há cache de PCM,
playback ou políticas de loop/piano/mixer neste catálogo.

Limites padrão:16 fontes,65536 entradas agregadas,4096 bytes por nome/path,
4MiB de strings lógicas e os limites do decoder (16MiB por XA/4MiB PCM).
Strings contabilizadas: paths absolutos, nomes serializados de todas as
entradas FAR e keys XA normalizadas, incluindo duplicatas.
O limite de entrada XA vale para a entrada selecionada, não o container FAR.

Contagem de records limita metadata; name_bytes é contabilidade lógica,
não RSS ou capacidade de allocators. PCM devolvido pertence ao chamador e
não é contabilizado globalmente depois do retorno. O mixer mantém seu orçamento
agregado próprio em [pcm-mixer-validation.md](pcm-mixer-validation.md).
load(hot,id,allowance) permite reduzir o limite PCM do decoder à capacidade
disponível do chamador antes da alocação; não pode ampliar o limite configurado.
Nada disso prova viabilidade em consoles.

## CLI

odh-xa-patch HSM HOT PATCH_ID (--far FILE|--xa FILE)...

ID decimal; múltiplas fontes explícitas. JSON descreve proveniência, rate,
canais, frames, trailing bytes e tamanho lógico do índice. Não abre áudio nem
produz WAV/extração. Erros retornam2 sem recibo JSON de sucesso.

## Evidência

Teste C++ com35 checks e wrapper CLI Python, apenas fixtures sintéticas:
FAR/loose, PCM exato, trimming/tail, basename/case/slashes, proveniência,
duplicatas em ambas ordens/dentro do mesmo FAR/misturando fontes,
unrelated duplicates, invalid patch/name/extension, budgets agregados,
input/PCM limits, malformed XA, mudança de tamanho e cwd, PCM owned.
Allowance inclui zero, menor/exato e limite configurado não ampliável.
CLI confere argumentos, metadata, erro ambíguo e SHA input preservado.

Suítes atuais Debug e ASan/UBSan:106 configurados,105 aprovados,um X11
ignorado pelo ambiente; sem warnings. LSan não validado (detect_leaks=0).

SoundData.far nativo:2965 entradas,2909 XA. Patch3212 encontrada pelo filename
do HOT como Voice\\mood\\VOX_COMFORT_SIGHM4.XA, offset109753498/length29124;
mono22050Hz/54296frames. As12 patches do conjunto anterior coincidem em
SHA256 encoded e metadata PCM. Recibo registro arquivado não distribuído
Patch3212 encoded SHA256:
e68b39e613b2500e40796346fb6cef6343ae97dda5eea2ae0debc3f9f6dcbd64.

Verificador registro arquivado não distribuído
SHA256 4bdd901045c3f0ca5825d174122abcf27a6f3b3393cf314b02600f102c97f62f:
factory HOT/HIT ->World source binding ->catálogo FAR ->XA ->SDL dummy.
Args[1,1024,0], Literal32 e escolhas0/0 permanecem explícitos. World avatar
construído, não saved-frame continuation; group1 vazio explicitamente antes
da nota. Todos os54296 samples coincidem com a referência decodificada
anterior. Debug e ASan/UBSan concluem a instância e fazem stop uma vez.
Sem hardcode de path/offset no loader; patch3212 só é assertion da fixture.
Recibos registro arquivado não distribuído,asan}.json.

O backend do verificador ainda admite uma nota nessa fixture, sem mixer.
Consumo da fila não prova audibilidade/DAC, playback original ou Main retomado.

## Continuidade

A cobertura atual está na matriz pública de funcionalidades. Os resultados abaixo
se limitam ao cenário histórico descrito.
Mixer isolado já validado em pcm-mixer-validation.md. Pendentes: backend
compartilhado/cancelamento, diretórios/precedência/formatos não XA,
integração do loopWorld, caller arguments/units/ArgStyle/controllers, loops e
outras partes da meta integral. Não usar reInit ou sucesso artificial para
contornar os gates de Main.


A [continuação global/reutilizável](hit-reusable-pcm-validation.md) acrescenta
`load(NativeHitPatchRef,allowance)` e novas verificações. Os totais anteriores
permanecem como evidência histórica; o catálogo ainda não decide streaming.
