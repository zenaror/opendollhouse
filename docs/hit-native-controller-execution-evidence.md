# Subconjunto executável e ordem do poll HIT nativo

Evidência estática de 2026-10-15, base ODH `f5ab084`. Complementa
[lifecycle](hit-native-controller-lifecycle-evidence.md) com encoding de
LoadL/Wait/Call, flags e aritmética do fade. Sustenta um núcleo separado com
binding de áudio corrente e Clock explícito; não transforma o backend de
última handle do perfil legado em backend nativo.

## Proveniência

Imagem externa `../referencias/The Sims/Sims.exe`, SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
Base PE `0x400000`; offset de arquivo dos trechos selecionados = VA −
`0x400000`. Exports existentes `0057e3dc`, `00582457` e assembly pontual;
bytes e entradas da jump table em
`reports/hit-native-controller-execution-selected.asm.txt`. Nenhum original
executado, projeto Ghidra ou referência alterados. OMM consultada como
continuidade; a prova abaixo vem dos bytes.

## Encoding e fronteira das variáveis

| Opcode | Encoding demonstrado | Handler VA / offset |
|---|---|---|
| 2 | Um byte literal passado ao áudio corrente, sem store de handle | `57e478` / `17e478` |
| 5, LoadL | Byte de localização, depois DWORD little-endian | `57e4ef` / `17e4ef` |
| 7, Call | DWORD little-endian usado como chave de lookup | `57e6bf` / `17e6bf` |
| 8 e12 | Sem operando; pop da pilha ou solicitação de parada | `57e6e7` / `17e6e7` |
| 9, Wait | Byte de localização; valor obtido pelo getter | `57e726` / `17e726` |
| 11 | Sem operando; status do binding e retry | `57e761` / `17e761` |
| 27 | Sem operando; solicitação +34 | `57f0c1` / `17f0c1` |

LoadL usa `0x57f85a` para ler a localização e testa não zero/<=127.
`0x57e501` lê o DWORD, avança PC em4 e chega ao writer público+84 com
`(localização,bits,0)` em `0x57e4c3–0x57e4cd`. Não há conversão numérica do
imediato: bits negativos são preservados. No provider próprio, IDs1–16
alcançam o banco de registradores, atualizam flags e retornam sem efeitos de
áudio; `0x57e1ea–0x57e1ed` encaminha IDs<18 ao retorno. ID16 pertence ao
mesmo banco e pode ser escrito; seu zero fresh não o torna constante.

O writer público `0x57e150` delega ao provider+c8 quando aponta para outro
objeto, exceto ID53. O writer local `0x57e191` estabelece zero=(value==0),
positive=(value>0), sign=(value<0), grava o banco e então despacha efeitos
especiais. Um núcleo sem provider compartilhado deve limitar explicitamente
seu domínio ao próprio banco.

ID55 também só grava banco e flags: `0x57e2eb–0x57e2ee` compara com55 e
retorna por `0x57e379`, sem side effect. No initializer,46=1 é escrito pelo
writer local+94 (`0x57deab–0x57debe`), deixando zero=false, positive=true,
sign=false; a pausa seguinte não altera essas flags. Os setters públicos
19 e20 do caller substituem esse resultado e deixam as flags do pan final,
conforme os bytes já selecionados na evidência de lifecycle/retrigger.

**Operando inválido não tem fallback seguro implícito:** `0x57f85a` lê um
byte unsigned; se>127, chama diagnóstico manager+c8 e retorna1. Zero passa
pelo reader e é recusado pelos handlers LoadL/Wait. O callback padrão
`0x57c05d` chama stop+38; retorno1 do reader não prova sucesso da instrução.
Recusar bytes fora1–127, variáveis desconhecidas e instruções truncadas
antes de efeitos é política defensiva ODH, não reprodução desse estado
residual após diagnóstico. Um subconjunto pode aceitar LoadL somente1–16
e recusar os demais destinos ainda que sejam encodings nativos válidos.

Wait9 usa getter+88 e `ADD` em `0x57e741–0x57e74c` para guardar
`deadline198 = Clock + valor`. A soma é módulo2^32; não há clamp, mínimo1,
teste de valor negativo ou decremento periódico da variável. Valor0 deixa
o deadline igual ao Clock; se não zero, o próximo bytecode pode executar
no mesmo poll. Soma que resulta0 desabilita o loop de bytecode.

Call7 **não** usa o DWORD como endereço absoluto. Chama `0x57d0a9`, que
consulta manager+168 por `0x582457`, obtém o valor associado e o usa como
novo PC. O helper do hash pode inserir uma chave ausente. Só depois o
handler empilha o PC de retorno em+1a8. Sem contrato do mapa de entrypoints,
o subconjunto deve recusar7; não substituir essa resolução por salto direto.
Nesse escopo, 8/12 suportam apenas a pilha vazia, explicitamente; não alegar
cobertura de retorno aninhado sem Call ou entrada de pilha válida.

Extensão de 2026-10-17: o controller passou a aceitar um resolvedor explícito
do caller para Call7 e a manter pilha limitada. Isso remove a recusa de7 quando
há mapa fornecido, sem estabelecer a origem/precedência do mapa do manager.
O contrato e a validação posteriores estão em
[hit-native-call-validation.md](hit-native-call-validation.md); a descrição
acima preserva o recorte e as limitações da investigação original.

## Parada gradual e flags

End com pilha vazia e opcode27 chamam+34. Com áudio corrente ativo,
`0x57dfc2–0x57dffe` escreve40=0,41=19,42=100 e45=1, nessa ordem, pelo
writer público; liga byte+1cf antes da última chamada. No provider próprio,
as flags finais são zero=false, positive=true, sign=false. IDs40/41/42
não têm outro efeito especial nesse writer;45 somente testa atividade e
zera45 se inativo (`0x57e352–0x57e364`). O ramo não escreve deadline ou PC.
Com áudio ausente/inativo, +34 chama+38; se controller já inativo, retorna.

O fade usa41 como **localização de variável**, não como valor de volume.
O getter público lê essa variável; o writer público recebe a mesma localização
com terceiro argumento0. Para destino19 há atualização do áudio corrente
via `0x57fa52`; não se deduz daí escala Q15 ou ganho físico. Destinos que
um núcleo não saiba ler/escrever devem produzir recusa explícita, sem zero
inventado ou remapeamento para19.

Em `0x57db00–0x57db61`, a operação exata é:

```text
current = read(variable41)        // interpretação signed32
target = variable40; step = variable42
if step == 0: variable45 = 0
if (current < target) != (step > 0): step = neg32(step)
next = add32(current, step)
if (step > 0 && next > target) || (step < 0 && next < target):
    next = target; variable45 = 0
write(variable41, next, 0)
```

As comparações são assinadas; NEG e ADD guardam só os32 bits. Logo
`neg32(INT32_MIN)==INT32_MIN`. Não substituir por saturação ou intermediário
signed C++ com overflow. Igualdade exata **não** é ultrapassagem; atingir o
alvo exatamente mantém45 naquele poll, podendo limpar no próximo. Step0
ainda executa a escrita de current, com flags e efeitos correspondentes.
Depois, se+1cf!=0 e45==0, chama+38 e sai imediatamente do poll; não executa
bytecode nem verifica o deadline194 nesse ramo.

## Ordem dos deadlines e limites do wrap

O poll `0x57dac6` exige ativo e não pausado. Sua ordem é:

1. Processar fade e a eventual parada após fade.
2. Capturar Clock do manager em um temporário do poll.
3. Enquanto198!=0 e `uint32(198)<=uint32(Clock capturado)`, executar
   instrução; ignorar seu AL e testar novamente198.
4. Só depois de sair desse loop, testar194!=0 e
   `uint32(194)<=uint32(Clock capturado)`. Se vencido, chamar áudio+98(0)
   quando houver binding e zerar194, inclusive sem binding.

O prazo194 de NoteOn temporizado é separado do198 de instruções. Assim,
bytecode devido tem precedência sobre194 devido no mesmo poll; uma nova
escrita em194 pode substituir o prazo antes de ele ser verificado. Stop
chamado por uma instrução zera198 e permite sair para essa verificação;
stop do ramo pós-fade retorna antes dela. Pausa/inatividade na entrada
também impedem ambas as verificações. Callbacks de backend/host não devem
ser usados para reentrar e modificar o estado durante essa sequência.

WaitSamp ativo usa dois `INC` em `0x57e9bb–0x57e9c0`: Clock+2 módulo2^32.
O código não usa comparação de distância modular nem tolerância de wrap.
Ativação copia Clock literalmente para198; zero não recebe ajuste.

Há uma ressalva adicional: Clock com bit de sinal ligado chama diagnóstico
em `0x57db94–0x57dba1`; deadline198 vencido com bit de sinal também chama
diagnóstico em `0x57dbbb–0x57dbca`. O startup instala callback `0x57c05d`
em manager+c8 (`0x57ca63`), cujo corpo chama stop+38. Não afirmar que o
original atravessa livremente o limite de sinal ou wrap completo porque as
somas usam32 bits. Um núcleo pode restringir Clock a0..INT32_MAX, recusando
valores fora desse perfil, ou representar esse diagnóstico/stop explicitamente.
Deadlines resultantes de ADD continuam palavras32; restringir Clock não
autoriza saturar seus bits. A cadência externa do Clock segue desconhecida.

O contador local de instruções começa0 em `0x57dba9`. Depois de cada handler,
`0x57dc02–0x57dc0b` carrega o valor antigo, incrementa o contador e compara
o valor antigo com100, continuando se<=100. Assim o primeiro diagnóstico
ocorre **após a102ª instrução** daquele poll. O callback padrão solicita
stop+38; o dispatcher volta ao teste do deadline. Não confundir essa regra
com um yield obrigatório a cada100 ou102 instruções. Um limite explícito ODH
pode falhar antes, mas é política de segurança própria.

## Subconjunto coerente para implementação

Há prova suficiente para um núcleo próprio com 2,5,9,11,12,27 (8 pode usar
o mesmo ramo de12), LoadL restrito a registradores1–16, leituras de Wait
limitadas a variáveis conhecidas, provider próprio e pilha sempre vazia.
Call7 e demais opcodes devem falhar explicitamente. O núcleo deve executar
bytecode real: por exemplo, LoadL pode preparar o valor de Wait; NoteOn atua
sobre o binding corrente; WaitSamp inativo continua no mesmo poll; End pode
armar fade e continuar. Esses efeitos não são equivalentes a executar apenas
uma tabela de transições externa.

Os callbacks abstratos pertencem ao **binding corrente**: consultar atividade,
receber o byte literal de NoteOn, aplicar controles raw, pausa/retomada e
teardown/substituição. Nenhum callback deve interpretar o byte de NoteOn como
destino de handle. Implementar essa fronteira não prova DSP, quantidade de
vozes, buffers compartilhados ou política física de reprodução. Limites de
instruções, falhas fixadas e proibição de reentrada são políticas ODH que
devem ser identificadas separadamente. A integração do host só pode ampliar
as alegações quando seu adapter concreto cumprir esses contratos.

## Extensão: chave HOT de entry point não exige chooser inicial

O campo CSV7, com prefixo `@`, é guardado pelo parser ODH como
`HotTrack.hitlist_id`. No caminho nativo já rastreado, esse argumento do
construtor é armazenado em `this+0x31c` (`0x57d781–0x57d78b`, offsets
`0x17d781–0x17d78b`). `0x57dedb` lê esse campo em `0x57dee0`, consulta
o mapa `manager+0x168` por `0x582457` e retorna o valor associado. O
initializer usa o resultado como PC e loop-PC (`0x57de79–0x57de8a`);
os caminhos do parser e initializer completos estão na evidência de
[retrigger](hit-native-retrigger-evidence.md) e
[lifecycle](hit-native-controller-lifecycle-evidence.md).

Essa inicialização não escolhe uma patch de hitlist nem carrega essa chave
como lista ativa: limpa as listas+1b0/+1b4, enquanto o binding inicial depende
separadamente do ID50 efetivo. Portanto uma API que já recebe `entry_pc`
resolvido pode aceitar `hitlist_id` resolvido e não zero. Exigir zero para
evitar "initial hitlist selection" importaria uma restrição do caminho de
seleção legado ODH para esse contrato nativo. Isso não habilita opcodes de
listas, Call7, resolução automática de entry ou execução de todo script real.

Esta extensão acrescenta ao relatório somente os stores de+31c e o helper
de entry; o lookup `0x582457` já consta da seleção anterior. A fonte e seu
SHA256 permanecem os mesmos. SHA256 do relatório antes desta extensão:
`d91670be9386f2c95827fc23672056a58ae8c02b3904a2cf51cd802dbf3207d6`.

Precisão acrescentada na investigação do loader: o initializer acessa esse
objeto via `controller+0xc4`, que o construtor inicializa como self, e o valor
do mapa é um ponteiro para um buffer HIT mais offset, não um PC global de
arquivo. Chamadas podem atravessar buffers. A origem do mapa e os limites da
precedência estão em [mapa nativo](hit-native-entry-map-evidence.md).
