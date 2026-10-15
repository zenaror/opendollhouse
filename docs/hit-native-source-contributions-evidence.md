# Contribuições nativas de fontes por TrackID

Evidência estática de 2026-10-15, base ODH `ce8b327`. O serviço nativo mantém
pares únicos `(TrackID, R1)` e agrega controles de todas as fontes do track.
Remoção de uma fonte, vida do controller, execução HIT e ownership PCM são
contratos distintos. Esta unidade fecha operações de dados e aritmética para
um componente portátil explícito; não liga automaticamente suas operações ao
World, ao mixer ou à política de reprodução.

## Fonte e método

Imagem externa `../referencias/The Sims/Sims.exe`, SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
Base PE `0x400000`. Para os trechos selecionados `.text`, `.rdata` e `.data`,
offset de arquivo = VA − `0x400000`; globais de runtime não são offsets.
Nenhuma execução do original, escrita em referências ou alteração de projeto
Ghidra foi feita. Não se estabelece equivalência com outra imagem executável.

Leitura inicial dos exports `0057376b`, `0057320a` e `0057a9a6` em
`reports/ghidra/supplied-functions/`, seguida de `objdump` pontual. Foram
procurados calls relativos para os helpers específicos e confirmados os
callers relevantes em assembly; isso não enumera chamadas indiretas gerais.
Bytes, VAs, offsets e identidade estão em
`reports/hit-native-source-contributions-selected.asm.txt`.
Complementa [identidade de TrackID](hit-native-track-identity-evidence.md).

## Estrutura e operações

O multimap fica no serviço de eventos +`0xd4`, separado do catálogo de
controllers `manager+0x80` e do registry de opcode69. Cada nó contém próximo
em +0, TrackID em +4 e fonte em +8. TrackID é tratado como chave de 32 bits
no hash; a fonte é comparada por igualdade de palavra e, em alguns callers,
por comparação assinada. Não há geração World nem handle PCM nesse par.

| Operação | VA / offset | Efeito comprovado |
|---|---|---|
| Buscar par | `0x57451a` / `0x17451a` | Equal-range por TrackID; procura fonte igual. |
| Inserir se ausente | `0x574581` / `0x174581` | Retorna sem inserir par repetido; senão garante espaço e insere. |
| Remover fonte | `0x57462e` / `0x17462e` | Remove uma entrada correspondente, aplica efeito ao track, reinicia busca. |
| Limpar TrackID | `0x5764a8` / `0x1764a8` | Libera todos os nós daquela chave, decrementa contagem e retorna quantidade. |
| Atualizar track afetado | `0x5747dc` / `0x1747dc` | Sem fontes: +`0x34`; com fontes: agrega e aplica controles. |
| Atualizar todos | `0x5745d3` / `0x1745d3` | Percorre equal-ranges e aplica controles uma vez por TrackID. |
| Agregar | `0x57488f` / `0x17488f` | Maior volume e pan ponderado, conforme abaixo. |

Inserção não chama provider, setter ou rotina de áudio. Não valida zero ou
sinal da fonte; o ramo Play tipo1/ArgStyle2 barra R1=0 **no caller**. A
limpeza por TrackID não dispara atualização/parada; o caller inativo de
Play a usa antes de admissão e inserção. Capacidade fixa e resultado de
recusa por limite seriam políticas explícitas ODH, não limites descobertos
no multimap nativo dinâmico.

`0x57462e` faz erase por iterador em `0x57466b` e só então chama
`0x5747dc(track)` em `0x574673`. Após esse efeito, obtém novamente o início
da tabela. Não é uma remoção em lote seguida de callbacks. Se só forem usados
os helpers de inserção comprovados, um mesmo source aparece no máximo uma
vez por track; a retirada atravessa todos os TrackIDs que o contêm.
A ordem segue buckets e encadeamento do hash (`0x576e56`), não foi provada
equivalente à ordem de inserção. Um vetor ODH pode ter ordem determinística
própria, desde que essa diferença não seja apresentada como ordem nativa.

## Agregação: palavras raw e aritmética exata

O resultado de `0x57488f(track, out_volume, out_pan)` tem três condições:

- Equal-range vazio: escreve volume0 e pan512, retorna false
  (`0x5748c3–0x5748d2`). Não exige controller nesse ramo.
- Equal-range não vazio, controller ausente no catálogo: retorna false sem
  escrever outputs (`0x5748dd–0x5748ed`). Não é o mesmo fallback do vazio.
- Fontes e controller presentes: consulta cada fonte por `0x57320a`, agrega,
  escreve resultado e retorna true. O retorno individual do provider é ignorado.

Para amostras completas `(v_i,p_i)`, ambos int32, a operação demonstrada é:

```text
maximum = 0
denominator = uint32(0)
numerator = uint32(0)
para cada amostra:
    maximum = max_signed(maximum, v_i)
    denominator += bits32(v_i)                         // módulo 2^32
    numerator += (bits32(p_i) - 512) * bits32(v_i)      // módulo 2^32
q = 0 se denominator == 0
    senão trunc_toward_zero(int32(numerator) / int32(denominator))
volume = maximum
pan = bits32(q + 512)
```

Prova: comparação assinada `CMP/JLE` em `0x574924–0x574929`; `ADD` do
denominador em `0x57492e`; `SUB 512`, `IMUL` de dois operandos (low32) e
`ADD` em `0x574931–0x57493a`; `CDQ; IDIV dword` em `0x574956–0x574957`;
`ADD 512` final em `0x574960`. Não há saturação ou clamp no agregador.
Não substituir o cálculo por médias de pan sem peso, por somatório de
volumes, por divisão unsigned ou por acumulador largo sem wrap intermediário.

Soma de pesos zero resulta pan512 mesmo se houver amostras e numerador não
zero. Valores negativos participam das somas; o máximo inicial zero impede
volume de saída negativo. Overflow dos produtos/somas é wrap x86 de 32 bits.
Denominador zero é protegido. Numerador `INT32_MIN` e denominador `-1`
causam divisão não representável em `IDIV`; não há guard nativo para esse
caso. Uma recusa explícita ODH evita UB/trap sem inventar resultado nativo.
Para amostras completas e sem esse trap, as somas modulares e o máximo não
dependem da ordem; isso não vale para a resolução defeituosa descrita abaixo.

### Fonte especial e fonte positiva não resolvida

`0x57320a` testa source assinado. Para source<=0 escreve `(1024,512)` e
retorna false (`0x573214–0x573230`). Como o agregador ignora esse retorno,
essa contribuição é efetivamente incluída. Não confundir false com ausência
de amostra. Zero pode existir na tabela por outros callers além de Play tipo1.

Para source>0, `0x570f1d` busca o objeto por `0x407498`. A busca é 1-based:
exige ID>0 e menor que tamanho+1, depois lê tabela[ID−1]. Não contém contador
de geração. O provider extrai campos +`0xf8`, +`0xf4` e +`0x110` do objeto;
`0x573248–0x573254` recusa lookup falho ou qualquer um dos dois primeiros
campos zero **sem escrever** volume/pan. Os passos seguintes usam contexto
de projeção e ID57 do controller. Suas fórmulas geométricas não fazem parte
do componente de agregação sobre amostras completas.

Há efeito residual preciso no caller nativo: antes da primeira consulta,
pan foi zerado (`0x5748fe`); o temporário de volume `[ebp-0x14]` ainda contém
o segundo DWORD do iterador final, isto é, o ponteiro da própria tabela
`service+0xd4` (`0x5748b3/0x5748be`; equal-range escreve o container em
`0x576446`). Se a primeira consulta positiva falha, esses valores entram no
cálculo. Após uma consulta que escreveu outputs, uma falha posterior reutiliza
o volume e pan anteriores. Isso é dependência de resíduos/pointer de processo,
não fallback semântico para zero. ODH deve resolver amostras explicitamente e
recusar fonte positiva indisponível antes de chamar o agregador puro; tal
recusa é uma escolha segura declarada, não emulação desses resíduos.

## Aplicação e efeitos após mutação

`0x57482d` chama agregação, busca o controller e aplica volume antes de pan
(slots +`0x40/+0x48`, setters `0x57d873/0x57d899`). Esses setters escrevem
IDs19/20 pelo writer público; não são conversão para ganho Q15 do host.
Há um ajuste **fora do agregador** quando `service+0x114 != 0`: decrementa
volume em 32 bits, e se o resultado assinado for negativo substitui por 1.
Assim volume0 vira1 e volume1 vira0 (`0x574860–0x574870`). Não descrevê-lo
como clamp mínimo1 nem incluí-lo na função pura de agregação.

`0x5747dc` chama +`0x34` quando o último par foi retirado. Seu destino
`0x57df9c` retorna se a thread está inativa. Se ativa e com objeto de áudio
que responde ativo, escreve fade_dest40=0, fade_var41=19, fade_speed42=100,
fade_on45=1 e marcador +`0x1cf`=1. Caso contrário chama parada imediata
+`0x38`. Não é garantia de desaparecimento imediato de PCM, nem o mesmo
fade do opcode69. Não converter 100 em duração física. Havendo outras
fontes no track, o caminho normal de remoção reaplica os controles delas.
Esses helpers assumem controllers presentes nos caminhos de efeito; não
oferecem recuperação geral para TrackID obsoleto.

## Callers de remoção e refresh

| Caller | Operação comprovada |
|---|---|
| Evento `0x17`, `0x573802–0x57381d` | Remove fonte passada no argumento seguinte ao tipo, somente se não zero. |
| Evento `0x1e`, `0x573e68–0x573ed9` | Remove R1 de todos os tracks, depois adiciona `(TrackID,R1)`, agrega e inicializa/aplica o novo track; não há filtro R1!=0 nesse ramo. |
| Evento `0x20`, `0x573f07–0x573f76` | Coleta TrackIDs que contêm a fonte, depois atualiza cada track; não remove pares. |
| Evento `0x21`, `0x5742ab` | Chama refresh de todos os tracks. |
| Evento `0x69`, `0x5743cf–0x5743df` | Chama a limpeza especial `0x5746a7` descrita abaixo. Esse número é tipo de evento, não opcode HIT69. |
| Rotina `0x57312a` | Quando +`0x114` é não zero, refresh com seu valor atual, zera o campo, faz segundo refresh e armazena valor anterior−1. |

O wrapper `0x57a9a6` encaminha seu argumento ao evento0x17, sob gates de
habilitação e locks. Callers `0x432163` e `0x432478` passam
`MOVSX word[object+0xf0]`. O segundo está na rotina `0x4323e0`, que restaura
a vtable `0x666d34` e libera membros do objeto. RTTI: ponteiro COL em
`0x666d30` → `0x68b4e8` → type descriptor `0x6c0ee0`, nome
`.?AVcXObject@@`. É evidência concreta de remoção ligada ao cleanup de objeto,
sem prova de geração ou cobertura de toda forma de destruição/reuso World.

`0x4525ae` também chama esse wrapper com word do frame+4 ou da instância+f0,
conforme o ramo. Outros callers passam `-1` explicitamente, por exemplo
`0x4ce853` (e `0x54aee3`, `0x552355`, `0x557e3e`, `0x55f69f`, `0x565382`,
`0x569899` na busca de calls). `-1` não é wildcard em `0x57462e`: ele remove
somente pares cuja fonte é exatamente `-1`. O caso zero no wrapper evento17
é ignorado. Não transportar esses valores especiais para IDs de entidades.

Para refresh, `0x4f6772` chama wrapper `0x57ad70`, que envia evento0x21.
A cadência de todos os callers e os triggers de movimentação/projeção ainda
não estão mapeados; a existência desse refresh não prova um tick World fixo.

A limpeza especial `0x5746a7` preserva em tabela temporária os pares com
source<=0. Para **cada par positivo**, chama +`0x34` no controller antes de
limpar o multimap inteiro, restaurar os pares não positivos e fazer refresh
geral. Pode chamar parada várias vezes no mesmo track, inclusive quando ele
também tem fonte negativa preservada. Não substituir esse procedimento por
"parar somente tracks que ficaram vazios"; essa regra pertence à remoção
normal de uma fonte, não a esse caminho especial.

## Contrato portátil sustentado e limites

É sustentado um componente de pares raw com inserção idempotente, consulta,
cópia das fontes de um track, limpeza por TrackID e remoção de uma entrada
por fonte que devolve o TrackID afetado. O orquestrador pode intercalar efeitos
entre remoções sem callbacks dentro da coleção. Limites explícitos, recusa
antes de excedê-los e ordem determinística própria são políticas ODH.

A função pura de agregação recebe somente amostras completas, usa as palavras
int32 e wraps acima, e recusa a divisão não representável. Não chama provider,
não decide validade de fonte, não aplica o ajuste +`0x114`, não faz fade e
não controla ownership. Uma máscara de geração World, caso usada pelo host,
continua externa ao par nativo.

No Simitone fixado (`4906db3`, FreeSO `f86bbd3`, MPL2.0),
`tso.sound/HITSound.cs:31–57` escolhe volume/pan da contribuição que venceu
em volume; isso não é a média ponderada inteira demonstrada aqui. Seus
`AddOwner/RemoveOwner` tampouco substituem o tracing do multimap original.

Continuam pendentes a política completa de registro/reload dos controllers,
gates e cadência de todos os callers World, geração/reuso dos ObjectIDs,
geometria fiel de `0x57320a` e efeitos de áudio compartilhados. O contrato
portátil de dados e aritmética pode avançar sem promover esses pontos a
equivalência nativa ou ligar automaticamente a coleção ao probe.
