# Assinaturas de código e recuperação de objetos salvos

## Identidade e limite da evidência

Investigação de 2026-10-25, motivada pelos oito frames com rotinas privadas ausentes ao
entrar na House07 com o Simitone. Perfil nativo fornecido: `Sims.exe`, SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`,
base PE `0x400000`. Os endereços abaixo são VA, não offsets de arquivo.
A evidência é análise estática desse perfil; não é uma comparação executada
com o original nem prova de que outras revisões têm o mesmo código.

O projeto Ghidra `reports/ghidra/projects/ODH_TS1_Supplied`, programa `Sims.exe`,
foi consultado com `-readOnly -noanalysis`. Exportações e hashes ficaram em
`reports/ghidra/objt-version-audit/manifest.json`, ignorado no Git. Nenhum
original ou projeto de análise foi alterado. O comando de reprodução usa:

```sh
/home/rafael/Tools/Open-GBP/ghidra_12.1.3_PUBLIC/support/analyzeHeadless \
  reports/ghidra/projects ODH_TS1_Supplied -process Sims.exe \
  -readOnly -noanalysis -scriptPath reports/ghidra/scripts \
  -postScript ODHFunctions.java /tmp/odh-objt-native-functions \
  0043a5fb 00457759 0045778a 00402741 0040204f 004020a6 \
  00431edd 00444346 0043206d 00431c90 004331d4 004333ef 0045e8fd
```

## Comparação nativa

`ObjectSaveTypeTable`: RTTI `0x6c3b18` → COL `0x68b7a8` → vtable
`0x66702c` → thunk `0x43f202` → serializador `0x43a5fb`.
Ao ler versões OBJT a partir de 1, o serializador compara **dois DWORDs
completos**, associados às entradas Init (0) e Main (1). Qualquer diferença
ativa o bit `0x2` em tipo+`0xb4`. A House07 observada tem OBJT versão 2.
O ramo OBJT anterior a 1 é diferente e não foi implementado nesta etapa.

Os getters `0x457759` e `0x45778a` chamam `0x402741`. Sua assinatura é:

```text
(DWORD da raiz no offset +8 << 16)
    + soma dos DWORDs no offset +8 de cada rotina encontrada na closure
    módulo 2^32
```

A busca visita cada ID uma vez, incluindo a raiz; percorre todas as instruções,
sem filtrar por alcançabilidade dos ramos. Um opcode vira chamada quando
`(opcode & 0x7fff) > 255`. Ciclos e chamadas repetidas não repetem a soma.
Rotinas ausentes não acrescentam valor nem descendentes. A resolução mantém
o mesmo contexto BehaviorFinder. O getter `0x40204f` devolve os dados BHAV
e `0x4020a6` localiza instruções de 12 bytes após o header de 12 bytes.

Portanto os campos baixos chamados `Unknown1a`/`Unknown2a` pelo leitor de
referência **não são contagens de instruções**. Comparar somente as palavras
altas `InitTreeVersion`/`MainTreeVersion` também é insuficiente. O DWORD do
header participa inteiro na soma, incluindo a palavra alta/reservada, e a
soma pode produzir carry. O modelo mantém os bytes e os nomes antigos de
leitura, mas a comparação deve recompor cada par em um uint32.

## Implementação validada até aqui

O patch `0005-native-code-signature.patch` adiciona `BHAV.NativeVersionWord`
e `VMTS1CodeSignature.Compute(root, resolver)`. O leitor preserva o DWORD em
`0x8000`, `0x8001` e `0x8002`, sem alterar a posição final do stream nem o
comportamento anterior de `Version`. O layout TSO `0x8003` permanece distinto:
o helper recusa uma rotina resolvida sem header nativo suportado.

O helper não usa gráficos, áudio, arquivos ou APIs de sistema. O chamador
fornece a resolução de recursos; isso mantém a regra separada do frontend e
dos serviços de plataforma. Ele **não implementa reset ou migração**.

```sh
dotnet run --project tools/simitone/signature-tests/SignatureTests.csproj -c Release
```

Foram aprovados 17 casos: 12 de grafo com resultados calculados a partir da
fórmula nativa, e cinco usando o parser real da `FSO.Files.dll` publicada.
Cobrem ciclos, duplicatas, IDs de escopos diferentes fornecidos pelo resolver,
árvores ausentes, máscara de opcode, bits altos, overflow, formato incompatível,
alinhamento e truncamento. Isso não valida o adaptador de recuperação nem a
resolução de todos os recursos do jogo.

O harness usa o SDK .NET10 instalado. Um ambiente com SDK/runtime9 pode passar
`-p:TargetFramework=net9.0`. Os caminhos de fonte/runtime podem ser substituídos
por `SimitoneSourceRoot` e `SimitoneRuntimeRoot`. Ele testa o helper do checkout
de trabalho e o parser da publicação, sem compilar o motor C++ preservado.

## Confirmação com os dados da House07

`reports/simitone-linux-evaluation/house07-native-signatures/` registra a execução
real do Simitone adaptado, com dados/perfil copiados e display Xvfb isolado.
Não houve erro no diagnóstico. Os seis arquivos comerciais acompanhados
mantiveram seus hashes; aplicação/Xvfb e cópias temporárias foram encerrados.
Os diálogos de exceção continuam presentes: o teste não é gameplay aprovado.

| Objeto | Init salvo | Init atual | Main salvo | Main atual |
|---|---|---|---|---|
| 110 | `0003000a` | `00040009` | `0003001d` | `0002002c` |
| 133 | `00010008` | `0004000c` | `0003001d` | `0002002c` |
| 136 | `00000005` | `00010005` | `0002002a` | `00040012` |
| 152 | `00010008` | `0004000c` | `0003001d` | `0002002c` |
| 153 | `000e0011` | `00000003` | `00060013` | `00010001` |
| 157 | `00020005` | `00080010` | `00030004` | `0002002c` |

Os valores atuais foram calculados pelo helper ODH com recursos reais, não
medidos executando o cálculo no original. Todas as seis definições têm Init e
Main diferentes dos salvos. O carregamento resolve seus arquivos em
`GameData/Objects/Objects.far`: `Lamps.iff`, `phones.iff`, `Paintings.iff` e
`lampgarden.iff`. Uma auditoria estrutural independente dos OBJD confirmou
tipo 4, MasterID0 e SubIndex0 para os seis GUIDs, com hashes dos entries em
`source-definitions.json`. Isso é consistente com o caminho comum individual;
não foi capturada uma instância do original em memória para medir sua vtable.
Todas as seis entradas Reset/EP3 atuais são zero.

## Recuperação nativa que ainda precisa ser adaptada

`0x431edd` executa Load/EP2 quando aplicável. Com diferença de assinatura e
controle global habilitado (byte `0x6c2972` inicialmente 1 na imagem), notifica
as demais entidades, recupera o objeto e executa Init/EP0. Há também um caso
especial para o GUID exato `0x3c08965f`, independente de diferença; não equivale
a selecionar todo OBJD cujo tipo seja Cursor.

`0x444346` recebe o mundo em ECX e o alvo como argumento: visita as outras
entidades e chama seu método de reset com esse alvo. **Não reinicia filhos.**
Objetos comuns ignoram alvo não nulo; pessoas podem reiniciar sua execução
ou remover ações/referências ao alvo. A implementação de pessoa é `0x45e8fd`.
O descarte de uma ação pode executar EP4; não equivale a remover itens da fila
sem notificação nem a reiniciar indiscriminadamente todos os Sims.

`cXObject` usa vtable `0x666d34`, reset `0x43206d`. Com EP3, executa a entrada,
ajusta flags/UseCount e reinicia Main. Sem EP3, usa snapshot de posição,
nível, container/slot (`0x432664`), retirada (`0x4331d4`), reinicialização
(`0x431c90`) e reposicionamento (`0x4333ef`). Identidade e tipo ficam fora do
bloco limpo. O bloco de 72 palavras em objeto+`0x4a..0xd9` é zerado, preservando
índices `1,21,24,29,41,45,46,47,67`; direção é normalizada. O índice11 recebe
o ID e o índice58 recebe a versão da definição. O array de palavras próprio
do objeto e os oito temporários também são zerados. Main é reiniciado por
`0x474979`; Init é executado nos pontos demonstrados pelo fluxo, inclusive
após reposicionamento na recuperação por incompatibilidade.

As variantes multitile envolvem operações por peça; portal e cursor têm
outras sobrescritas. As APIs atuais `VMEntity.Reset`, `Init`, `ResetData` e
`Delete/CreateObjectInstance` não substituem esse contrato automaticamente:
podem perder identidade, slots, atributos, posição ou notificações.

Próxima implementação: planejar **todos** os tipos e instâncias incompatíveis
do lote, não só os seis que já geram exceção; preservar a ordem Load,
notificações, recuperação e Init com APIs adequadas. Mapear os ramos de pessoa,
multitile e colocação antes de declará-los cobertos. Não desativar diálogos,
inventar retorno de BHAV ou manter frames cujo código não existe para passar
no teste. A prova final continua sendo abrir e jogar o lote, interagir,
comprar, salvar e reabrir sem essas falhas.


## Adaptação em andamento: fases e limites (2026-10-25)

A leitura de OBJM fornece 68 palavras por objeto, enquanto o modelo em memória
usa 80. A importação passa a completar as palavras restantes com zero, sem
alterar o OBJM original. Isso permite aplicar a regra nativa de limpar 72
palavras, preservando os campos demonstrados acima. O plano considera os 82
tipos não nulos da House07: 61 têm assinaturas diferentes, sem definições ou
raízes obrigatórias ausentes. Não se limita aos seis tipos cujos frames falham.

A implementação de recuperação ainda é **experimental e incompleta**. O teste
`house07-recovery-padded` compila, mas falha antes da migração por classes ainda
não mapeadas. Essa rejeição não prova recuperação funcional. Ela confirmou que
árvores, portas/janelas, móveis, Bob e Betty também precisam ser tratados.
O runner agora oferece `--require-clean-vm` com `--vm-diagnostics`: processo
vivo, sozinho, não aprova um teste que registrou exceção de simulação ou fatal.

### Objetos comuns e grupos

Para o caminho sem EP3, posição/identidade são capturadas antes da retirada;
as palavras retidas são capturadas na reinicialização, **depois** dos callbacks
de retirada. As notificações às pessoas vêm antes de ambos. A categoria também
precisa sair do índice do motor antes de limpar a palavra59. A versão inicial
da adaptação tinha esses pontos incorretos; a revisão independente os apontou.
No caminho com EP3, reiniciar Main não deve zerar os temporários: a limpeza
explícita deles pertence ao caminho sem EP3.

O grupo nativo usa `0x422db9`: EP10 em todas as peças, depois retirada de todas.
`0x4231a8` reinicializa todas, recompõe os deslocamentos e executa Init em cada
peça. `0x422c62` recoloca as peças e executa EP9 em cada uma. Por fim `0x431edd`
executa Init no alvo original. O chamador continua por entidade; não foi
introduzida uma deduplicação por grupo sem evidência de que o original o faça.
A ordem nativa entre peças além da principal ainda precisa ser comparada com
a lista importada pelo Simitone. Esses callbacks podem alterar o grupo.

Portal usa sobrescritas de retirada `0x463648` e colocação `0x46322f`, que fazem
trabalho adicional de ocupação/cache antes/depois das operações do grupo.
Aplicar apenas o caminho comum a uma porta não demonstra equivalência.
O snapshot `0x432664` consulta ParentId26/SlotHierNumber14, que não devem
ser confundidos sem prova com ContainerId2/SlotNumber3. Há suporte experimental
restrito a um filho direto, sem filhos próprios, cujos dois pares de campos e
o slot recíproco do pai coincidam. A regra é estrutural, sem lista de GUIDs.
Ela cobre os quatro registros observados na House07; cadeias gerais continuam
rejeitadas. A colocação `0x4333ef` passa por `0x432a70`, que chama `0x432771`
para somar um ao slot solicitado antes de `0x4327d9` escrever o campo14.
Assim, o slot0 termina com SlotNumber3=0 e SlotHierNumber14=1. Não basta ler
apenas a subtração feita no snapshot e concluir que os dois campos usam zero.
Exportações adicionais: `reports/ghidra/container-recovery-audit/manifest.json`.
A tabela de slots do próprio objeto fica fora do bloco de palavras limpo;
isso não é justificativa para resetar filhos.

### Pessoas

O reset de pessoa `0x45e8fd` não chama a limpeza comum `0x431c90`. Preserva
motivos e os demais dados salvos, desenrola usos de objetos, limpa controles
visuais, ajusta colocação, reinicia Main e executa EP3 também para humanos.
Depois limpa campos específicos: Posture0, WalkingUpStairs40 e
InteractionStatus59 de PersonData, Hidden34 e RoutePreference9 de ObjectData.
Não há limpeza explícita dos temporários nesse caminho. `VMAvatar.Init` não
serve como substituto, pois redefine motivos, personalidade e outros campos.

A notificação reinicia uma pessoa quando o alvo consta dos usos ativos ou é
o IconOwner da ação ativa (`0x45e974` compara o ponteiro diretamente, sem
testar TTAIndex); não basta encontrar uma referência arbitrária de
StackObject. Nos demais casos, remove ações cujo alvo ou ícone coincide,
executando os callbacks EP4 e preservando a ordem das ações restantes.
A importação atual perde limites/flags de `OBJMPerson.ObjectUses` ao convertê-los
em frames; o adaptador precisa preservar os registros ordenados originais.
`0x45e769` decrementa UseCount ao desenrolar cada uso e atualiza a ação ativa.

Exportação adicional, somente leitura: `reports/ghidra/person-recovery-audit/`.
`0x45fc43` destrói itens de uma coleção visual; `0x46e15e(0)` limpa controles
por item, sem apagar os motivos. `0x43270d` constrói um snapshot de colocação;
`0x432644` consulta se ele está no mundo. `0x434895` implementa busca de posição
com restrições de sala/colisão; usar apenas SetIndivPosition não reproduz isso.
As conclusões continuam estáticas, específicas ao perfil identificado acima.


Os metadados nativos de pessoa agora são capturados antes da conversão: usos
ordenados com limites/flags, ação ativa, última ação e fila, incluindo os
argumentos copiados para coleções somente leitura. Esse estado é conservado
no plano de compatibilidade; ainda não executa o reset de pessoa. A seleção
pura por alvo segue o uso ativo ou o ícone ativo, sem inferir validade por
TTAIndex; a última ação não participa desse teste.
