# Percurso, VM e ticks — 2026-10-13

`odh_routing` liga a primitiva45 à busca de percurso e ao World em um perfil
inicial. O estado muda ao longo de ticks, com conclusão da espera da VM quando
chega ao destino. Ainda faltam snap, animação, scheduler geral e fidelidade de
roteamento original; a meta Linux funcional permanece aberta.

## Estado e contrato

World tem posição opcional (x/y inteiros, nível positivo e direção 0..7), com
unidades de 1/16 tile no adaptador. Posições fora da faixa, entidades ausentes
ou vinculadas a um recipiente recusam mudança. Posição não integra ainda todos
os campos brutos, flags espaciais, multi-tile ou dados de lote do TS1.
Cada entidade recebe uma geração monotônica; reutilizar ID não faz um job
antigo mover a nova entidade.

O callback recebe identidade da VM e ticket antes de planejar. Um controlador
liga-se a uma VM de endereço estável e recusa outra origem. Planejar não move
o avatar. Um tick consecutivo aplica um segmento válido; duplicatas, passado
e lacunas de ticks são recusados. O chamador deve processar ticks em ordem.
Na chegada aplica facing/person data37 e conclui o ticket uma vez.

Antes de cada passo, verifica ticket, gerações, posições esperadas, vínculo de
ocupação e próximo segmento na malha. Restart, exclusão, recriação de ID,
movimento externo ou novo obstáculo cancelam o percurso. Falhas de percurso
retornam false neste protótipo; árvores de falha do original não estão ligadas.
Observers recebem eventos após a mudança, não podem reentrar no controlador;
exceções de observers não revertem movimento já aplicado. Todos os objetos
referenciados devem sobreviver às chamadas; não há sincronização entre threads.

## Perfil provisório e lacunas

Operando45: Global scope2, flags e tail zero. O SLOT tipo3 é selecionado por
data. O perfil requer versão10, offsetzero, círculo min=max=optimal positivo,
flags0x55, standing1/sitting0/ground0, snap-1, height0, facing-2, resolution16,
maxsize/i10 zero e gradientbits0x3e400000. Outros layouts ou regras falham
explicitamente como unsupported.

Gera oito destinos com diagonal inteira baseada em 46341/65536; usa menor
custo de malha, com tie determinístico. Direção/entry flags derivam dos
octantes. Essa aproximação não replica completamente floor/rotação, scores,
aleatoriedade e regras de rooms do FreeSO/original. Não alegar equivalência.
Uma unidade de movimento por tick, inclusive diagonal, é velocidade ODH
provisória; não é tempo/velocidade de caminhada TS1 comprovados.

A máscara da malha é fornecida explicitamente e deve considerar clearance.
Não há carregamento ou cálculo dos footprints originais. Busca é síncrona;
limite esgotado é diagnóstico, não retorno false de destino inalcançável.
Obstáculo novo cancela; replanejamento, concorrência entre Sims, portais e
reservas de destino ainda precisam ser implementados.

## Evidência

CTest12/12 passou em Debug e ASan/UBSan. A suíte routing tem 39 checks em
mundos construídos: espera/avanço, relógio, chegada, restart, obstáculo novo,
entidade movida/removida/recriada, profiles unsupported, nível, outro VM e
reentrada. A CLI usa FAR/IFF/BHAV/SLOT sintéticos para testar a integração.
LSan permanece não validado (`detect_leaks=0`, ptrace).

Probe com `DiningChairs.iff`/BHAV1001 e `Global.iff`/SLOT100 reais, identidades
registradas em `slot-validation.md` e `world-validation.md`. O cenário ODH tem
avatar(16,64), cadeira(64,64), nível1, malha128x128. A região55..73 de ambos
os eixos é bloqueada; parede entre x39/40 com abertura y50..55. Essa geometria
é construída e não vem de um lote/save original.

Resultado observado: 33 ticks/movimentos até(48,64), direção2, entryflags64.
Cada passo tem deslocamento máximo de uma unidade, evita a região bloqueada
e cruza a parede pela abertura. A VM retoma, executa11instruções totais e
para em BHAV1003/PC19, `unsupported primitive 46` (snap), com três frames.
Posição/person data mudaram; sentar/levantar completo ainda não ocorreu.

JSON Debug e ASan/UBSan idênticos, stderr vazio, saída3 esperada. Reports
ignorados `reports/chair-route-probe.json` e `reports/chair-route-probe-sanitize.json`.
Nenhuma execução do Sims.exe.bak, janela, animação ou teste de hardware.

Próximas etapas: decodificar/resolver SLOT privado da cadeira para snap46,
ligar animação44, sprites/paletas e composição Linux. Ampliar e comparar a
política de roteamento, clearance e tempo contra o original, sem promover
este percurso construído a prova de fidelidade.
