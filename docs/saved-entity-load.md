# Ação de carregamento dos objetos salvos

`run_saved_entity_load` implementa em C++ a chamada síncrona da entrada 2,
“Carregar”, sobre um candidato privado. A operação parte da execução restaurada
do objeto; não executa suas entradas de criação, inicialização, reinicialização
ou início de Main.

O chamador fornece uma VM descartável com os programas exatos dos recursos,
a VM da execução salva e um tratador de ações síncronas. A chamada usa o próprio
objeto como chamador e alvo, nenhum objeto na pilha e quatro argumentos zero.
Ela copia os registradores temporários da execução restaurada para a chamada,
sem devolver mudanças nesses registradores. As alterações explícitas nos dados
do objeto permanecem no candidato.

A função exige estar fora de um despacho ativo da VM. A pilha, posição de
execução, espera pendente e registradores da Main restaurada permanecem iguais.
Tanto retorno verdadeiro quanto falso encerram normalmente a ação, pois o
chamador da referência ignora esse resultado. Ação ausente ou de número até
255 é ignorada. Código não resolvido, espera assíncrona, falha ou orçamento
esgotado impedem completar a preparação. Efeitos anteriores à falha não são
desfeitos: o candidato privado deve ser descartado.

O chamador também deve garantir que a simulação esteja fora do processamento
de um passo completo. A checagem de despacho impede reentrada de instruções;
não identifica, sozinha, o intervalo entre dois objetos no mesmo passo.

## Evidência e escolha do caminho

Foram consultados os objetos Git do Simitone alexjyong
`e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab` e FreeSO
`7e76c1960252ddd033d8937d5edb47d326f5b69c`:

- `Simitone.Client/UI/Screens/TS1GameScreen.cs`, `InitializeLot` e
  `BlueprintReset`, escolhem os caminhos de carregamento do jogador local.
- No caminho IFF, `VMBlueprintRestoreCmd.cs`, linhas 34–66, restaura FSOV ou
  converte o IFF, chama a entrada 2 dos objetos e depois verifica a família.
- `VMEntity.cs`, linhas 589–606 e 623–625, monta essa chamada;
  `VMThread.cs`, linhas 92–124, usa uma execução temporária síncrona. Fora do
  processamento normal, os registradores são copiados.
- O caminho preferencial `LocalHouse/*.fsov` em `TS1GameScreen.cs`, linhas
  811–821, faz `vm.Load` e `vm.Reset` sem essa chamada de entrada 2.
  O `VM.Reset` desse caminho retira avatares e limpa estados específicos;
  não reinicia indiscriminadamente as execuções dos objetos restantes.

“Reset” também pode significar `VMEntity.Reset`, que limpa a execução de uma
entidade e chama as entradas 3 e 1. É uma operação distinta. Não há evidência
de que deva substituir o carregamento de todos os objetos retidos na casa.

O valor global da família pode ainda ser o salvo durante a entrada 2 no caminho
IFF; a verificação da família vem depois. O helper não altera esses valores.
A escolha do caminho, a ordem dos callbacks e a aplicação da família precisam
estar fixadas no perfil de ativação, com suas próprias evidências.

## Validação e integração restante

O teste `object_load` cobre os contextos e argumentos, alterações explícitas no
World privado, preservação de dados brutos e bancos importados, cópia de TEMP e
TEMPXL, retorno verdadeiro/falso e identidade byte a byte da Main em espera.
Também cobre código ausente, reutilização da VM descartável, despacho aninhado,
espera, falha e limite de instruções.

Esses testes usam programas sintéticos e recursos explicitamente fornecidos.
A função ainda não está ligada automaticamente à entrada de outra família nem
ao leitor normal de todos os saves. O próximo perfil deve validar e reconstruir
as continuações dos objetos retidos e a chamada de carregamento apropriada,
excluindo as ordens e execuções dos moradores que saíram. O arquivo estrutural
atual continua sem autorização de ativação ou reembolso.
