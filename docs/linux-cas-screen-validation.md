# Tela CAS Linux

## O que está ligado

Na vizinhança, a tecla `C` abre o criador de família. A tela coleta o nome da
família, nomes e biografias dos Sims, gênero, idade, tom de pele, escolhas de
cabeça e corpo disponíveis no catálogo real e cinco atributos de personalidade.
O botão de salvar converte cada escolha para os dados do personagem e grava os
IFFs em `XDG_DATA_HOME/opendollhouse/Characters` (ou no caminho equivalente em
`~/.local/share`). Família e moradores vão para o arquivo auxiliar vinculado
à identidade de `UserData/Neighborhood.iff`. Nenhum arquivo comercial é
reescrito.

A conversão foi conferida com `People\TemplatePerson.iff` de `Objects.far`:
o STR# 200 original tem 27 entradas, e o conversor agora só substitui os campos
usados pelo fluxo CAS do Simitone, incluindo corpo, cabeça e mãos. Isso evita
escrever fora dos limites do arquivo-modelo.

## Verificação visual e gravação

Build usada: `/tmp/odh-family-overlay-build`, commit-base
`42982f5767ec019cddd43e19318c9e34c64a58cd`, mais as alterações locais desta
etapa. A build completa passou. Depois da correção final dos índices, o CTest
completo executou 237 casos: 234 passaram, nenhum falhou e três testes de
janela foram ignorados porque o CTest não estava rodando num display.

Um Xvfb temporário mostrou a vizinhança e abriu o CAS com `C`. O teste digitou
`ODH Test Family` e `Test Sim`, salvou e voltou à vizinhança. O perfil temporário
continha `User00001.iff` (8.842 bytes) e um overlay de família (701 bytes).
O processo reportou cancelamento normal depois que Escape fechou a vizinhança;
o `xdotool` observou `BadWindow` porque tentou terminar o envio da tecla após a
janela desaparecer. Os arquivos de teste foram criados apenas sob `/tmp`.
Xvfb foi parado ao fim de cada execução e nenhuma tela virtual ficou ativa.

## Limites e próximo gate

Esta verificação confirma a tela, entrada de texto, gravação privada e retorno
ao mapa. O gate real de criação até entrada está documentado abaixo; ele ainda
não confirma janela gráfica, execução do lote, save/retomada ou gameplay. Os
rótulos da tela CAS estão em inglês; escolher o idioma controla a codificação
dos nomes e biografias, mas a localização visual do CAS ainda falta. Editar ou
renomear uma família também não está ligado. A remoção lógica de famílias
desocupadas elegíveis foi ligada separadamente no seletor; seus limites estão
em [family-overlay-linux-validation.md](family-overlay-linux-validation.md).

## Gate real de família criada até entrada do lote

Em 2026-10-25, o gate opt-in `tests/cas_movein_e2e.py` usou a instalação externa
`../referencias/The Sims` com perfil XDG temporário. Leu o catálogo CAS e
`People\\TemplatePerson.iff`, criou uma família privada com um adulto, escolheu
o primeiro lote vago válido (House01, preço SIMI 10.500), gravou a mudança no
overlay privado e resolveu a entrada com `load_session_entry`. Em seguida,
`odh-person --plan-only` retornou `ready`, o GUID esperado e 132 arquivos de
recursos. O gate calculou SHA-256 de 2.699 arquivos comerciais antes/depois;
nenhum mudou. O perfil temporário foi removido ao terminar.

Isso fecha o caminho de dados CAS → compra → overlay → plano de entrada. Ainda
não testa a tela CAS ou a seleção com mouse/teclado, não executa o runtime da
casa, e não valida save/retomada nem uma família com vários moradores. O teste
é opt-in porque precisa de uma instalação externa e não entra no CTest comum.
Como executar: ver o comando `tests/cas_movein_e2e.py` no README.
