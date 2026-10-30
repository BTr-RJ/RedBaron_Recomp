# EOS e matchmaking dentro do jogo

O provedor EOS agora está ligado ao executável e às chamadas nativas de rede.
O menu do jogo publicou uma partida pública real no EOS; uma consulta separada
encontrou o registro. Isso ainda **não valida uma partida entre dois usuários
EOS**, conexões pela Internet ou relay. O release permanece pendente.

## Uso no ambiente de desenvolvimento

Na aba Online do launcher, marcar **Enable online play through Epic Online
Services**, salvar e clicar em Play. No jogo, usar **Online → Player Match →
Find Game**, escolhendo o mesmo modo. O comportamento original cria uma
partida pública quando a busca não encontra outra adequada. O anfitrião
precisa manter o jogo aberto. Clientes usam o menu, sem endereço de servidor
ou convite do launcher.

O arquivo local `build_cpp_release/eos/config.ini` contém a configuração do
produto. Não enviar seu conteúdo ao Git, logs, mensagens ou pacotes públicos.
A opção fica desmarcada por padrão; o comportamento offline/LAN anterior é
preservado. EOS e uma sala WebRTC não podem ser ativados simultaneamente.
O launcher pode fechar depois de abrir o jogo: o provedor roda no jogo.

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

Permanecem necessários: partida completa com dois executáveis em redes distintas,
desconexão/reentrada durante gameplay e teste de oito jogadores
e co-op. Amigos Epic, convites privados EOS, ranking, voz, rollback e ports
Linux/Android não estão implementados por esta integração.

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
