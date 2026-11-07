# Requisitos da primeira versão jogável

A primeira versão deve permitir jogar e continuar a partida em C/C++, com
paridade funcional com o Simitone fixado como primeiro marco. O projeto mantém
a experiência original do The Sims 1, suas sete expansões e conteúdo clássico
como destino. Bugs conhecidos podem ser descritos; sistemas centrais ausentes
não podem ser apresentados como concluídos.

## O que o jogador precisa conseguir fazer

- Abrir a vizinhança, ver famílias e informações dos lotes e entrar na casa.
- Criar adultos e crianças, editar, renomear e excluir famílias elegíveis;
  mudar de casa preservando pessoas, progresso e dinheiro.
- Controlar Sims, escolher/cancelar ações, alterar velocidade e deixar a autonomia
  funcionar com necessidades, relações, habilidades, escola, carreira e serviços.
- Comprar, mover, girar e vender objetos; construir com custo e restrições reais,
  incluindo paredes, pisos, portas/janelas, escadas, telhados e alterações do terreno.
- Usar animais e atividades das expansões; viajar ao Centro, Férias, Cidade Velha,
  Cidade Estúdio e Cidade Mágica, permanecer e retornar mantendo casa e progresso.
- Salvar, sair e continuar, inclusive ações em andamento e estadias admitidas.
- Escolher idioma e usar conteúdo personalizado clássico com dependências claras.
- Obter logs úteis quando ocorrer uma falha, sem depender de ferramentas internas.

## Interface e dados

Usar a interface original em todo o jogo: mapa, criação de família, simulação,
compra/construção, menus de ações e diálogos. Preservar proporção dos elementos,
retratos, navegação, comportamento dos submenus e informações ao passar o mouse.
O modo de diagnóstico não substitui a apresentação da versão jogável.

Os dados são fornecidos pelo jogador; não se exige executável original nem CD
inserido. A entrada implementada é uma pasta extraída. Compatibilidade com Legacy
Collection é interesse adicional, sem equivalência presumida com Complete Collection.

## Aceitação

Exercitar os percursos completos no aplicativo e conferir resultados observáveis:
necessidades, dinheiro, relações, personagens e alterações do lote. Conferir
cancelamento/falha sem perda ou cobrança indevida. Salvar e reabrir em processo
novo, preservando estado e pedidos ativos. Conferir apresentação e controles
em janela e uma sessão prolongada com limitações explícitas.

Testes sintéticos, execução sem janela e compilação apoiam a verificação, mas
não substituem o uso real. A aceitação Linux não estabelece suporte nos demais
alvos; cada um tem seus [próprios critérios](portability.md).
