# Inventário do bairro e consulta FindToken

O [checkpoint de prefixos salvos](saved-prefix-validation.md) posterior
implementa o decoder C++ e a ligação objt descritos na próxima etapa abaixo.

O host lê NGBH externo e despacha opcode51/mode3 FindToken sobre o inventário
do avatar. Dados do original continuam imutáveis. Não implementa ainda as
operações de adicionar/remover tokens, enumeração, autofollow ou persistência.

Referência: FreeSO main `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0,
clonado em `refs/upstream/FreeSO`. Contratos consultados em `NGBH.cs:29–51,93–97`,
`TS1NeighbourProvider.cs:91–93,255–259`, `VMTS1InventoryOperations.cs`
e registro51 em `VMContext.cs:492–496`. Opcode49 Notify é uma operação distinta.

## Reader próprio

`read_ngbh` conserva reserved/version/magic brutos e16 palavras signed16.
Assim como a leitura estrutural da referência, não valida magic ou versão
como whitelist. O prefixo tem44 bytes. EOF exato nessa posição significa
inventário conhecido sem extensão; não significa recurso ausente.

Quando há extensão, lê contagem signed32, registros com marker signed32,
NeighborID signed16 e itemCount signed32. Cada item conserva Type signed32,
GUID uint32 e Count uint16. Último registro serializado de um NeighborID
substitui a lista inteira e o marker anterior. Itens duplicados permanecem
na ordem, sem consolidação de quantidades. Cauda desconhecida é informada
como extent. Contagens negativas são recusadas no perfil limitado ODH.

Limites padrão:1MiB payload,4096 registros serializados e65536 itens
cumulativos. Listas posteriormente sobrescritas continuam consumindo o
orçamento da leitura. Truncamento/contagem/budget precedem alocação da lista;
o resultado possui seus dados e sobrevive ao View original.

O host seleciona o primeiro chunk NGBH na ordem serializada, como o provider
de referência; não pressupõe ChunkID1. O JSON registra seu ID e versão,
presença da extensão, números de inventários/itens e cauda. Sem NGBH o handler
não é ligado; consulta51 permanece unsupported. NGBH inválido selecionado
é erro de recurso, sem fallback para inventário vazio ou outro chunk.

## Semântica da consulta e publicação na VM

Operandos: Mode/TokenType/Flags/Flags2 e GUID uint32 LE. Apenas mode3 é
implementado neste checkpoint. Target é Caller, ou entidade indicada por
Temp4 quando Flags2 bit5 está ligado. Exige avatar com PersonData31;
NeighborID pode ser signed. Alvo ausente/objeto/banco incompleto é unsupported.
GUID não zero é literal. GUID0 exige definição explícita de StackObject e
usa seu GUID, sem alias por master ou por Caller.

A consulta escolhe o primeiro item com GUID e tipo solicitado, sendo tipo0
wildcard. Inventário inexistente em NGBH carregado ou item ausente retorna
false sem saídas. Não usa CountInTemp nem o índice inicial para limitar a
busca FindToken. Outros modos permanecem unsupported, sem sucesso inventado.

Ao encontrar item, Count uint16 vira signed low16 e é escrito em
Temp[(Flags>>2)&3]. Flags bit4 solicita índice signed low16 em
Temp[Flags2&3]. O retorno é true apenas quando a contagem **signed** é positiva;
0 e32768..65535 retornam false, mas ainda publicam as saídas. A consulta
não altera inventários e não aloca armazenamento durante a busca.

PrimitiveOutcome agora tem um segundo temp opcional de capacidade fixa.
A VM valida frame/temps/contexto antes de publicar qualquer saída, apenas
em branches síncronas. Aplica contagem primeiro e índice depois; quando
ambos selecionam o mesmo temp, índice vence. Índice inválido ou disposição
WAIT/call/unsupported com saídas deixa todas as palavras intactas.
As referências capturadas devem sobreviver à VM; callbacks não podem reentrar
ou alterar o backend durante dispatch.

## Evidência Linux

160 checks sintéticos do reader,57 de FindToken e101 do teste central VM.
Os checks de FindToken incluem seleção Caller/Temp4, GUID literal/StackObject,
primeiro match/tipo wildcard, contagens signed nos limites, índice32768,
ausência conhecida, alvos inválidos e duas saídas em VM real, inclusive
retorno false e alias. CLI verifica primeiro NGBH serializado (ID7), recurso
ausente/malformado, inventário vazio/populado e origem intacta.

Debug:62 testes passaram no primeiro conjunto; após corrigir fixtures do
novo teste FindToken, esse teste passou, totalizando63 aprovados/1 X11 pulado
(64 configurados). ASan/UBSan:63 aprovados/1 X11 pulado no conjunto completo.
LeakSanitizer ficou desativado por ptrace. Não houve validação de console.

Survey estrutural de UserData, UserData2–8 e TemplateUserData: cada arquivo
tem NGBH1/reserved0/payload44, sem extensão, mapas vazios e tail0.
UserData version62; demais version63. Inventários populados foram testados
com fixtures sintéticas, sem alegar evidência nativa desse perfil.

Reprodução: comando de [family-restore-validation.md](family-restore-validation.md),
House07/--activate-family/--initialize/NBRS/zoning/seed1, Bob User00000 e
membro47 User00001 separadamente. Agora ambos:

- FindToken GUID0x4e562938/type6 em8664 pc0 retorna false sem escrever temps;
- Init180+Main290=470 instruções,2 ticks;
- param em opcode44 AnimateSim, PersonGlobals8330 pc9,
  operandos `[25,1,0,0,3,32,0,0]`, true36/false9;
- stack4096 pc1 →8193 pc4 →8283 pc16 →8330 pc9;
- exit3, host_failed=false, RNG final12162792540164244070.

Relatórios Debug/ASan idênticos byte a byte, ignorados em
registro arquivado não distribuído:
Bob SHA256 `5d7fbcbc8d18f60dcb77060d38d43680cdeb14aaea22f1d3453c696a6c1dd7eb`;
membro47 SHA256 `3f681bb26efbf29538a62b9c132fdc899b486dc030cf7031ba65e715e75dafcb`.
Neighborhood e House07 foram conferidos antes/depois com os mesmos hashes
do checkpoint de família. A execução usa World construído, não um lote salvo
restaurado nem uma comparação com o executável original.

## Próxima restauração espacial

Auditoria independente do prefixo ObjM de House07 leu182 IDs únicos, todos
presentes em IDToOBJT, com Level1/UnknownData1. X varia -16..648 e Y -16..744;
oito pares(-16,-16) são OUT_OF_WORLD. Prefixos consomem316..451 bits e deixam
pelo menos158 bits posteriores;98 offsets são ímpares. Atributos:
8×142 instâncias,12×34,32×4,11×1 e16×1.

Primeiro bloco: ObjectID179/type10, X280/Y360/Level1, footprint
[MinY352,MinX272,MaxY368,MaxX288],362 bits. Último:ID203/type41,
X440/Y744/Level1,354 bits. Evidência independente está em relatórios locais
ignorados registro arquivado não distribuído e registro arquivado não distribuído.

Próxima implementação C++ deve decodificar signed32 com larguras6/11/21/32,
atributos limitados,8 temps/68 ObjectData/5 extras, mantendo cursor em bits e
paridade para strings posteriores. ObjectData11 resolve ID→IDToOBJT→objt/OBJD;
somente depois é possível interpretar o restante variável da instância.
Essa auditoria não publica posições em World. Aparência/animação, arquitetura,
restauração de entidades, Main/fila/render e lote Linux funcional seguem pendentes.
