# EOS e matchmaking dentro do jogo

O provedor EOS agora está ligado ao executável e às chamadas nativas de rede.
Em 30/10/2026, dois PCs Windows em conexões de Internet diferentes chegaram ao
lobby e ao voo de Team Dog Fight usando a busca nativa e o EOS. A segunda tentativa
permaneceu em voo por cerca de quatro minutos e meio e foi interrompida manualmente
pelo usuário. O encerramento completo da rodada e a qualidade do combate ainda
precisam de validação. O release permanece pendente.

## Uso no ambiente de desenvolvimento

A aba Online do launcher contém somente o nome do jogador. EOS e login Epic
são automáticos; Play usa o bootstrapper oficial e verifica os componentes Epic.
O primeiro login pode exigir autenticação e consentimento. Os campos antigos de
salas WebRTC, servidor, LAN e ativação de EOS foram retirados.

RT no hangar/lobby abre o painel social Epic em vez da tela interna de amigos
Xbox 360. Em uma sessão publicada, selecionar um amigo e a opção de convite do
overlay. Private Match publica uma sala EOS somente por convite com as vagas
privadas nativas. Player Match continua oferecendo descoberta pública. Seu
hangar anterior a Find Game ainda não publica uma sessão de jogo: selecionar
ou criar a partida para habilitar o convite da sessão.

O arquivo local `build_cpp_release/eos/config.ini` contém a configuração do
produto. Não enviar seu conteúdo ao Git, logs, mensagens ou pacotes públicos.
Configurações antigas não desativam EOS durante a migração. Falha de conexão
ou login mantém o jogo local disponível. O acesso social público fora da
organização ainda depende da configuração/aprovação de marca do aplicativo EAS.
O launcher pode fechar depois de abrir o jogo: o provedor roda no jogo.

Para o teste privado autorizado em dois PCs, `scripts/package-private-test.ps1`
recebe o caminho do INI local e cria um instalador com esse recurso embutido.
Ele instala o INI junto ao runtime e habilita EOS automaticamente na primeira
configuração, migrando os perfis antigos para o provedor automático. Esse
artefato fica local e privado; não é um pacote autorizado para publicação.
O guia está em [PRIVATE_TEST.md](PRIVATE_TEST.md). Play grava logs por sessão;
**Update → Open logs folder** abre a pasta para o diagnóstico.

## Implementação

- `port/eos/online_service.*`: worker dedicado para autenticação por dispositivo,
  renovação, consultas, criação/saída de lobby, notificações de membros e P2P.
- `port/src/online.cpp`: inicialização antes dos perfis e sockets do jogo,
  endereços estáveis e desligamento antes do encerramento imediato do SDK.
- `patches/rexglue-online-provider.patch`: encaminha busca, QoS e conexão do
  runtime ao provedor. A abertura do socket escolhido aguarda a entrada EOS.
  A busca usa um snapshot atualizado em segundo plano, sem aguardar a Internet
  a cada frame. O patch é aplicado após `rexglue-online-transport.patch`.
- `native_record.*` e `lobby_publisher.*`: publicam os atributos reais da sessão
  nativa, preservando modos e metadados. Uma partida cheia ou temporariamente
  oculta deixa de ser anunciada sem perder o transporte; a exclusão efetiva da
  sessão encerra o lobby.
- `address_wire.*`: os endereços do jogo permanecem estáveis quando o EOS atribui
  posições no lobby. Rejeita colisões e mensagens antigas de posições reutilizadas.
- `eos_wire.*` e `network/socket_bridge.*`: TCP/UDP do jogo sobre mensagens EOS,
  fragmentação e retransmissão pelo anfitrião. A simulação e a replicação continuam
  sendo as originais do jogo. Isso não implementa rollback.
- QoS usa socket/canal EOS separados, consulta dados reais do anfitrião e mede
  o tempo de resposta. Uma segunda identidade de dispositivo validou esse caminho.
- O runtime limita sessões EOS a oito jogadores. Limites nativos menores,
  como o co-op de dois, são mantidos. Bots não são convertidos em membros EOS.

## Evidências e limites

Em 30/10/2026, no teste privado com o usuário:

- O usuário confirmou que a instalação no segundo PC Windows 11 abriu o jogo.
- A busca pelo menu conectou o segundo PC ao lobby público; os dois nomes e a
  contagem regressiva apareceram no host. O usuário confirmou o voo na primeira
  tentativa, que depois exibiu uma tela de placar e saiu do lobby; a causa dessa
  primeira saída não foi determinada.
- Na segunda tentativa, o host usou o executável de produção, sem o driver de
  entrada de diagnóstico. SDL reconheceu controle Xbox e teclado/mouse no jogador 1.
  O host exibiu o voo por cerca de quatro minutos e meio, sem saída de participante
  registrada nesse intervalo. A sessão terminou com a mensagem Disconnected;
  o usuário confirmou que havia interrompido manualmente o teste.
- Logs e capturas permanecem locais em `prioridade3_multiplayer/`, fora do Git.
  Não houve medição de latência, identificação do caminho direto/relay dessa
  partida, confirmação do combate nos dois lados nem conclusão natural da rodada.
  Este teste não valida oito jogadores reais, co-op ou todos os recursos EOS.

Em 29/10/2026:

- Build do jogo e nove testes CTest passaram, incluindo encaminhamento nativo,
  autenticação de remetentes, fragmentação e estabilidade de endereços.
- Sete sockets TCP reais conectaram ao host simulado; UDP fragmentado transitou
  entre dois clientes pela ponte do host. Foram usadas oito instâncias locais
  da ponte; **a entrega de pacotes EOS foi simulada nesse teste**.
- Com o EOS real, o diagnóstico de runtime publicou, escondeu, reabriu e removeu
  uma sessão. Um segundo processo consultou o serviço e encontrou um registro
  durante a publicação e zero após o encerramento. Ambos usaram a mesma
  identidade de dispositivo, portanto não comprovaram entrada de outro jogador.
- O executável do jogo abriu com EOS, renderizou o menu e criou a partida
  pública Team Dog Fight. A consulta independente encontrou a sessão real.
- O launcher passou nos testes de preferências, argumentos, componentes ausentes
  e exclusão mútua de provedores. Sua aba Online foi inspecionada renderizada.

- Um diagnóstico Linux isolado, com outra identidade de dispositivo, encontrou
  o host Windows, consultou o QoS real, ingressou e recebeu o roster autenticado.
  TCP de 32768 bytes e UDP de 1800 bytes retornaram sem corrupção pela ponte
  nativa usando o EOS real em conexão direta. Esse diagnóstico não executa o jogo
  Linux e não valida uma partida completa nem redes físicas diferentes.
- A mesma identidade saiu e entrou novamente, repetindo os testes de dados.
  Com relay forçado nos dois lados, o EOS informou conexão do tipo relay tanto
  no QoS quanto no socket de gameplay, e os dois testes TCP/UDP passaram.
- O teste real revelou e corrigiu atributos de roster visíveis somente ao host
  e atualizações concorrentes que podiam apagar os atributos da sessão.
- O jogo confirmou oito vagas no menu e removeu o lobby ao fechar a janela.
- Uma instalação limpa compilou com as bibliotecas EOS embarcadas e o probe
  confirmou a disponibilidade do provedor, sem incluir configurações do produto.
- Sete clientes com identidades EOS distintas executaram o diagnóstico em
  paralelo contra o host Windows. Todos ingressaram, receberam o roster e
  completaram os ecos TCP/UDP reais. Foram oito participantes do transporte;
  esse teste não substitui oito pessoas executando o jogo.

Validação social de 30/10/2026:

- Login Epic e overlay oficial executados com a conta do proprietário.
- RT no Private Match abriu diretamente o overlay. Ele exibiu a sala com 1/8
  participantes e a ação “Convidar para o jogo” para um amigo. Nenhum convite
  foi enviado a pessoas durante o diagnóstico.
- O runtime confirmou 12/12 conquistas nativas e consultou o progresso EOS.
  Três exemplos do portal são ignorados pela sincronização autoral, mas ainda
  podem aparecer no overlay do servidor. Não foram concedidas conquistas falsas.
- O código recebe convites EOS e encaminha a sessão ao fluxo nativo. O ingresso
  por convite entre duas contas reais permanece sem validação.
- Testes cobrem RT por borda, preservação da arma durante voo, precedência da
  sessão sobre a party persistente, vagas privadas e pacote nativo de convite.
  A política de menor ping possui teste; a detecção do Quick Game sem filtro
  de modo ainda precisa ser conferida no menu real.

Permanecem necessários: convite/entrada de duas contas reais, conquista obtida
normalmente, rodada completa até o placar, oito jogadores e co-op. Ranking,
voz, rollback e ports Linux/Android não estão implementados por esta integração.
O release público permanece pendente.

## Compilação e instalador

Para compilar o provedor a partir do SDK oficial local:

```powershell
cmake -S port/eos -B eos-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DREDBARON_EOS_SDK=C:/caminho/SDK -Drexglue_DIR=C:/caminho/rex/lib/cmake/rexglue
cmake --build eos-build --parallel 4
ctest --test-dir eos-build --output-on-failure
```

O ReXGlue usado deve conter o patch do provedor. Para compilar o jogo com os
fontes EOS, passar `-EosSdk C:/caminho/SDK` a `scripts/build.ps1`.

`scripts/stage-eos.ps1 -EosSdk <SDK> -BuildDirectory <eos-build> -RuntimeSdk <rex>`
prepara bibliotecas autorais, DLL/import library EOS e avisos de terceiros no
SDK de instalação. O jogo também pode ligar essas bibliotecas já compiladas,
sem incluir os headers do SDK EOS no pacote. `launcher/build.ps1` inclui o código
autoral e esses componentes do SDK preparado. A configuração do produto não
é incorporada automaticamente; sua distribuição precisa ser resolvida antes
de publicar um instalador online para usuários finais.

Diagnósticos opt-in, que acessam o produto real:

```powershell
eos-build/redbaron_eos_service_check.exe --host <config-local> <userdata-de-teste> <categoria-unica> 50
eos-build/redbaron_eos_service_check.exe --browse <config-local> <categoria-unica>
eos-build/redbaron_eos_service_check.exe --echo <config-local> <userdata-de-teste> <categoria-unica> 120
eos-build/redbaron_eos_service_check.exe --relay-echo <config-local> <userdata-de-teste> <categoria-unica> 120
```

Em um segundo dispositivo Linux, `redbaron_eos_peer_check <config-local>
<categoria-unica> <auto|relay>` verifica descoberta, QoS, entrada, roster e eco
TCP/UDP real contra `--echo`/`--relay-echo`. Para comprovar relay de gameplay,
o callback deve informar o tipo relay no socket de gameplay. Cada host de teste
deve encerrar antes de iniciar outro com a mesma identidade EOS; processos
simultâneos com a mesma identidade interferem no roteamento P2P do SDK.

O primeiro usa dados autorais de teste no runtime e remove somente o lobby que
criou; não executa uma partida. Os diagnósticos anteriores de SDK, login e
publicação permanecem em `redbaron_eos_probe`.

Referências oficiais: [Lobbies](https://dev.epicgames.com/docs/epic-online-services/multiplayer/lobbies-and-sessions/lobby-interface),
[P2P](https://dev.epicgames.com/docs/epic-online-services/multiplayer/nat-p2p-interface).
