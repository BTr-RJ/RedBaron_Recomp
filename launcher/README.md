# Launcher do RedBaron_Recomp

Interface Windows x64 em WPF/.NET Framework 4.8, sem pacotes NuGet. Tema escuro simples escolhido pelo usuário. Configura o build atual e instala outra cópia a partir dos dados locais.

## Abrir e jogar

Execute `launcher/out/RedBaron_Launcher.exe`. Na árvore de desenvolvimento, ele encontra a raiz do projeto e usa `build_cpp_release`. Em uma instalação criada por ele, o launcher fica na raiz escolhida.

O atalho aponta para o launcher, respeitando **Abrir o launcher na próxima vez**. Desmarque e salve para iniciar direto no jogo. Segure **Shift ao abrir** ou use `RedBaron_Launcher.exe --settings` para voltar às configurações. Abrir `snoopyflyingace.exe` diretamente continua funcionando e não consulta essa preferência.

| Aba | Implementação |
|---|---|
| Install | Selecionar pasta extraída, validar XEX/Title ID `584109B5`, copiar para destino vazio, gerar C++, compilar e criar atalho opcional |
| Video | GPU, resolução 720p/1080p/1440p/4K, janela/tela cheia, VSync, anisotropia e FXAA. Opções consultadas no runtime instalado |
| Controls | Quatro mapas, controle SDL por identidade, remapeamento de botões/gatilhos/direções dos analógicos, edição/captura de teclas e cliques |
| Online | Nome validado e reservado para integração futura. UPnP desabilitado enquanto o online estiver adiado |
| Save / Play | Salvar com backup e iniciar o jogo. Play também salva; erros impedem a abertura e aparecem no rodapé |

## Limites atuais

O jogo reconhece apenas o perfil 1; configurar quatro entradas não habilita partidas para quatro. Um teclado/mouse é compartilhado com P1 ou reservado a um perfil por vez. Mapas dos outros perfis ficam salvos para uso posterior. As ações correspondem ao preset **FlyingAce**; os desenhos do jogo continuam Xbox.

O runtime instalado não inclui FSR; DLSS não está integrado. Essas escolhas aparecem indisponíveis, mesmo se a GPU as suportar em outros jogos. Desbloqueio/limitação adicional de FPS não foi implementado. Resoluções elevadas precisam de avaliação visual em missão. FXAA é independente de upscaling, não uma implementação substituta de DLSS/FSR.

Nome: regra do projeto de 1–15 caracteres ASCII, começando com letra/número; permite letras, números, espaços internos, hífen e sublinhado. Não aceita espaços finais. Fica no launcher; não altera ainda o perfil XAM nem conecta ao Xbox Live. Nenhuma porta de rede é aberta. Som e Conquistas continuam planejados.

## Instalação

Forneça uma **pasta já extraída** contendo `default.xex`, `Config` e `Packages`, o SDK experimental compatível, CMake, Ninja e Clang no PATH. Extração de pacotes STFS e instalação automática das ferramentas não estão incluídas.

O launcher embute um ZIP de fontes autorais/configurações do port e o script de instalação. Não embute SDK, XEX, assets, C++ gerado ou saves. Gera código localmente e copia DLLs do SDK fornecido. Não sobrescreve destino ocupado, não segue links/junções na origem/destino e não permite sobreposição com dados ou SDK.

Logs: `launcher-data/install-<data>/install.log` na instalação de origem. O destino recebe `.redbaron-install.json` com estado `installing`, `ready` ou `failed`. Cancelar encerra o processo e seus compiladores; arquivos parciais ficam no destino para inspeção. Nova tentativa exige pasta vazia. Os dados de origem não são removidos.

## Preferências e entrada

- `launcher-settings.json`, na raiz da instalação: quatro mapas, seletores, nome e abertura direta.
- `build_cpp_release/snoopyflyingace.toml`: opções do jogo. Chaves desconhecidas/tabelas são preservadas; alterações criam backups datados.
- O launcher impede salvar enquanto o jogo dessa instalação estiver aberto.
- `redbaron_pad_slots`: quatro seletores separados por `;`, cada um `auto`, `none` ou `GUID#ocorrência` SDL.
- `redbaron_pad_map_p1` até `p4`: 24 índices de origem na ordem de `Model.cs` / `pad_mapping.h`; `-1` remove uma ação, string vazia mantém identidade.
- `redbaron_keyboard_player`: 0 compartilha P1, 1–4 reserva o perfil. `mnk_mode=false` desliga teclado e ignora a reserva.

Controles idênticos usam GUID mais ordem de ocorrência. Durante a execução, desconectar um não renumera o outro. Entre execuções, pode ser necessário conferir a seleção se a ordem de conexão mudar. Entradas analógicas associadas a ações digitais usam limiar de metade do curso. Guide mantém o comportamento do SDK.

## Compilar e verificar

```powershell
# SDK e game/ fornecidos localmente, conforme README principal.
.\scripts\build.ps1
.\launcher\build.ps1

cmake -S port -B build_cpp_release -DREDBARON_BUILD_INPUT_TESTS=ON
cmake --build build_cpp_release --target redbaron_input_tests --parallel 4
ctest --test-dir build_cpp_release --output-on-failure
Start-Process .\launcher\out\RedBaron_Launcher.exe -ArgumentList '--self-test','C:\caminho\de\testes' -Wait
```

O launcher usa `csc.exe` e assemblies WPF do .NET Framework do Windows. `redbaron_probe.exe` consulta flags do runtime/GPU e dispositivos SDL sem carregar dados do jogo. A base de controles e sua licença acompanham a instalação.

Validado: build C++, testes de roteamento/remapeamento, testes C# de configuração/validação/argumentos/atalho, instalação limpa completa, rejeição de pastas sobrepostas/destino ocupado/título incompatível, Save/Play via UIAutomation, abertura direta e retorno por `--settings`. O ZIP embutido foi conferido contra os fontes atuais e não contém dados do jogo. Evidências locais em `prioridade5_launcher/`, fora do Git. Jogabilidade com mapas personalizados, múltiplos controles físicos e resoluções elevadas ainda requer teste do usuário.
