# Requisitos da primeira versão jogável

A primeira versão Linux deve permitir jogar e continuar a partida em C/C++,
recuperando a experiência original do The Sims 1, suas sete expansões e conteúdo
clássico. A paridade funcional com o Simitone fixado é uma etapa intermediária.
Bugs conhecidos podem ser descritos; sistemas centrais ausentes não podem ser
apresentados como concluídos. Limitações da referência não retiram requisitos
do jogo original.

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

## Eventos da casa e ciclo de vida

Os eventos incluem seus efeitos e desfechos, além da chegada do personagem:

- Visitas de animais, como urso, gambá e guaxinim, com os comportamentos próprios
  previstos nos dados, incluindo mexer no lixo e as reações da casa.
- Ladrão, detecção e roubo; chamada à polícia pelo jogador ou pelo alarme
  contra roubo, intervenção e resultado sobre os bens da família.
- Entrada de bebês na família por nascimento ou adoção conforme as regras
  originais, cuidados, crescimento para criança e intervenções previstas pelo jogo.
- Morte de Sims, chegada da Morte, lápide ou urna e efeitos sobre a família.
  Incluir a tentativa de recuperar o Sim com a Morte quando admitida pelas
  regras, preservando o resultado e a identidade do personagem.
- Incêndio, propagação, risco de morte e objetos queimados transformados em
  cinzas; combate pelo jogador, chamada aos bombeiros pelo telefone ou pelo
  alarme de incêndio e resultado da intervenção.

A cobertura abrange também os demais visitantes, serviços e eventos do jogo
original e das expansões. Condições de chegada, escolhas e resultados seguem
os dados e comportamentos originais. Esses fluxos fazem parte do primeiro
marco Linux, inclusive seu estado após salvar, sair e continuar.

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

Nos eventos da casa, conferir chamadas manuais e automáticas por alarme,
intervenções e consequências sobre pessoas, objetos, dinheiro e família.
Nos ciclos de vida, conferir criação, retirada e eventual retorno do personagem
sem duplicação ou perda de identidade. Uma chegada de NPC ou uma imagem isolada
não comprova o evento completo.

Testes sintéticos, execução sem janela e compilação apoiam a verificação, mas
não substituem o uso real. A aceitação Linux não estabelece suporte nos demais
alvos; cada um tem seus [próprios critérios](portability.md).
