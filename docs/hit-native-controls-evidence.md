# Controles HIT antes da execução do script

Evidência estática de2026-10-15. A inicialização com entradas espaciais explícitas
está sendo integrada; derivação geométrica e escala do backend continuam abertas.
Mesma imagem analisada de hit-native-caller-arguments.md:
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
payload0x6f6914. O construtor0x580629 preenche inicialmente com INT32_MIN,
mas o startup0x57cb33–0x57cc0a sobrescreve todos os31 valores. Banco final,
em ordem de ID17 até47:

```text
32,1024,1024,512,3600,0,0,0,0,0,0,100,10000,0,0,0,
0,0,0,0,0,0,1,0,0,0,0,0,0,0,0
```

EBX é zerado em0x57ca4e e preservado para os stores zero. O store0x57cc10
em0x6f6990 já está fora do banco. Alocação nativa de registros cobre IDs1–16
(0x57da20–0x57da4b); o allocator0x580558 zera todos em0x580586–0x580591.
Isso prova estado fresco, não reset de threads reutilizadas. Não confundir
layout físico com IDs do interpretador.

Parser Track0x57b921 recebe registro de0x57b0f6, associado à string0x6cf440.
Terceiro campo CSV vai para EBX em0x57b9ed–0x57b9f2 e argumento3 do construtor
em0x57bb2d. Construtor0x57d7ed–0x57d7f4 escreve ID18=Track.volume;
0x57d7e1–0x57d7e8 escreve ID25=Track.ducking_priority. A assinatura observada
é trackID,name,volume,ArgStyle,duckpri,group,Spl,hitlist/entry,patch.
Banco de controller usa0x80000000 como unset; getter vtable+0xb4 é0x57d8f5.
Inicialização0x57dd98–0x57dde8 copia defaults/controllers sem copiar o sentinela.
Isso também vale para os valores de Track.volume/ducking_priority armazenados
nesse banco: se forem0x80000000, a comparação0x57dda9/0x57dddb salta ao
próximo ID e preserva defaults18=1024/25=0. Não instalar INT32_MIN nesses
locals só porque a palavra bruta de Track possui esses bits. A regra não
estabelece por si só o tratamento de ID50, fora do banco17–47.
O controller ID50 retém Track.patch_id bruto: store0x57d791–0x57d794 no
controller e0x57f979–0x57f981 no thread. Um hitlist pode escolher outra patch;
essa escolha não prova que ID50 deixou de ser zero. Spl aparece no controller
ID57, mas a visibilidade completa desse valor no thread ainda não foi provada.

## ArgStyle2: ordem antes do primeiro tick

1. Argumentos brutos vão para IDs1–3; defaults/controllers são instalados.
2. 0x57de08–0x57de1b armazena argumento2 em ID19 e argumento3 em ID20.
3. 0x57de27 armazena argumento1 em ID47, source, por0x57c257. O endereço
   this+0xb0 corresponde a payload this+0x38+(47-17)*4; não é banco separado.
   0x57deab–0x57debe escreve ID46,isplaying=1.
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

O escritor local também define zero/positive/sign em0x57e199–0x57e1be antes
de armazenar ou recalcular. Como o setter de pan é o último antes de ativar,
o estado fresco com provider próprio entra no script com zero=(pan==0),
positive=(pan>0), sign=(pan<0). O interpretador ODH representa zero/sign.
Manter ambos falsos erra o caso pan=0. A ativação verificada muda pausa/fila,
sem reescrever esses flags. Provider compartilhado/reutilização não cobertos.

ID18 é volume no perfil nativo: escrita0x57e1ea–0x57e20c recalcula controles.
Rebinding de patch usa ID50, pelo caminho0x57e2e1–0x57e331. O alias histórico
18/50 do perfil FreeSO deve continuar restrito ao perfil legado.

O Track3410 chair_sit_metal tem volume1024,ArgStyle2,duckpri10,group1,Spl20,
patch0 e hitlist tkd_chair_sit_metal. Portanto sua inicialização deve guardar
ID25=10 e ID50=0 mesmo quando a escolha inicial seleciona uma XA não zero.
Trechos adicionais e identidade reverificada em
reports/hit-native-controls-init-selected.asm.txt.

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
