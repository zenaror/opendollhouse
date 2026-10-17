# Limites do Simitone e referência comunitária

Orientação humana de 2026-10-17: investigar o executável e os dados originais
para funções ausentes ou divergentes no Simitone. A implementação ODH busca
o comportamento TS1 PC; lacunas de referências não definem o escopo final.

## Roadmap indicado pelo usuário

A [issue8 do Simitone](https://github.com/riperiperi/Simitone/issues/8),
"1.0 Roadmap", foi criada em 2018-06-16 e permanecia aberta na consulta de
2026-10-17. A API informou `updated_at=2026-04-29T22:55:26Z`. O corpo consultado
tinha oito checkboxes marcados e 73 desmarcados, incluindo autonomia fiel,
promoção/rebaixamento de fama, salvamento em férias e chamadas globais TS1.
O bloco QA também distingue comportamento que deveria funcionar de casos
que ainda precisam de investigação. Snapshot local:
`reports/simitone-reference-audit-2026-10-17/issue-8.json`.

Esses itens orientam perguntas e cenários de regressão, sem provar que todos
os estados do checklist correspondem ao código atual. Comentários com desejos
de recursos adicionais não são requisitos do comportamento original.

## Fork alexjyong

Clone de consulta independente em `refs/upstream/Simitone-alexjyong`, branch
`simitone-forked`, revisão
`e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab` do
[fork comunitário](https://github.com/alexjyong/Simitone/tree/e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab).
O gitlink FreeSO é `7e76c1960252ddd033d8937d5edb47d326f5b69c` e seu URL
é `https://github.com/alexjyong/FreeSO`. Esse submódulo não foi materializado
ou auditado. Nenhum setup/build do fork foi executado.

A consulta do frontend encontrou implementação útil para comparação:

- `Client/Simitone/Simitone.Desktop/Program.cs:32` e
  `Simitone.Shared/GameLocator/LinuxLocator.cs:28`: startup Linux e procura
  por instalações. O locator usa caminhos específicos de Steam/Proton/Wine,
  sem comprovar descoberta de todas as bibliotecas Steam.
- `Simitone.Shared/GameLocator/WindowsLocator.cs:108`: detecção Steam Legacy,
  appID3314060. Isso não prova execução da Legacy pelo motor.
- `Simitone.Client/UI/Panels/UIMainPanel.cs:171` e
  `UI/Panels/LiveSubpanels/UIBuyBrowsePanel.cs:1210`: busca no catálogo;
  o segundo arquivo também trata eyedropper e transição entre modos.
- `Simitone.Client/UI/Panels/UICheatTextbox.cs:220` e
  `UI/Screens/TS1GameScreen.cs:447`: comandos/efeitos de clima e persistência
  auxiliar. `WEATHER_IMPLEMENTATION.md` qualifica o sistema como cosmético.

Nos caminhos abreviados de projetos `Simitone.*`, a raiz é `Client/Simitone/`;
nos caminhos `UI/`, o projeto é `Simitone.Client/`.
Declarações de partículas e clima automático nos documentos não foram
confirmadas pelo frontend auditado; podem depender do FreeSO não materializado.
Não são evidência de comportamento TS1 nem prioridade sobre o lote mínimo.

## Como aplicar ao desenvolvimento

Simitone e forks ajudam a localizar formatos e caminhos de execução. Para uma
função em falta, delimitar o cenário nos BHAV/recursos, rastrear a rotina nativa
na imagem identificada por SHA256 e registrar VA/RVA/offset corretamente.
Converter a evidência em contrato e testes de estados/transições; distinguir
teste sintético, execução ODH com recurso comercial e observação do original.

Já há uma divergência concreta: no Simitone fixado, Return8 encerra a thread;
no binário estudado, Return8 e End12 usam o mesmo handler de pilha. A unidade
ODH segue essa evidência nativa, conforme
[Call7 e retornos](hit-native-call-validation.md). As próximas lacunas incluem
população do mapa de entrypoints, backend/provider de áudio e continuação dos
saved frames/Clock; autonomia, globais, expansões e save exigem gates próprios.
As fontes originais permanecem externas e preservadas. A Legacy é comparação
de outra edição; seus endereços não substituem os da imagem anteriormente
estudada. O fork não altera automaticamente as revisões principais fixadas.
