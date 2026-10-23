# Relógio e necessidades em House07

Este checkpoint liga um relógio de jogo ao ciclo real de execução Linux e salva
o estado necessário para continuar a mesma simulação. É uma fatia de gameplay
validada com House07; ainda não representa uma sessão jogável completa.

## O que foi ligado

- O perfil TS1 consultado no Simitone usa 30 passos de simulação por minuto e
  atualiza necessidades em intervalos de dois minutos. A referência é Simitone
  `4906db3156616bfaffc10ce0963cf08062975fef`, submódulo FreeSO
  `f86bbd34b2112296ed786741f640a0535886d180`, em
  `refs/upstream/Simitone/FreeSO/TSOClient/tso.simantics/VMClock.cs` e
  `.../Entities/VMTS1MotiveDecay.cs`. Isso documenta o Simitone, não prova
  equivalência com o executável original.
- A hora avança junto com os passos não pausados. As variáveis globais 0, 1, 4,
  5, 6, 7 e 8 recebem hora, dia, o valor de hora-do-dia de referência, minuto,
  segundos, mês e ano. O título da janela mostra a hora e os sete valores de
  necessidade do morador ativo. O checkpoint de HUD abaixo também os desenha
  nos pixels da cena após a composição do lote.
- A janela Linux atualmente agenda passos a cada 40 ms (25 por segundo). Com
  essa meta, um minuto do jogo dura cerca de 1,2 segundos reais, se o computador
  acompanhar o ritmo; isso fica aproximadamente 17% mais lento que o ritmo
  nominal de um minuto real por minuto de jogo descrito pelo Simitone. Sob carga,
  a simulação pode avançar mais devagar. Essa velocidade ainda não foi medida
  numa janela gráfica validada.
- Fome, conforto, higiene, bexiga, energia, diversão e social usam as fórmulas
  padrão dessa referência e guardam suas frações por morador. O valor de sala é
  conservado do motivo 13; o cálculo ambiental do quarto ainda não existe. O
  host também não implementa controladores `SetMotiveChange`; perfis que não
  podem ser representados com segurança são identificados como não suportados.
- Saves novos com relógio incluem a seção obrigatória `SIMS` versão 1. O save
  guarda a hora e as frações por morador. Saves antigos ainda abrem, mas sem
  `SIMS` a simulação de relógio/necessidades fica desativada porque seu estado
  anterior não pode ser reconstruído.

## Resultado real reproduzível

Com os dados externos de The Sims instalados, rode da pasta do repositório:

```sh
python3 tests/house07_clock_cli.py build/linux-checkpoint/odh-person "../referencias/The Sims"
```

O gate sem tela conferiu House07, morador vizinho 48, seus dois moradores e os
arquivos originais protegidos por hash. Ambos os moradores, IDs 105 e 137,
receberam atualizações. O primeiro passo terminou em 7:01; no
passo 31, o relógio estava em 7:02. Após 331 passos, os dois moradores tinham
processado atualizações, pelo menos uma necessidade havia mudado, e a execução
que salvou/retomou produziu o mesmo estado de relógio e necessidades que uma
execução contínua. A seção `SIMS` estava presente e os arquivos comerciais
continuaram byte a byte iguais.

House07 contém `seconds/global6 = 22`. O ativador fixado do Simitone multiplica
esse valor por 30 e o relógio zera a fração ao avançar o primeiro passo; por
isso, a observação desse lote é 7:00 → 7:01 no primeiro passo. O teste preserva
essa regra observada da referência e não a converte em 22 segundos por
suposição. O timing do jogo original ainda precisa de comparação independente.

Os testes C++ focados passaram 11/11 após incluir uma regressão para as
variáveis de relógio no save/resume. O gate real acima é headless; ele não
valida a aparência do título da janela, o uso físico de pausa, autonomia dos
Sims, construção/compra, outras casas ou destinos de expansão.


## HUD numérico em cena — 2026-10-23

`tools/status_hud.hpp` desenha um painel numérico portátil no canto superior
esquerdo, depois da composição do lote e antes dos sinks de janela/PPM. O painel
mostra relógio HH:MM, ID do morador ativo e sete linhas com índice, valor assinado
e barra. As linhas 1–7 correspondem a fome, conforto, higiene, bexiga, energia,
diversão e social, nessa ordem (`Ts1Need`, índices de motivo 7, 6, 8, 9, 5, 15,
14). A barra limita visualmente -100..100; o número preserva o valor bruto.
Valores indisponíveis aparecem como traços, inclusive o relógio de uma sessão
antiga sem `SIMS`; não se inventa estado para preencher o painel.

Este é um HUD de diagnóstico sem rótulos linguísticos, sem proposta de tradução
final ou de correspondência visual com o jogo original. Os algarismos usam
máscaras próprias e não dependem de Pango/Cairo, fontes de sistema ou assets
comerciais. O build validado tinha X11 compilado e PangoCairo indisponível:
o HUD funciona nesse perfil. A faixa de ação existente continua no caminho de
janela quando o renderer de texto opcional está disponível; fica na região
inferior e não sobrepõe o painel de status. Neste build, a faixa de texto está
desabilitada pela dependência ausente; o título continua disponível. Não houve
validação gráfica da faixa ou da janela neste checkpoint.

A pintura só escreve os pixels RGBA. Não escreve World, necessidades, relógio,
profundidade, contadores de rasterização, owner IDs nem mapas de picking. Não
cria uma nova região interativa: cliques seguem usando os mapas já compostos.
O morador é lido de `active_household_actor` em cada captura, inclusive quando
for diferente do morador que ancora a câmera/playback.

O gate real reproduzível usa a instalação externa, sem abrir GUI:

```sh
python3 tests/house07_status_hud_cli.py build/linux-checkpoint/odh-person "../referencias/The Sims"
```

A opção integrada `--headless-status-frame PATH --headless-status-tick N` é
explícita e restrita a `--headless-ticks`, com N em 1..limite; plano, viagem,
par incompleto, zero e tick acima do limite são rejeitados. O destino deve ser
novo, seguindo a proteção existente do exportador PPM. N é relativo ao início
da execução atual: após retomar tick 31, N=30 captura tick absoluto 61. O mesmo
significado relativo passa a valer para `--render-tick` no runtime de baixo
nível; em uma execução nova o tick inicial é zero. Sem amostra de Playback no
tick pedido, a execução falha explicitamente e não alega captura concluída.

Resultado observado: House07, vizinho 48 / ator 105, capturado no tick 31 às
07:02, com valores `[99,98,99,71,100,20,99]`. O save live foi retomado e capturado
no tick 61 às 07:03, com os mesmos sete valores naquele intervalo. O gate
verificou os pixels de todos os algarismos do relógio, ID e sete linhas contra
o estado reportado. A simulação após retomar com captura coincidiu com 61 ticks
contínuos sem captura. Configuração e save ficaram temporários; DISPLAY e
WAYLAND_DISPLAY foram removidos. Hashes antes/depois de House07, Neighborhood,
dois personagens e FARs Global/Animation/Objects permaneceram iguais, inclusive
no bloco de finalização do teste. Nenhum recurso comercial foi versionado.
Os SHA-256 do RGB completo foram
`40aa0cd9b090b26f9d530de1362c0e23d4c1a76a4cb6ebf8aecb6ab85d1d2619` (tick 31) e
`8410de68113c3d234ec0e10f3de99c3e8d1c2f4296be9d146bbdb23a6febd1ea` (tick 61).

CTest focado passou 6/6 (`status_hud`, `ts1_simulation`, `session_bundle`,
`linux_game_entry_cli`, `linux_household_session_resume_cli`,
`linux_session_resume_cli`). O teste de HUD contém nove checks sintéticos de
limites, preservação de owner/depth/contadores, barras e área da faixa de ação.
O gate real prova pixels PPM e estado headless nesse perfil House07; a janela
recebe o mesmo frame pelo ponto comum de composição, mas não foi aberta ou
medida. Seleção física de outro morador, pausa, outras casas, paridade visual e
runtime em consoles continuam sem validação neste checkpoint.
