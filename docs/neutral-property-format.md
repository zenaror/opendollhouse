# Arquivo estrutural de propriedade desocupada

O formato nativo da propriedade guarda o resultado da preparação da casa
separadamente do save da família. O codificador recebe somente um
`PreparedPropertyCheckpoint`, cuja origem foi conferida contra o salvamento
confirmado da ocupação. Não aceita um World arbitrário como autorização
de saída ou reembolso.

Este primeiro perfil é **estrutural**. Não inicia a simulação nem confirma
a mudança de família. Uma versão que permita ativação precisa de um perfil
obrigatório próprio, reconhecido explicitamente pelo leitor.

## Conteúdo e leitura

O contêiner de arquivos nativos leva exatamente cinco seções obrigatórias,
com limites de tamanho, comprimentos explícitos e checksums:

| Seção | Conteúdo |
| --- | --- |
| `NPRP` | Versão e perfil, casa, ocupação de origem, salvamento e histórico esperado da vizinhança |
| `META` | Idioma, expansões e identidade do conjunto de recursos |
| `WRLD` | Dados atuais dos objetos que ficam, posições, conteúdo e gerações |
| `RELA` | Relações preservadas entre objetos e vizinhos |
| `NOWN` | Grupos completos, reservas de números e origens de criação e retirada |

O leitor exige as identidades esperadas dos recursos e da casa instalada.
Confere os grupos, a ausência de avatares e objetos temporários da interface,
as relações atuais, as reservas e a consistência das origens. A reconstrução
com as definições continua necessária para identificar todos os tipos de ator.
Grupos sem mestre só podem ter um objeto. Seções desconhecidas, de execução,
ausentes ou tratadas como opcionais são recusadas neste perfil. O leitor
normal de saves de família também recusa esse arquivo.

A arquitetura exige seleção explícita do perfil de casa instalada **sem
alterações de construção**. Não há retorno automático a essa arquitetura se
dados editados estiverem ausentes. Arquitetura editada precisa de representação
própria. A revisão e o arquivo anterior da propriedade podem ser fixados como
observação do chamador; isso ainda não comprova a autoridade dessa observação
no histórico persistente.

## Validação e próximos limites

O teste `property_checkpoint` verifica escrita e leitura com objetos preservados,
criados, retiradas anteriores e nenhum objeto restante. Cobre outra casa ou
conjunto de recursos, corrupção, limites, versões e seções incorretas e
agrupamento inválido de objetos independentes. A revisão independente encontrou
esse último caso, que foi corrigido e recebeu teste de regressão.

Os testes são sintéticos e passam pelo formato normal do salvamento da família
antes da preparação. Ainda não publicam uma casa desocupada no jogo. O arquivo
não contém filas, pilhas de execução, serviços ou callbacks da família anterior.
Números arbitrários nos dados dos objetos são preservados.

Faltam a [política de vínculos](property-reference-audit.md) e reconstrução de
[execução e carregamento](saved-entity-load.md),
a arquitetura editada, o armazenamento imutável e a confirmação conjunta da
propriedade, saída da família e dinheiro. Consulte o
[projeto de persistência](native-property-persistence-design.md).
