# Validar uma mudança

Escolha verificações que respondam ao risco da alteração. Não repetir uma prova
aprovada sem mudança de código, dados, configuração, ambiente ou dúvida concreta.

## Componentes e percursos

1. Execute os testes pertinentes ao serviço alterado. Não crie testes que apenas
   repitam a implementação ou troquem um nome de objeto por outro equivalente.
2. Integre os consumidores do aplicativo e percorra o resultado do jogador:
   por exemplo, comprar um objeto, usá-lo, salvar e continuar.
3. Quando persistência for afetada, reabra em um processo novo e confira o estado
   relevante. Continuação em etapas e execução contínua só são equivalentes se
   essa equivalência tiver sido realmente conferida.
4. Para interface, confira janela, navegação, texto e proporção. Para áudio,
   saída silenciosa ou dispositivo simulado não comprovam reprodução audível.

Use dados e perfis de teste isolados. Copie o perfil completo quando o percurso
precisar de ocupação, configurações ou registro de família; somente um save
pode ser insuficiente. Preserve os arquivos fornecidos pelo usuário.

Os verificadores `tools/validate_runtime_smoke.py` e
`tools/validate_runtime_sequence.py` oferecem execução delimitada e continuidade.
Consulte `--help` para argumentos atuais. A sequência deve parar na primeira
falha e conservar o último estado válido. Não responder automaticamente a qualquer
janela nem limpar ações para fabricar uma continuação bem-sucedida.

## O que cada prova permite afirmar

| Verificação | Conclusão possível | Não comprova |
| --- | --- | --- |
| Compilação | Código aceito naquela configuração. | Jogo funcionando ou desempenho. |
| Teste sintético | Regra delimitada com entradas controladas. | Dados comerciais e percurso completo. |
| Execução sem janela | Efeitos e estado da simulação naquele percurso. | Interface e controles corretos. |
| Salvar/reabrir | Estado conferido sobrevive à restauração. | Todo conteúdo ou todas as ações. |
| Uso em janela | Apresentação e entrada observadas. | Outra plataforma ou ausência geral de bugs. |
| Aparelho real | Comportamento e recursos medidos naquele aparelho. | Outros modelos e plataformas. |

Revisão independente deve conferir fontes, limites e efeitos do conjunto.
Documentação sem mudança de código não exige compilar o jogo. Resultados
públicos devem explicar cenário e limitações; saídas geradas não são dependência
do clone nem substituem instruções reproduzíveis.
