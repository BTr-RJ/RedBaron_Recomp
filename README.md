# RedBaron_Recomp

Port nativo experimental de **Snoopy Flying Ace** para Windows x64, usando ReXGlue SDK / RexAuto 0.10.0. O executável de desenvolvimento é `snoopyflyingace.exe`. O XEX local identifica o título como `584109B5`.

## Estado atual

- Build Release funcional, com renderização 3D, áudio e controle Xbox confirmados pelo usuário.
- Licença completa configurada no runtime; o usuário confirmou que “Unlock Full Game” desapareceu.
- Duplo clique com descoberta automática da pasta `game` ao lado de `build_cpp_release`.
- Direct3D 12 em ROV, configuração necessária para o 3D funcionar nesta máquina.
- Mapeamento inicial de teclado e cliques do mouse, sem movimento de câmera pelo mouse.
- Backend SDL e base adicional de controles incluídos. O controle Microsoft foi reconhecido; outros modelos ainda exigem teste físico.
- Roteamento opcional para separar teclado/mouse de controles por jogador. Perfis adicionais e partidas locais ainda dependem de implementação; veja [o diagnóstico de multiplayer](docs/MULTIPLAYER.md).

O [launcher Windows](launcher/README.md) oferece **Install, Video, Controls e Online**, Save/Play e abertura direta opcional. Execute `launcher/out/RedBaron_Launcher.exe` após compilar com `launcher/build.ps1`. O instalador usa dados locais já extraídos e requer ferramentas/SDK instalados. Dois jogadores locais com bots e LAN experimental entre dois processos foram validados; quatro locais, Internet completa e rollback continuam pendentes. Som e Conquistas permanecem planejados. Veja o [roadmap](docs/ROADMAP.md) e o [registro das alterações](docs/ESTADO_ATUAL.md).

## Conteúdo do repositório

| Pasta | Conteúdo |
|---|---|
| `port/src` | Inicialização e ajustes específicos do jogo |
| `port/config` | Configuração padrão, guia de controles e base SDL com licença |
| `port/generated/rexglue.cmake` | Integração CMake do toolkit, sem código derivado do jogo |
| `port/*.toml` | Manifesto e correções de agrupamento de funções |
| `patches` | Patch do codegen do SDK para os saltos entre chunks |
| `docs` | Estado validado, limitações e planejamento |
| `scripts` | Preparação/build local e auditoria do conteúdo versionado |
| `launcher` | Interface WPF, instalador, preferências, captura de teclas e testes |

O GitHub não distribui o jogo: XEX, assets, ícones extraídos e C++ gerado a partir do XEX ficam apenas na máquina do usuário. SDK instalado, builds, saves, caches, logs e backups também são locais. O histórico publicado é filtrado para excluir fontes derivados, ícones e cópias antigas desses arquivos; o histórico original completo fica em backup local.

## Compilar

Ambiente utilizado: Windows x64, LLVM/Clang 23.1.4, CMake, Ninja e SDK experimental ReXGlue/RexAuto 0.10.0. O SDK instalado fica em `sdk_experimental`; seu código-fonte é mantido separadamente. Veja [as instruções do patch](patches/README.md).

```text
RedBaron_Recomp/
  port/
  game/                 # default.xex e demais dados extraídos, fornecidos localmente
  sdk_experimental/     # SDK instalado: bin/, include/, lib/
  build_cpp_release/    # gerado pelo CMake
```

Forneça seus dados extraídos em `game/` e instale o SDK em `sdk_experimental/`. Na raiz do projeto, em PowerShell, com as ferramentas no PATH:

```powershell
.\scripts\build.ps1
.\launcher\build.ps1
```

O script valida os caminhos, preserva um backup do build existente, gera os fontes **localmente antes de configurar o CMake**, compila e copia as DLLs. Aceita `-SdkRoot`, `-BuildDirectory`, `-Parallel` e `-ConfigureOnly`. Ele usa dados já extraídos; extração de pacotes no instalador do launcher é uma etapa futura.

O SDK deve conter o patch experimental compatível com o manifesto e os agrupamentos deste projeto. Um SDK genérico sem esse ajuste pode voltar a produzir erros de labels. Para compilações incrementais após a preparação, use `cmake --build build_cpp_release --target snoopyflyingace --parallel 4`.

## Executar e configurar

Abra `launcher/out/RedBaron_Launcher.exe` para configurar e jogar. A abertura de `build_cpp_release/snoopyflyingace.exe` continua disponível. Mantenha ao lado do jogo as DLLs, `redbaron_probe.exe`, `snoopyflyingace.toml` e `gamecontrollerdb.txt`.

- Edite `build_cpp_release/snoopyflyingace.toml` para ajustes pessoais.
- `port/config/snoopyflyingace.toml` é o padrão versionado, instalado somente quando o build ainda não tem configuração.
- Preserve `render_target_path_d3d12 = "rov"` e `mnk_mouse = false`.
- Consulte [CONTROLES.txt](port/config/CONTROLES.txt), cuja tabela usa o preset **FlyingAce**.
- O CMake copia o guia e a base SDL e preserva configurações pessoais existentes.

Multiplayer e funções online adicionais ainda dependem de implementação. Não há integração de conquistas com serviços de PC.

## Histórico e componentes

Cada alteração concluída deve receber um commit com descrição do comportamento e da validação, preservando o build funcional. As instruções de trabalho estão em [AGENTS.md](AGENTS.md).

Antes de enviar commits, execute `python scripts/audit_repository.py`. A auditoria verifica os caminhos e tipos de arquivos de todos os commits alcançáveis pelas referências Git, não somente a pasta atual. Ela complementa a revisão humana da origem de arquivos novos.

A base comunitária [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) tem sua [licença zlib](port/config/gamecontrollerdb.LICENSE.txt) e [origem/hash](port/config/gamecontrollerdb.SOURCE.txt) incluídos. Os demais componentes conservam seus avisos e licenças originais.
