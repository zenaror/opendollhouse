# Fórmulas dos controles HIT antes e depois do wrapper

Investigação estática de 2026-10-17 sobre o PE32 i386 externo
`../referencias/The Sims/Sims.exe`, SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
O original não foi executado nem modificado. Esta unidade usa os recortes e
exports existentes antes de acrescentar assembly pontual; nenhum projeto
Ghidra foi aberto ou alterado. Complementa
[binding e origem dos controles](hit-native-binding-outcomes-evidence.md).

`python3 registro arquivado não distribuído` regenera manifesto,
assembly, vtables, constantes binárias e exemplos aritméticos derivados.
São 25 recortes, 2.131 bytes comparados com a imagem, com cobertura contígua
sem `.byte` ou instrução inválida. O manifesto deriva offsets dos cabeçalhos
PE e registra hashes por trecho. Os exemplos são avaliação sintética das
fórmulas, não execução do jogo nem testes do runtime ODH.

## Convenções necessárias para uma implementação portátil

`S32(x)` significa conservar os 32 bits baixos e interpretá-los com sinal.
`MUL32(a,b)=S32(a*b)`. `TZ(n,d)` é divisão inteira com sinal truncada em
direção a zero. Não usar overflow signed de C++ nem deslocamento lógico
como substitutos dessas operações. Os `IMUL` de dois operandos conservam
somente a metade baixa; o `CDQ` seguinte estende esse resultado já truncado,
não o produto matemático de 64 bits.

Os getters públicos são chamadas virtuais. Na rota própria, `0x57e10f`
chama `0x57e141` e o getter de banco `0x5805ba`, que percorre bancos e segue
defaults para o sentinel `0x80000000`, sem escrever bancos/flags. Se provider
não é o próprio controller nem nulo, o getter encaminha a chamada. Portanto
**retorno descartado não significa chamada inexistente ou universalmente
sem efeitos**. As fórmulas abaixo descrevem os valores efetivamente retornados.

## A: pan ou posição, depois ganho — `0x57fa52`

Se `controller+1d0` é nulo, retorna AL=0 sem getters ou chamadas de controle.
Caso contrário, obtém o wrapper uma vez e consulta pelo getter público, nesta
ordem: **19, 18, 20, 26, 27, 28, 29, 30, 31, 32, 56, 57**.

- Guarda 19, 18, o teste não zero de26 e os valores30/31/32.
- Os valores retornados por20/27/28/29/56/57 são descartados nesse helper.
  Isso não elimina o efeito dos setters diretos28/29, que têm outro dispatch.
- Com26=0, chama `wrapper+48` passando **o DWORD local `controller+44`
  (ID20)**. Não passa o resultado que acabou de obter do getter20.
- Com26!=0, chama `wrapper+4c(x,y,z)` com os valores dos getters30/31/32,
  nessa ordem. Antes, lê manager+cc/+d0 e calcula, com wrap de32 bits,
  `abs32(MUL32(y-managerY,y-managerY)+2*MUL32(x-managerX,x-managerX))`.
  O helper `0x636bef` é somente `if(signed<0) NEG; return`; seu resultado é
  sobrescrito em `0x57fb45`. Não existe uso dessa distância no ganho do
  controller nem transformação das coordenadas passadas ao wrapper.

Após pan/posição, chama o getter `track+ b8`; na Track de vtable `0x6719b4`
é `0x57d907`, que lê Track+320. Se o grupo é não zero, `0x57c0ff` procura
em manager+ec e **cria/registra um grupo se ausente**, com ganho+c0=1024.
Logo essa consulta pode modificar o catálogo do manager. O caller usa o
ponteiro retornado sem guard de falha de alocação.

Seja `v19` o primeiro getter19, `v18` o getter18 e `groupGain` o DWORD+c0
quando existe grupo. A fórmula exata antes do wrapper é:

```text
g = v19
if group != 0 and groupGain != 1024:
    g = TZ(MUL32(groupGain, g), 1024)
g = TZ(MUL32(v18, g), 1024)
duck = getter(123)
if duck != 0:
    priority = getter(25)
    if signed(priority) < signed(duck):
        g = TZ(g, 2)
wrapper.slot40(g)
```

O fator de grupo1024 é um **bypass**, não uma multiplicação com divisão que
poderia dar overflow. O estágio de18 sempre multiplica. As divisões1024 são
`IDIV` signed. A metade usa `CDQ; SUB EAX,EDX; SAR EAX,1`, portanto -513
vira -256. Não há clamp no controller. A comparação prioridade/duck é signed;
ID25 nem sequer é consultado quando ID123=0.

O retorno de pan/posição é ignorado. A retorna AL deixado por `wrapper+40`.
Os callers binding, initializer e writer examinados não testam esse AL.
Nenhuma chamada deste corpo é writer de banco/flags do controller.

## B: argumento de pitch — `0x57fa16`

Se não há wrapper, retorna AL=0. Caso contrário, lê **o campo local
`controller+48` (ID21)**, consulta patch.ID21 pelo getter+88 e chama:

```text
wrapper.slot44(S32(local21 + patchGetter21 - 3600))
```

A soma/subtração é o `LEA` de32 bits em `0x57fa40`. Não há getter público de
controller21 nem clamp. Depois da chamada B força AL=1, independentemente do
retorno do wrapper. B não escreve banco/flags do controller.

No binding válido, A/B ocorrem antes da cópia de patch.ID26 ao controller;
initializer e writer50 repetem A/B depois do retorno. Writer26 apenas grava
banco/flags. Esse contrato de fases é fechado pela unidade de binding.

## Fronteira portátil dos pedidos

Um adapter pode representar exatamente os pedidos inteiros ao wrapper:
`pan(local20)` ou `position(getter30,getter31,getter32)`, depois `gain(g)`,
e separadamente `pitch(S32(local21+patch21-3600))`. Isso não implica aplicar
esses números diretamente ao mixer, converter em Q15 ou chamar sempre a
mesma operação no backend físico. Em especial, o nome `position` descreve
o **slot do wrapper**, não garante `IDirectSound3DBuffer::SetPosition`.

## Wrapper: forwarding e transformação de frequência

| Slot | Corpo | Operação real |
| --- | --- | --- |
| +40 | `0x580147` | Envia inteiro sem mudança a backend+c0 slot+78 |
| +48 | `0x580335` | Envia inteiro sem mudança a backend+c0 slot+84 |
| +4c | `0x58035b` | Obtém patch+98; converte x/y/z signed32 para float32 e chama backend+ d0 |
| +44 | `0x5802d4` | Calcula frequência e chama backend+c0 slot+8c |

Nenhum desses wrappers verifica backend nulo. Posição usa `FILD` signed32 e
`FSTP` float32, em ordem z,y,x no stack para a assinatura x,y,z. Inteiros até
2^24 são exatos; os demais dependem do arredondamento x87 ativo na conversão.
Não há ajuste de escala, troca de eixos ou clamp nessa camada.

Para pitch, o wrapper subtrai **outros3600**, com wrap32:

```text
d = S32(wrapperPitch - 3600)
if d == 0:
    f = 22050.0
else:
    exponent = store_binary64(x87(signed(d)) * binary64_constant(0.01))
    f = pow_native(binary64_constant(1.0594630943592953), exponent) * 22050.0
integer64 = x87_FISTP_truncate_toward_zero(f)
backend.slot8c(low32(integer64))
```

Assim, no nível matemático e fora de wrap, o expoente vem de
`(local21+patch21-7200)*0.01`, não de `(local21+patch21-3600)*0.01`.
O caso local21=patch21=3600 envia3600 ao wrapper e produz a frequência-base22050.
Os bytes exatos estão em `constants.json`: a base é a aproximação binary64
`1.0594630943592953`, não uma operação simbólica exata `2^(1/12)`.

`0x636e69` usa o caminho x87 `FYL2X` e `0x6419e0`
(`FRNDINT/F2XM1/FSCALE`) para a potência. O helper salva/controla o ambiente
x87; o retorno normal restaura o control word. O produto por0.01 é
explicitamente guardado em binary64 antes da potência; o produto final por22050
vai diretamente do stack x87 ao conversor, sem store binary64 intermediário.
Substituir por `std::pow` e casts comuns não está provado bit-identical.

`0x63604c` força RC=truncate no control word, faz `FISTP` **signed64**, restaura
o control word anterior e retorna EDX:EAX. O caller usa só EAX. Para valor
finito representável em signed64, a regra é truncar e conservar low32; não
é saturação signed32. Para NaN/infinito/fora de signed64, exceções x87 e CRT
limitam a fórmula portátil; com invalid mascarada, FISTP produz indefinite64
cujo low32 é zero. O relatório não fecha toda a política de exceções/errno
nem o estado x87 global de todas as rotas do programa.

## Backend concreto: clamps e destinos não uniformes

Vtables confirmadas: comum `0x676094`; interface espacial `0x6762e4`, com
thunks que ajustam `this` em -0x174 para ganho/frequência/pan. A posição
espacial `0x5fac81` recebe a interface secundária diretamente.

**Ganho comum** `0x5f94c1`: limita o argumento signed a0..1024 e grava
base+28 mesmo sem buffer. Sem buffer primário retorna sucesso. Com buffer,
obtém ganho global pelo objeto em `0x6fed70`, slot+18, e calcula
`v=TZ(MUL32(globalGain,clampedInput),1024)`. Se v=0, envia -10000;
se v=1024, envia0; nos demais casos envia
`S32(TZ(MUL32(v,4000),1024)-4000)` a buffer COM+3c (`SetVolume`).
Não há um segundo clamp explícito de v nesse corpo. O sucesso exige HRESULT0.

**Ganho espacial** `0x5fabea`: o provider global+8 pode fazer retornar sucesso
sem modificar ganho. Com flag base+1a0 ligada, chama o ganho comum com o
argumento recebido. Com ela desligada, chama o comum com
`S32(input-base+1a4)` e depois restaura base+28 ao argumento raw recebido,
mesmo se a chamada comum reportar falha. A subtração é de32 bits.

**Pan comum** `0x5f93f9`: sem buffer retorna falso. Com buffer, limita signed
input a0..1024 e envia `TZ(input*20000,1024)-10000` a COM+40 (`SetPan`).
Esse produto não transborda após o clamp. Não guarda o pan num campo local.
O pan da vtable espacial chega a `0x607db7`, que retorna falso sem operação.

**Frequência** `0x5f9471`: provider global+8 falso retorna sucesso sem chamada.
Se habilitado, exige buffer e limita o argumento interpreted signed a100..100000,
enviando-o a COM+44 (`SetFrequency`). O caminho espacial encaminha ao mesmo
corpo. Os low32 de frequência que aparentam negativos portanto chegam ao
clamp inferior100, não a um clamp unsigned superior.

**Posição espacial** `0x5fac81`: com flag secondary+2c ligada, exige o ponteiro
COM secondary+4 e chama COM+4c (`SetPosition`) com x/y/z e apply=0. Sem o
ponteiro retorna falso. Com a flag desligada, grava x/y/z float32 em
secondary+38/+3c/+40 e calcula a distância euclidiana ao listener em
`0x6fed78 +14/+18/+1c`. A soma dos quadrados é guardada em binary64 e passa
pelo helper sqrt `0x636264`. Calcula no stack x87
`sqrt(distanceSquared)*6000.0f / secondary+28`, converte por `0x63604c` e
salva low32 em secondary+30 (=base+1a4, atenuação).

Esse ramo imediatamente chama ganho comum com `S32(oldRawGain-attenuation)`
e restaura oldRawGain em base+28. Portanto A espacial pode fazer uma
atualização de volume pela posição usando ganho antigo, seguida de outra
pelo `wrapper+40` com ganho novo. Não fundir essas operações ao alegar
fidelidade. O divisor/valores do listener e exceções x87 exigem validação
dos estados externos; não foi inferida uma política de divisão por zero.

No backend **comum**, slot+d0 aponta para `0x5f8909`, a rotina de carga de
recurso identificada na evidência de backend; não é o setter espacial. A
primeira fase A pode usar controller.ID26 anterior diferente de patch.ID26.
Nessa combinação, chamar wrapper+4c não prova um efeito físico de posição.
O recorte desta unidade guarda somente a entrada desse destino comum, sem
reabrir a carga/decodificação inteira.

## Exemplos derivados e limites

`arithmetic-examples.json` conserva etapas intermediárias, por exemplo:

| ID19 | ID18 | Ganho grupo | Duck / prioridade | Ganho ao wrapper |
| --- | --- | --- | --- | --- |
| 768 | 900 | 600 | 0 / não lida | 395 |
| 768 | 900 | 600 | 2 / 1 | 197 |
| 2047 | 900 | 600 | 0 / não lida | 1053 |
| -513 | 1024 | 1024 | 2 / 1 | -256 |
| 2097152 | 1024 | 1024 | 0 / não lida | -2097152 |
| 2147483647 | 1 | 1024 | 0 / não lida | 2097151 |

São exemplos sintéticos dos pedidos raw e do wrap observado, não medições
sonoras. Precisão bit a bit de toda a biblioteca matemática, modos x87
externos, erros COM, fontes de listener/globalGain e DSP/PCM físico continuam
fora da prova fechada. A fronteira inteira **controller→wrapper** está
separada dessas pendências e pode ser implementada sem inventar lei Q15.
