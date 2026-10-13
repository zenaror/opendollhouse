# Tuning BCON por proprietário de código

Checkpoint de 2026-10-13. `BconTuning` fornece um snapshot imutável de constantes
BCON privadas, semiglobais e globais. O host `odh-person` agora liga essas tabelas
à leitura26 e registra a origem de cada valor. O Init nativo avança da primeira
leitura de tuning até a dependência de lote; a inicialização completa segue aberta.

## Contrato de endereço

Referência fixa FreeSO `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL2,
consultada também no clone local:
[Engine/VMMemory.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Engine/VMMemory.cs).
Scope26 usa os16 bits do argumento, independentemente de seu sinal na VM.
`raw_table = unsigned_query >> 7`, `index = unsigned_query & 127`.

| raw_table | Banco | ID BCON |
|---|---|---|
| 0..63 | Privado do code owner | 4096 + raw_table |
| 64..127 | Semiglobal do code owner | 8192 + raw_table - 64 |
| 128..191 | Global compartilhado | 256 + raw_table - 128 |
| 192..511 | Privado estendido | 4096 + raw_table |

A última faixa é o comportamento do algoritmo da referência: mode permanece0
quando raw_table>=192. Não é classificada arbitrariamente como outro banco ou
recusada por tratar o argumento assinado como índice negativo. O query-1,
por exemplo, é raw_table511/index127 e consulta BCON4607 privado.

Os valores BCON são words unsigned na estrutura, interpretados como int16 na
consulta. Zero presente é valor válido. Cada `TuningValue` conserva owner que
fez o pedido, banco/endereço, valor assinado e indicação de fallback.

## Semiglobal e limites de evidência

Um `TuningOwner` distingue vínculo semiglobal desconhecido, ausente e carregado.
Quando explicitamente ausente, a referência usa o cache privado, **mantendo o
ID semiglobal**: query0x2000 consulta8192 no privado, não4096. Um semiglobal
carregado com tabela ausente não faz esse fallback. Vínculo desconhecido e
owner privado/semiglobal ausente permanecem unsupported. O global compartilhado
é independente da presença do owner no snapshot; o owner no registro identifica
o contexto solicitante, não um banco global duplicado por proprietário.

A referência usa `ScopeResource = CodeOwner.Resource`, preservado inclusive
através de chamadas globais. Callee e StackObject não escolhem outro banco de
BCON. Antes da consulta, FreeSO permite `entity.TuningReplacement` do Callee;
esse mecanismo não está implementado no ODH. A implementação atual é explicitamente
BCON-only, aplicada a entidades construídas sem backend de replacement.

[WorldObjectProvider.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.content/WorldObjectProvider.cs)
popula seu cache com BCON e depois pode sobrescrevê-lo com tuning OTF. O snapshot
ODH não carrega OTF, upgrades ou replacements, nem afirma que o cache completo
do original contém apenas BCON. A comparação nativa aqui é com os bytes dos
BCON explicitamente fornecidos. Equivalência ao interpretador original segue
não demonstrada.

Outra diferença deliberada de cobertura: a referência devolve0 quando não
acha um entry no cache; ODH devolve nullopt e a VM registra read unsupported.
Isso separa dado ausente de uma constante zero e evita fabricar valores para
atravessar uma dependência ainda não carregada. Não é prova da semântica de
constante ausente do executável original.

## Snapshot e integração

O snapshot possui bancos por owner e um global compartilhado. Sua construção
copia ou recebe propriedade dos dados e valida limites agregados:64 owners,
1024 tabelas,65536 constantes por padrão. Cada BCON continua limitado pelo
count byte a255 palavras; queries só endereçam os primeiros128 entries.
Limites são configuráveis, com validação por subtração para impedir overflow.
Estados semiglobais incompatíveis com tabelas fornecidas e counts estruturais
inválidos são rejeitados. Não existe registro ou mutação de tuning durante run.
Esses limites não são medição de RSS nem orçamento comprovado de New3DS.

O host lê BCON dos mesmos recursos explícitos de avatar, semiglobal, globals e
alvo. Duplicação de ID dentro de um recurso é erro. IDs BCON não seguem o role
de BHAV: um privado pode conter BCON8192 para o fallback conhecido acima.
Owner1 registra seu semiglobal como carregado; owner2 só é aceito sem vínculo
semiglobal e registra essa ausência. Não há busca automática de outro arquivo.

O callback de memória delega scope26 ao snapshot e conserva os demais scopes
do World. Leituras bem-sucedidas entram em `tuning_reads` no JSON, incluindo
fallback. O log é limitado a20.000 leituras, compatível com no máximo duas
consultas por Expression e o budget10.000 dispatches do host. Ausência continua
com o diagnóstico padrão da VM e o PC/contexto preservados.

## Gates

- `bcon_tuning`:76 checks sintéticos de todas as fronteiras de banco/tabela,
  index0/127, negativo/query-1, signed words, zero/ausência, owners distintos,
  global compartilhado, semiglobal desconhecido/ausente/carregado e fallback
  sem alterar o ID. Também valida cópia/move, limites exatos e excesso, estado
  inválido e chamada global na VM preservando o owner para a consulta.
- `person_cli` carrega BCON sintéticos via FAR/IFF: uma rotina global do owner1
  lê privado=-2, semiglobal=11 e global=0; a ação owner2 lê privado=25 e um
  semiglobal ausente faz fallback para seu privado8192=126. A lista de origens
  e valores é conferida; semiglobal carregado ausente, entry fora do count e
  duplicação de BCON são rejeitados.
- Debug e ASan/UBSan:42 testes passam, um X11 é skipped neste sandbox. LSan
  desligado (`detect_leaks=0`), sem validação de vazamentos.

No corpus externo, Init4097 agora executa19 dispatches/0ticks. Ele lê
GlobalBCON257[4]=1 (`0x4084`) e[7]=7 (`0x4087`), passa por outras instruções de
8192 e entra8325/init motives. O fault seguinte ocorre em global321/pc1,
`unsupported read scope 6 index 10`, onde o lote atual ainda não foi fornecido.
Ambas constantes foram comparadas com parser Python independente dos bytes
FAR/IFF/BCON. O JSON inteiro é idêntico entre Debug e ASan/UBSan.

Main4096 permanece6 dispatches/1tick e o mesmo fault de lote em global320/pc0,
sem leituras de tuning antes dessa fronteira. Isso não é regressão nem conclusão
de Main: a dependência de tuning do Init foi resolvida no perfil BCON, e o
contexto de lote é o próximo gate em ambos.

## Continuidade

A biblioteca e o novo host continuam sem Init/entry8/body strings completo,
atributos dinâmicos, zoning/lot context, RNG8/traits, replacements/OTF,
administração completa da queue ou render conectado ao Main real.
A próxima unidade deve definir o contexto de lote/globals a partir de origem
explícita, com zoning GenericTS1Call28 e gates de inicialização separados.
Não se atribui lote/random/idade arbitrários apenas para fazer o programa passar.
