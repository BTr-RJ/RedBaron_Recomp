# Epic Online Services: integração planejada

O EOS foi escolhido para integração do online. Um diagnóstico separado já
compilou e inicializou o SDK oficial 1.19.2.1 no Windows, mas o provedor online
**ainda não está implementado**. O jogo continua usando o transporte experimental existente.
Não anunciar salas, amigos, crossplay ou conectividade EOS como funcionais até
serem testados com o produto e o SDK oficiais.

## Experiência pretendida

- O anfitrião cria uma partida no jogo. Partidas públicas podem ser encontradas
  no jogo sem convite; as privadas usam convite ou entrada por amigos.
- A simulação continua no jogo do anfitrião. O EOS fornece serviços de sessão,
  identificação e comunicação; não converte automaticamente a lógica original
  em uma simulação autoritativa e não implementa rollback para o port.
- O acesso à lista de amigos Epic exige a integração e autenticação da conta
  Epic. O Social Overlay é uma opção no Windows; a documentação da Epic informa
  que ele não está disponível no Linux ou Android. Nessas plataformas, a
  interface correspondente precisará ser própria e validada.
- O SDK existe para Windows, Linux e Android, mas isso não conclui os ports do
  jogo nem comprova crossplay entre eles. Testar as versões e funções exatas
  das plataformas antes de anunciar suporte.

## Preparação pelo responsável pelo projeto

1. No [Epic Developer Portal](https://dev.epicgames.com/portal/), configurar
   organização e produto `RedBaron_Recomp`. Revisar e aceitar pessoalmente os
   acordos que a Epic exigir; não registrar aceites em nome do responsável.
2. Em Downloads & Release Notes → EOS SDK, baixar e extrair o SDK oficial para
   Windows. Manter o pacote local, fora do Git, até revisar sua redistribuição.
3. Configurar o produto, sandbox, deployment e um cliente com política limitada
   aos serviços necessários. Para recursos sociais com contas Epic, configurar
   também Epic Account Services e seguir as exigências de revisão da Epic.
4. Informar ao desenvolvedor apenas o caminho local do SDK. Não enviar senhas,
   tokens nem segredos em issues, conversas, logs ou commits. As credenciais e
   configurações de acesso devem ser tratadas conforme a política do cliente e
   a documentação oficial da Epic.

O SDK 1.19.2.1 fornecido pelo usuário foi extraído em pasta local ignorada pelo
Git. Para compilar o diagnóstico em outra máquina, informe a pasta `SDK` extraída:

```powershell
cmake -S port/eos -B eos-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DREDBARON_EOS_SDK=C:/caminho/para/SDK
cmake --build eos-build --target redbaron_eos_probe --parallel 4
ctest --test-dir eos-build --output-on-failure
```

Com as credenciais em um arquivo local ignorado pelo Git, os comandos
`redbaron_eos_probe --device-login <config>` e `--lobby-check <config>`
testam, respectivamente, o login anônimo por dispositivo e a criação seguida
da remoção de uma sala privada temporária. Não coloque esse arquivo ou seus
valores em logs, mensagens ou commits. No ambiente de desenvolvimento, os dois
testes foram concluídos usando o produto configurado pelo usuário. O login por
dispositivo não é autenticação de conta Epic e não fornece lista de amigos.
O comando `--search-check <config>` cria uma sala pública temporária em uma
categoria aleatória de diagnóstico, busca essa categoria pela mesma identidade
e remove a sala. Publica e lê registros sintéticos válidos de 8 KiB e do tamanho
mínimo, divididos em atributos pequenos, verificando a remoção dos atributos
antigos ao reduzir o registro. A busca repetida foi necessária até as alterações
aparecerem no índice. `lobby_record.*` implementa essa publicação e leitura e
valida o registro nativo; ainda não está ligado ao processo de rede ou ao jogo.
Esse teste não valida descoberta por outro usuário nem entrada no jogo.

O diagnóstico `--sdk-check` somente inicializa e encerra a biblioteca; não autentica,
não cria uma conta e não verifica conectividade com os serviços da Epic. Um
segundo teste cobre a fragmentação e remontagem de mensagens de até 32 KiB em
pacotes de até 1170 bytes, limite declarado pelo SDK usado. Ela é utilizada
pelo adaptador P2P experimental descrito abaixo.

`port/eos/eos_wire.*` contém um adaptador experimental de pacotes P2P do EOS
para a interface de rede existente. Ele só aceita membros previamente fornecidos
pelo responsável pela sessão e usa o anfitrião para retransmitir entre clientes.
O comando `--wire-check <config>` valida apenas sua inicialização com uma
identidade autenticada e sem outros jogadores. O adaptador ainda não está ligado
ao lobby, ao jogo ou ao launcher; tráfego real, desconexões e autenticação dos
membros continuam pendentes de validação. Ele deve usar uma instância do EOS
dedicada ao transporte, pois consome as filas dos canais 0 e 1 dessa instância.
Um teste automatizado simula três membros e a API P2P: verifica encaminhamento,
mensagens fragmentadas confiáveis e não confiáveis e rejeição de remetentes
desconhecidos ou forjados. Essa simulação não prova conectividade EOS real.

## Integração e validação

Integrar inicialização, ciclo de chamadas e encerramento do SDK; autenticação;
criação, busca e entrada nas sessões; conexão P2P; e mapeamento das sessões
para as estruturas XAM usadas pelo jogo. A camada `port/network/wire.h` separa
o adaptador de sockets do provedor de rede, mantendo o WebRTC como referência
para testes de regressão. Será necessário adaptar limites de mensagem,
confiabilidade e ciclos de vida do EOS ao protocolo real do jogo.

Testar criação e entrada públicas no menu, salas privadas, até oito jogadores,
limite de dois no co-op, bots, desconexão e retomada, além de conexões diretas
e via relay em redes diferentes. Validar contas Epic e a experiência social
separadamente. Antes de uma release, revisar licenças, redistribuíveis,
políticas de acesso e todos os arquivos incluídos no pacote e no histórico Git.

Referências oficiais: [Lobbies e sessões](https://dev.epicgames.com/docs/epic-online-services/multiplayer/lobbies-and-sessions/lobbies-and-sessions-introduction),
[P2P](https://dev.epicgames.com/docs/epic-online-services/multiplayer/nat-p2p-interface/p2p-guide/p2p-intro),
[Social Overlay e crossplay](https://dev.epicgames.com/docs/epic-online-services/accounts-and-social/crossplay/crossplay-technical-overview),
[configuração de clientes e políticas](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/client-and-client-policy/client-policy-guide).
