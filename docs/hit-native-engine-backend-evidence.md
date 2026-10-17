# Engine e backend concreto do áudio HIT

Investigação estática de 2026-10-17 de `../referencias/The Sims/Sims.exe`,
SHA256 `6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
O executável original não foi executado ou alterado. Esta unidade complementa
[dispatch de binding](hit-native-binding-pause-evidence.md).

## Engine, interfaces e slots

O provider instalado por `0x5d5e1f` nasce em `0x604849`/`0x604890` e usa a
vtable `0x676c60`. Seu slot `+0x14` aponta para `0x5d5e09`, getter do campo
`provider+0x138`. O construtor `0x5d56da` cria esse engine por `0x603243`,
cuja vtable concreta é `0x676acc`. É esse ponteiro que `0x57ca49` instala
em `manager+0`.

| Factory do engine | Objeto retornado | Vtable do retorno |
| --- | --- | --- |
| `+0x5c` → `0x603d43` | objeto de `0x174` bytes, construtor `0x5f7ebe` | `0x676094` |
| `+0x70` → `0x603ec1` | interface em `base+0x174` de objeto de `0x1bc` bytes, construtor `0x5fab0b` | `0x6762e4` |

A segunda interface usa thunks que subtraem `0x174` do ponteiro `this`.
A vtable da base desse objeto é `0x6763e0`. Os campos seguintes são relativos
à base, não à interface secundária.

| Slot | Vtable `0x676094` | Interface `0x6762e4`, após ajustar `this` |
| --- | --- | --- |
| `+0x20` | `0x5f95ef` | `0x5fabab` → `0x5f95ef` |
| `+0x24` | `0x5f95a3` | `0x5fa9e7` → `0x5f95a3` |
| `+0x2c` | `0x5f8051` | `0x5f8051` |
| `+0x30` | `0x4d44bb` | `0x4d44bb` |
| `+0x74` | `0x5f94bd` | `0x5faa86` |
| `+0x98` | `0x5f956b` | `0x5faac7` → `0x5f956b` |

## Pausa, retomada e replay

O slot chamado pela pausa, `+0x2c`, despacha para `+0x28` e alcança
`0x5f9798`. No ramo sem streaming, ele consulta os buffers em `base+0x80`,
com limite de oito entradas e parada no primeiro nulo, e chama
`IDirectSoundBuffer::Stop` para os que têm o bit `PLAYING`. Pode cancelar
seu timer e chamar o callback armazenado no backend. Esse caminho não libera
os buffers COM nem reposiciona seus cursores.

O slot chamado pela retomada, `+0x30`, contém `mov al,1; ret` em `0x4d44bb`.
Ele retorna sucesso sem reiniciar áudio. Portanto, esta implementação concreta
não sustenta uma política de congelar um cursor e retomá-lo automaticamente.

O ramo de streaming, identificado por `base+0x25`, usa `0x5f9fe1`: cancela o
timer quando ativo, para o buffer primário conforme seu flag de estado, fecha
e libera o leitor, zera esse ponteiro e limpa o indicador de preparação. O
buffer COM permanece; o leitor não permanece como uma reprodução suspensa.

O NoteOn do wrapper obtém `+0x74`, chama `+0x98(0)` e depois
`+0x20(gain, bool(ID44), 1)`. `+0x74` lê o campo de controle `base+0x28`,
inicializado com `1024`; `+0x98(0)` tenta `SetCurrentPosition(0)` no buffer
primário. Seu resultado não é testado pelo wrapper.

Em `0x5f95ef`, quando já há um buffer ativo e o terceiro argumento é `1`,
o código reposiciona o buffer selecionado para zero e aplica controles, sem
outra chamada a `Play`. Quando inativo, aplica controles e chama `Play` no
buffer primário. Nesse ramo não há um segundo seek: após uma parada de um
buffer existente, o seek vem da chamada anterior do wrapper. Se o buffer
ainda não existir, aquela chamada falha e o caminho de play pode carregá-lo.

O status `+0x24` consulta até oito buffers e reconhece `status & 5`, ou seja,
`PLAYING | LOOPING`. Outros modos do backend têm busca de buffer ocioso e uma
chamada a `DuplicateSoundBuffer`; isso não prova oito vozes simultâneas nem
monofonia global. Há inclusive um ramo de duplicação que passa o valor nulo
da entrada corrente como origem; o relatório preserva essa irregularidade.

## Retenção e fronteira externa

A continuação em [ownership do backend](hit-native-backend-ownership-evidence.md)
rastreia o caso de alias `patch+0xe0 == patch+0xe4`, seu contador e os releases,
além do namespace global de Track/Patch usado no binding. As observações
abaixo delimitam o primeiro recorte; a continuação preserva as lacunas de
retenção externa e não comprova um adapter PCM completo.

As factories criam novos backends a cada chamada, mas `0x580229` apenas copia
os ponteiros da patch para o wrapper. Wrappers da mesma patch podem observar
o mesmo backend. Os contadores da patch controlam inicialização e carga dos
recursos. O destrutor do wrapper encaminha uma parada; a liberação do backend
aparece na limpeza da patch. A contagem completa de referências, incluindo
o caso em que `patch+0xe0` e `patch+0xe4` coincidem, não foi fechada nesta unidade.

A origem COM foi identificada por strings, importações e chamadas: o engine
carrega `dsound.dll`, resolve `DirectSoundCreate` e `DirectSoundEnumerateA`,
e usa `CreateSoundBuffer` para preencher `base+0x80`. Os slots foram comparados
com `/usr/share/mingw-w64/include/dsound.h`, cujo hash está no manifesto.
A DLL e o driver não foram executados ou inspecionados; latência física e PCM
já encaminhado ao dispositivo permanecem fora dessa prova.

`+0x78` limita o controle de ganho a `0..1024`, combina-o com um valor global
e produz um argumento inteiro para `SetVolume`. Isso não estabelece uma
conversão linear para Q15. A versão espacial tem ajuste adicional.

## Consequência para um adapter ODH

É justificável modelar esta pausa como parada lógica, reter a identidade do
binding/recurso e tratar a retomada como nenhuma operação de áudio. Um novo
NoteOn pode reiniciar do começo. `PcmTransport::stop` já representa parte desse
contrato; liberar um handle é uma decisão distinta da parada original, que
retém buffers. O adapter precisa preservar identidade e compartilhamento por
patch/backend, sem presumir exclusividade por controller.

A semântica do PCM já misturado e enviado ao sink, a conversão dos controles,
os callbacks e a implementação do ramo de streaming ainda precisam de limites
explícitos. Essa evidência permite abandonar a hipótese de pausa por cursor,
mas não comprova um adapter completo nem equivalência sonora com o original.

## Reprodução

`reports/hit-native-engine-backend/extract.py` verifica o SHA esperado, deriva
VA/RVA/offset dos cabeçalhos PE, extrai 44 trechos por `objdump` e compara os
6.726 bytes impressos com a imagem. `selected.asm.txt`, `vtables.json` e
`manifest.json` preservam a evidência; `findings.txt` detalha ramos e pendências.
Os exports auxiliares do Ghidra 12.1.3 usam o projeto ODH já existente,
`Sims.exe`, `-readOnly -noanalysis`; o import original desse projeto identifica
loader PE e arquitetura x86. Nenhum projeto ou configuração do Open-GBP foi
alterado. A verificação é estática, sem execução do jogo ou teste em hardware.
