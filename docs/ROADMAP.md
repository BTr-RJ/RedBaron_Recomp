# Roadmap do RedBaron_Recomp

## Decisões atuais

**Prioridade atual: online e multiplayer local para quatro pessoas, preservando bots.** Dois locais com bots e LAN entre dois processos foram validados. Quatro locais, rollback e serviços completos de Internet ainda não estão implementados. O launcher mantém tema escuro e interface em inglês. Ver [multiplayer](MULTIPLAYER.md) e [launcher](../launcher/README.md).

Preservar o gameplay original: o movimento do mouse não deve controlar câmera ou voo neste momento. Teclado, cliques e controles físicos devem coexistir. O layout atual serve como referência; os comandos são configuráveis no launcher.

O Xbox Series do usuário é o controle de referência. A compatibilidade é ampliada pelo SDL e pela base de mapeamentos, com confirmação por modelo conforme os testes.

## Sequência

1. **Licença e execução:** desbloqueio confirmado no menu e renderização normal com ROV. Continuar a validação de conteúdo e estabilidade da campanha.
2. **Entrada:** validar o layout no voo, uso simultâneo com controle, vibração, reconexão e outros modelos. Manter câmera pelo mouse desligada.
3. **Multiplayer (prioridade atual):** perfis/saves separados, segundo jogador e transporte LAN experimental implementados. Próximos: dois PCs/controles físicos, quatro câmeras/jogadores locais, modos online restantes, reconexão, serviços de Internet e avaliação de rollback. Bots locais preservados e testados. Ver [estado e testes](MULTIPLAYER.md).
4. **Vídeo (em validação):** FSR 1/CAS, VSync do monitor, resolução física até 8K, escala interna e filtros implementados. Validar missões em resoluções maiores; validar alvos de FPS nas demais missões, validar a correção da física e o pacing do VSync, refinar movimento de objetos/HUD dos previews FSR 3.1.5 e DLSS e validar HDR na TV. Ver [implementação e medições](VIDEO.md).
5. **Launcher com instalador embutido:** SDK e transporte podem ser incluídos na compilação do launcher; ferramentas ausentes são preparadas de fontes oficiais. A instalação de jogo extraído passou em teste isolado; CMake, Ninja, Python e LLVM foram preparados em pastas isoladas. Verificar a instalação do Microsoft Build Tools em uma máquina limpa e fazer auditoria de redistribuição. Dois pacotes DLC de jogo foram instalados e montados em teste; validar o conteúdo em partida e estudar os itens de avatar. A aba Update consulta e baixa releases verificadas; aplicação sobre instalação existente ainda falta. Base-game STFS, Som e Conquistas permanecem pendentes.
6. **Splash screens:** corrigir o pipeline 2D de apresentação das imagens de abertura.

## Launcher: estado e evolução

| Aba | Escopo previsto |
|---|---|
| Vídeo / GPU | Adaptador, backend, resolução, janela/tela cheia, proporção, VSync, limite de FPS, filtros e opções disponíveis |
| Controles | Dispositivos, mapeamento de teclado/mouse e gamepad, perfis, zonas mortas e teste de entradas |
| Som | Dispositivo de saída e volumes suportados |
| Conquistas | Lista, descrições e progresso local do runtime; serviços externos dependem de integração futura |
| Online / perfil | Nome online, perfil local e conexão; recursos de rede dependem da implementação do multiplayer |
| Geral | Caminho dos dados, iniciar jogo, restaurar padrões e acesso aos logs |
| Instalação | Selecionar cópia local do jogo, verificar arquivos, extrair/importar os dados e preparar os componentes necessários localmente |

O launcher preserva preferências e usa o TOML atual. O nome chega ao perfil XAM e a aba Online permite LAN experimental; UPnP segue desabilitado. DLSS tem cinco modos com escala automática. FPS/HDR exibem o estado real da saída; HDR converte conteúdo SDR. FSR/DLSS ainda precisam de movimento de objetos e composição separada do HUD. Sem frame generation. Ver [vídeo](VIDEO.md).

O repositório poderá tornar-se público posteriormente, sem arquivos do jogo nem código gerado. O instalador usa uma pasta extraída fornecida pelo usuário e compila em destino separado; a preparação automática das ferramentas já foi testada parcialmente. A extração do pacote do jogo base ainda não foi implementada, e a instalação do Microsoft Build Tools precisa ser validada em uma máquina limpa.

Interface nativa Windows em WPF/.NET Framework 4.8. Quatro mapas de entrada não equivalem a quatro câmeras locais; a tela dividida atual foi validada com dois jogadores e quatro bots.

O usuário autorizou empacotar e publicar o launcher em Releases quando o instalador estiver completo e validado. Essa condição ainda não foi atendida; nenhuma Release oficial foi publicada nesta etapa.
