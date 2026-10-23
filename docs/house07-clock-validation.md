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
  necessidade do morador ativo.
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
