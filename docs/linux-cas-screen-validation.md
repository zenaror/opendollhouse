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
ao mapa. Ainda não confirma que a família recém-criada pode ser escolhida num
lote vago, entrar na casa, jogar, salvar e continuar em outro processo. Os
rótulos da tela CAS estão em inglês; escolher o idioma já controla a codificação
dos nomes e biografias, mas a localização visual do CAS ainda falta. Editar ou
excluir uma família também não está ligado.
