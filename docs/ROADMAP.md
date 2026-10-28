# Roadmap do RedBaron_Recomp

## Decisões atuais

Preservar o gameplay original: o movimento do mouse não deve controlar câmera ou voo neste momento. Teclado, cliques e controles físicos devem coexistir. O layout atual serve como referência; os comandos serão configuráveis no launcher.

O Xbox Series do usuário é o controle de referência. A compatibilidade é ampliada pelo SDL e pela base de mapeamentos, com confirmação por modelo conforme os testes.

## Sequência

1. **Licença e execução:** desbloqueio confirmado no menu e renderização normal com ROV. Continuar a validação de conteúdo e estabilidade da campanha.
2. **Entrada:** validar o layout no voo, uso simultâneo com controle, vibração, reconexão e outros modelos. Manter câmera pelo mouse desligada.
3. **Multiplayer:** investigar até quatro jogadores locais e substituir stubs de rede. Quantidade de jogadores e funcionalidade online ainda precisam ser demonstradas.
4. **Vídeo:** resoluções, proporções, filtros e limite de FPS. Avaliar viabilidade de FSR/DLSS antes de oferecer opções.
5. **Launcher:** criar a interface abaixo sobre funções efetivamente suportadas pelo port.
6. **Splash screens:** corrigir o pipeline 2D de apresentação das imagens de abertura.

## Launcher planejado

| Aba | Escopo previsto |
|---|---|
| Vídeo / GPU | Adaptador, backend, resolução, janela/tela cheia, proporção, VSync, limite de FPS, filtros e opções disponíveis |
| Controles | Dispositivos, mapeamento de teclado/mouse e gamepad, perfis, zonas mortas e teste de entradas |
| Som | Dispositivo de saída e volumes suportados |
| Conquistas | Lista, descrições e progresso local do runtime; serviços externos dependem de integração futura |
| Online / perfil | Nome online, perfil local e conexão; recursos de rede dependem da implementação do multiplayer |
| Geral | Caminho dos dados, iniciar jogo, restaurar padrões e acesso aos logs |

O launcher deverá preservar preferências entre versões, permitir restaurar padrões e usar o TOML atual como ponto de partida. Nome online será uma configuração de perfil; não deve ser apresentado como conexão aos serviços originais do Xbox Live.

O desenho e a tecnologia serão definidos quando essa etapa começar. Este planejamento não inicia a implementação da interface nem muda a ordem das prioridades.
