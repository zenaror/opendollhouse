# Main do avatar, semiglobal e idle

Checkpoint de 2026-10-13. O carregamento explícito do código do avatar agora
valida seu vínculo GLOB128 com o semiglobal. A investigação identifica o
mecanismo de idle que falta ao probe Linux. Não altera a pose ao terminar Stand,
nem declara executado o Main completo do original.

## Recursos e carregamento

| Recurso externo | Bytes do IFF | SHA256 do IFF |
|---|---:|---|
| Objects.far: `People\TemplatePerson.iff` | 8568 | `3b602335406c772fc9ec2b081daf6e7248d0d07795658ec06be85322bb71ca2e` |
| Global.far: `PersonGlobals.iff` | 383543 | `6bb4d73b3f955a055f612b5e52f0db957f228a771df09c6d59dd5890430ee0cc` |

O TemplatePerson contém OBJD128/version138, GUID `0x7fd96b54` e Main4096.
GLOB128 tem payload256: comprimento13, nome `PersonGlobals`, extensão restante
242 bytes. Os quatro BHAVs ativos são4096,4097,4098,4100; chunks XXXX não são
rotinas executáveis. PersonGlobals contém473 BHAVs ativos.

`read_glob` preserva bytes do nome e a extensão restante. O perfil padrão é
Pascal-u8, com comprimento estrutural0..255; o perfil C-string é selecionado
explicitamente, exige terminador dentro do View e limita o nome a256 bytes.
Não há detecção heurística, conversão de encoding ou normalização de caminho.
Isso difere da heurística da referência: FreeSO lê Pascal se o primeiro byte
for menor que48, caso contrário lê C-string; seu writer emite C-string.
O perfil Pascal foi observado neste recurso nativo, não validado para todo o
corpus. Fonte fixa: [GLOB.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.files/Formats/IFF/Chunks/GLOB.cs)
e [IoBuffer.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.files/Utils/IoBuffer.cs).

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' 'People\TemplatePerson.iff' 4096 --semiglobal '../referencias/The Sims/GameData/Global/Global.far' PersonGlobals.iff --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff
```

A seleção das entradas FAR continua exata. `--semiglobal` exige um único
GLOB128 Pascal não vazio no code owner, compara seu nome mais `.iff` ao seletor
com ASCII casefold e rejeita separadores, NUL e nomes de caminho especiais.
Somente BHAVs privados são aceitos no owner desse modo; somente semiglobais
no recurso ligado; somente globais em `--global`. O JSON registra os dois
arquivos, GLOB/encoding/nome/extensão. Não busca arquivos automaticamente.

A VM ainda tem um único banco privado por conjunto de rotinas. Isso permite
inspecionar o avatar com seu semiglobal, mas não juntar privados do avatar e
da cadeira, que ambos possuem4096. Frames com code owners distintos e recursos
por owner são trabalho pendente para a queue geral.

## Contrato da referência e estrutura nativa

Referência FreeSO fixa `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL2.
As interpretações de operands abaixo seguem esse perfil; o executável original
não foi observado executando esses caminhos.

Na referência, a inicialização de [VMEntity](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Entities/VMEntity.cs)
executa Init e entrada8, e empilha Main. [VMAvatar](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Entities/VMAvatar.cs)
cria a lista de animações vazia: seu Tick avança estados existentes, sem trocar
um loop apenas porque Posture mudou. Não foram encontrados símbolos C#
`AnimateIdle` ou `DefaultAnimation` nesses arquivos.

[VMThread.Pop/EndCurrentInteraction](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Engine/VMThread.cs)
remove a interação e retoma o Main preservado em
[IdleForInput/AllowPush](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Primitives/VMIdleForInput.cs).
Não há reset universal da animação na saída. O routing tem seu próprio
standing loop e limpeza de misturas em
[VMRoutingFrame](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Engine/VMRoutingFrame.cs).

| Estrutura nativa | Caminho relevante |
|---|---|
| Template Main4096 | pc0 prepara StackObject; pc1 chama8193 e repete pc1 no TRUE |
| Person8193 | pc4 chama8283 e repete pc4 no TRUE |
| Loop8283 | pc6 chama8282/check queue; em caminhos condicionais pc16 chama8330/do new adult idle |
| Check queue8282 | pc0 chama global281/AllowPush0; quando TRUE, pc4 chama8292/reset idle antes de AllowPush1 em pc1 |
| Retomada8282 | ambos destinos de pc1 seguem pc3 (attr6=0), depois pc11 (attr5=0) e TRUE |
| Idle8330 | pc2 testa Swimming64; pc26 testa Posture==1 e chama8229 quando sentado; em pé segue para seleção de attr5 |
| Seleção em pé8330 | attr5<4: random attr5 com limite3, soma4; estados4/5/6 chamam8331/8333/8332 |
| Primeiros clips de standing idle | 8331 pc2 ID145; 8333 pc14 ID208; 8332 pc2 ID251, source3, flags32 |

GlobalSTR156 IDs145/208/251 nomeiam respectivamente
`a2o-idle-neutral-Lstand-breathe-1a`, `a2o-idle-neutral-Mstand-breathe-1a`,
`a2o-idle-neutral-Rstand-breathe-1a`. Há branches aleatórios anteriores a essas
instruções. GlobalSTR150[3] é `a2o-standing-loop`.

8292/reset idle não contém44 ID0. Posture==1 ou attr5==0 conduz à limpeza de
attr5 e PersonData23/22; outros estados podem executar clips de transição.
8229 também não contém44 ID0. Um reset explícito existe em8289/transition to
idle: pc0 opcode44, operands `[0,0,0,0,2,0,0,0]`, ambos destinosFE. Seus pc1..4
não são alcançáveis desde a entrada. Um caminho condicional de mood passa por
8275 e8289; a chamada8330→8229 fornece Param0=0 e não toma esse caminho.

[VMAnimateSim](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Primitives/VMAnimateSim.cs)
ID0 usa postura1/2, demais→índice3, sem escrever Posture. A tabela base adulta
é GlobalSTR150, infantil151; na referência STR150 privada do avatar sobrescreve
entradas não vazias. O resolver atual do probe só fornece a tabela global,
limitação que continua explícita.

Assim, o sit-loop após Stand isolado é consistente com perder o contexto do
Main/rota: o reset anterior à escrita Posture0 instala o loop sentado, e a
escrita isolada não o substitui. Essa explicação é inferência apoiada na
referência e nos bytes nativos, não prova de execução do original. A integração
correta requer retomada do Main e seus pedidos de animação, com seus estados e
recursos; não há evidência para reset incondicional após qualquer interação.

## Validação e próximo gate

- CTest Debug e ASan/UBSan:37 testes passam; um teste de janela X11 é
  skipped por acesso ao display neste sandbox. A janela não foi alterada nem
  revalidada nesta unidade. LSan desativado (`detect_leaks=0`), sem alegação
  de validação de vazamentos. Dump nativo ligado8193 idêntico entre os builds.
- Leitor:291 checks sintéticos de truncamento, limite, encoding explícito,
  subview, extensão e bytes brutos; nenhum fixture comercial versionado.
- CLI: chamada sintética privado4096→semiglobal8192→global256 retornaTRUE em
  três dispatches. Ausência/duplicação/truncamento de GLOB, vínculo divergente,
  caminhos, scopes inválidos e duplicação de opções/rotinas são rejeitados.
- No corpus externo, dumps ligados4096/8193/8283/8289/8330 são integralmente
  iguais aos dumps diretos, retirando apenas o novo objeto de procedência JSON.
- `--run --world-probe` no Main4096 ligado para após4 dispatches, trace de
  rotinas4096,4096,8193,280, com `unsupported primitive 0`. O mundo é o fixture
  genérico do CLI, não um avatar carregado integralmente. Nenhum sucesso de
  inicialização/Main é alegado.
- O cenário anterior cadeira/Sit/hold120/Stand/post240 permanece94 dispatches,
  662 ticks, Comfort3; todos os campos JSON da simulação são iguais à baseline.

A próxima unidade precisa modelar code owner nos frames/bancos e a continuação
Main/AllowPush, depois ampliar Sleep/RNG/escopos/resolução Misc e recursos do
avatar em gates limitados. O idle em pé, a inicialização completa, a comparação
visual com o original, lote/save e consoles permanecem pendentes.
