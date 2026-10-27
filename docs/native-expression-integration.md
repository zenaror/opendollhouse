# Execução nativa das expressões TS1

Esta é uma etapa implementada do [port completo para C/C++](native-port-plan.md).
O executável final deve dispensar C#/.NET. O candidato gerenciado ainda serve
como ponte temporária para executar e comparar os sistemas migrados.

## Implementação usada

`native/simulation/src/odh_expression.cpp` executa todos os 21 operadores TS1
da primitiva BHAV Expression: cálculos, atribuições, comparações e flags.
O caminho real `VMRoutine.Execute → VMExpression.Execute` chama essa
implementação quando a VM é TS1. Não existe fallback de operadores TS1 em C#.
O código gerenciado de listas restante atende somente ao TSO.

A biblioteca `odh_simulation` conserva os 14 exports da fila e acrescenta dois
exports `odh_expression_*`, com sua própria versão ABI 1. Operand de oito bytes,
inteiros de tamanho definido, ordem little-endian e callbacks síncronos estão
descritos no [contrato nativo](../native/simulation/EXPRESSION.md). O executor
não possui heap ou estado global mutável; decide a ordem de efeitos e preserva
as particularidades observadas da referência.

Os acessos a memória, entidades e andares ainda atravessam a ponte para
`VMMemory`. Portanto, o estado da VM e o restante da execução **ainda não estão
portados**. A ponte captura exceções dos callbacks e as relança depois do retorno
nativo, sem desfazer efeitos anteriores. Frames temporários são reutilizados,
limpando referências ao sair. Primeiras chamadas e maior profundidade de
reentrada podem alocar; somente chamadas já aquecidas foram medidas sem alocação.
O struct da ponte exige host little-endian, verificado explicitamente; a API C
usa bytes little-endian e não impõe essa limitação ao chamador nativo.

## Evidência

- registro arquivado não distribuído: 792
  verificações compartilhadas, CTest 4/4 incluindo a fila anterior, ASan/UBSan
  4/4 com warnings como erros; 16 exports conferidos.
- `managed-adapter/report.json` no mesmo diretório: 702 comparações/verificações
  contra o Execute original com memória sintética, aliases, erros, reentrada,
  threads e resultado das escritas. Medição local: zero bytes em 10.000 chamadas
  aquecidas. Não prova toda a implementação de VMMemory.
- registro arquivado não distribuído: mesmo programa mínimo rodou
  no CLR 9.0.21 e 10.0.13, frio/aquecido, vinculando a política de overflow,
  módulo, flags e raiz quadrada. O revisor também executou os 702 casos e a
  regressão de saída de log quebrada no CLR 9.0.21 real. Isso é evidência da
  referência gerenciada, não do executável original do The Sims.
- registro arquivado não distribuído: a mesma fonte nativa
  e os 792 checks compilaram para ARMv6K e produziram PASS pelo serviço de debug
  do guest. Captura, hashes, 147 arquivos de configuração preservados e limpeza
  da instância/perfil/Xvfb conferidos. É uma execução sintética em emulador;
  não é o jogo inteiro ou uma validação em hardware.
- registro arquivado não distribuído: compra de
  cadeira GUID26BFBB29, preço 80, saldo 665→585, Bob105 sentado usando objeto139;
  save e reabertura em processos distintos preservaram a compra e os dados do
  avatar. Amostras registram ao menos 81.920 e 16.384 chamadas nativas nos dois
  processos, sem erros registrados. SIMI 62→64 é a única mudança antes dos
  callbacks de inicialização; mudanças posteriores em outras entidades são
  mantidas no resultado bruto. `purchase-proof.json`, `lifecycle-summary.json`
  e revisão independente vinculam logs, fontes e binários. Originais preservados
  e processos/cópia/perfil encerrados. O número de display foi depois reutilizado
  pelo teste de família, sem significar que o processo anterior continuou vivo.


- registro arquivado não distribuído:
  118 ações observadas em sete processos isolados. Criou ODHExpr/Alex, comprou
  House09 (20.000→16.500), andou, salvou/reabriu, saiu da casa e excluiu a família;
  a ausência permaneceu após reiniciar. FAMI6/GUID1545731141, NBRS estrito
  49/49→289/289; posição e dados do Sim preservados ao reabrir. Amostras confirmam
  Expression nativa nas fases de simulação, sem erros VM/fatais. `summary.json`,
  `analysis.json` e `artifact-manifest.json` vinculam fontes, binários e arquivos.
  `final-review.json` independente aprova esse ciclo limitado.
  Há 18 mudanças de inicialização em outras entidades após EP2, sem alegar
  igualdade total. O save antigo da casa e registros do personagem permanecem
  após excluir a família; não foi validada a reutilização desse lote. O aviso
  repetido da expansão e preço 4.133 do lote vazio continuam pendentes.
  Todos os processos, tela virtual e cópia temporária foram encerrados; originais
  preservados. A primeira tentativa está separada: falha no controlador de teste,
  corrigida antes de repetir, sem evidência de falha do jogo.

Uma falha encontrada na revisão foi corrigida: escrever o diagnóstico não pode
transformar uma ação válida em falha nem esconder a exceção original. Também foi
preservada a rejeição de operadores de enum fora de um byte antes da conversão.
O domínio das operações nativas é o operand efetivamente codificado em oito bytes.

## Reprodução

Compilar `native/simulation` conforme EXPRESSION.md. O publish do candidato
continua exigindo os dois parâmetros absolutos `ODHNativeDataLibrary` e
`ODHNativeSimulationLibrary`; este último precisa apontar para a biblioteca com
os novos exports. A ponte não aceita biblioteca ausente ou ABI incompatível.

Frontend permanece com 0001/0002/0006. No FreeSO fixado, aplicar 0003/0004/0005
e o patch experimental atual com 24 arquivos, SHA-256
`1fe785a5e03ac5a69310250ad1d5a0b8fe2020c8bd8def5fc44ff20e6e9bb112`.
Replay independente reconstruiu os 29 arquivos do conjunto completo exatamente.
As fontes em `native/` e `managed/` deste checkout são dependências da receita.

SO Linux validado nesta etapa:
`3787dea79d4df8e2970b80b5087e50b23c8f59f74c8f00f634206e93b0aded08`.
3DSX do teste:
`551cdaa4abd4d1b5c5f94ed7075293e642e4cc6157b3433c577715a6c393e276`.

Próxima dependência a remover: estado e acesso à memória da simulação. Migrar
a implementação e sua propriedade para C/C++, sem criar um segundo estado
autoritativo espelhado no C#. O [plano integral](native-port-plan.md) também
inclui dados/saves, rotas, gráficos, UI, áudio, controles e todos os alvos.
