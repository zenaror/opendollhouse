# Controles HIT antes da execução do script

Evidência estática de2026-10-15, ainda sem implementação automática dessas
camadas. Mesma imagem analisada de hit-native-caller-arguments.md:
SHA2566e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2,
base0x400000 e offset=VA-base nos endereços citados dessa imagem. Equivalência
ao outro arquivo executável não comprovada. Nenhum executável foi executado;
exports anteriores e objdump atual, sem alterar projetos Ghidra ou dados.

HSM fornecido SHA256ca6bef035809b2cdacdc2c14a98582cd4c4702c53cc6640beceaa548c06d55b6.
Linhas97–102 nomeiam IDs16=h4,17=priority,18=vol,19=extvol,20=pan,21=pitch;
ID50=patch. Não importar para TS1 o comentário de variáveis TSO do Simitone
que chama0x10 de argstyle. ArgStyle nativo é campo separado em+0x10, getter
0x4df579. ODH endereça IDs numericamente, mas usa um split de storage diferente:
ID16 já é locals[0], IDs18/19/20 são locals[2/3/4].

## Defaults e Track

0x57cac1 cria banco global de31 valores a partir de ID17, base0x6f68f8 e
payload0x6f6914. Escritas0x57cb33–0x57cb51 estabelecem priority32,vol1024,
extvol1024,pan512,pitch3600. Alocação nativa de registros cobre IDs1–16
(0x57da20–0x57da4b); não confundir layout físico com IDs do interpretador.

Parser Track0x57b921 recebe registro de0x57b0f6, associado à string0x6cf440.
Terceiro campo CSV vai para EBX em0x57b9ed–0x57b9f2 e argumento3 do construtor
em0x57bb2d. Construtor0x57d7ed–0x57d7f4 escreve ID18=Track.volume.
Banco de controller usa0x80000000 como unset; getter vtable+0xb4 é0x57d8f5.
Inicialização0x57dd98–0x57dde8 copia defaults/controllers sem copiar o sentinela.

## ArgStyle2: ordem antes do primeiro tick

1. Argumentos brutos vão para IDs1–3; defaults/controllers são instalados.
2. 0x57de08–0x57de1b armazena argumento2 em ID19 e argumento3 em ID20.
3. 0x57de27 armazena argumento1 no campo de origem por0x57c257.
4. Final do initializer0x57decf chama vtable+0x58 ->0x57df27, pausando e
   retirando da fila de execução.
5. Caller0x5738ee ->0x57d873 sobrescreve ID19 com extvol espacial calculado.
6. Caller0x5738f8 ->0x57d899 sobrescreve ID20 com pan calculado.
7. 0x5738ff ->0x57df5c ativa e enfileira por0x57d310 ->0x580815.

Não há chamada ao interpretador nesse start antes dos setters. O primeiro
script precisa observar IDs19/20 pós-setters, enquanto R2/R3 brutos permanecem
zero nesse caller. R2zero não significa ganho final zero. Escritor0x57e150 pode
redirecionar ao provider compartilhado; local0x57e191 grava valor/flags e
recalcula controles para IDs18–20. Stores diretos de inicialização por0x5805fa
não executam esses efeitos. Shared provider ainda exige contrato próprio.

## Composição e fronteira com o backend

0x57fa52 lê controles. No domínio ordinário válido,0x57fb70–0x57fba7 compõe:

```text
external = extvol
se grupo associado tem level diferente de1024:
    external = trunc(group_level * external /1024)
effective = trunc(vol * external /1024)
se variável123 !=0 e duckpri < variável123:
    effective = trunc(effective /2)
```

Halving em0x57fbb4–0x57fbd0. IMUL é32-bit e divisão IDIV signed: não extrapolar
produtos grandes para aritmética matemática sem considerar wrap/traps x86.
0x57fbda envia inteiro bruto à vtable+0x40 do áudio. Isso demonstra composição
base1024, não conversão para Q15 ou lei de amplitude/dB do backend.

Branch não3D passa ID20 local em this+0x44 à vtable+0x48 em0x57fb56–0x57fb5d;
não é necessariamente o retorno do getter compartilhado anterior. Binding da
patch0x57f9c8–0x57f9d7 aplica volume/pan/pitch antes de NoteOn0x57f88e.
Caminho streamed reaplica controles em0x57f90a. Trechos selecionados em
reports/hit-native-controls-selected.asm.txt; sem teste dinâmico/áudio original.

## Espaço e lacunas

0x57320a calcula pan com centro512 e clamp0..1024, usando projected_x e width:
512+trunc((projected_x-trunc(width/2))*3072/trunc(width*100/2)). Spl0 mantém
pan512. Source<=0 escreve(1024,512) e retorna false; origem positiva ausente
ou certos componentes de posição zero retornam false sem definir toda saída.
Não fabricar contexto válido a partir de uma falha dessas.

0x57488f agrega fontes registradas: máximo extvol; pan é512 mais média dos
desvios ponderada por extvol, truncada; peso total zero produz512. Geometria
vem de offsets+0xf8/+0xf4/+0x110 por0x570f1d; projeção/visão de0x570f5d,
0x570d77,0x570d65,0x570d8d. Unidades e semântica completas da visão pendentes.

Próximo contrato implementável: inicialização raw de vol/extvol/pan antes do
primeiro tick, com recibos e inputs explícitos para o contexto ainda ausente.
Derivação espacial fiel precisa desses providers, agregação, grupos/ducking e
escala real do backend. O controle DSP atual permanece diagnóstico.
