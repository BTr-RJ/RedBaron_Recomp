# RedBaron_Recomp

Port nativo experimental de **Snoopy Flying Ace** para Windows x64, usando ReXGlue SDK / RexAuto 0.10.0. O executável de desenvolvimento é `snoopyflyingace.exe`. O XEX local identifica o título como `584109B5`.

## Estado atual

- Build Release funcional, com renderização 3D, áudio e controle Xbox confirmados pelo usuário.
- Licença completa configurada no runtime; o usuário confirmou que “Unlock Full Game” desapareceu.
- Duplo clique com descoberta automática da pasta `game` ao lado de `build_cpp_release`.
- Direct3D 12 em ROV, configuração necessária para o 3D funcionar nesta máquina.
- Mapeamento inicial de teclado e cliques do mouse, sem movimento de câmera pelo mouse.
- Backend SDL e base adicional de controles incluídos. O controle Microsoft foi reconhecido; outros modelos ainda exigem teste físico.

O mapeamento atual é **referencial e personalizável**. O launcher planejado terá abas de vídeo/GPU, controles e mapeamento, som, conquistas e online/perfil. A interface ainda não foi implementada. Veja o [roadmap](docs/ROADMAP.md) e o [registro das alterações](docs/ESTADO_ATUAL.md).

## Conteúdo do repositório

| Pasta | Conteúdo |
|---|---|
| `port/src` | Inicialização e ajustes específicos do jogo |
| `port/config` | Configuração padrão, guia de controles e base SDL com licença |
| `port/generated` | Código recompilado e integração CMake do checkpoint original |
| `port/*.toml` | Manifesto e correções de agrupamento de funções |
| `patches` | Patch do codegen do SDK para os saltos entre chunks |
| `docs` | Estado validado, limitações e planejamento |

Dados do jogo, SDK instalado, builds, saves, caches, logs e backups permanecem locais. O histórico original foi preservado; novos commits deixam de rastrear cópias `.bak` e metadados transitórios do codegen.

## Compilar

Ambiente utilizado: Windows x64, LLVM/Clang 23.1.4, CMake, Ninja e SDK experimental ReXGlue/RexAuto 0.10.0. O SDK instalado fica em `sdk_experimental`; seu código-fonte é mantido separadamente. Veja [as instruções do patch](patches/README.md).

```text
RedBaron_Recomp/
  port/
  game/                 # default.xex e demais dados extraídos, fornecidos localmente
  sdk_experimental/     # SDK instalado: bin/, include/, lib/
  build_cpp_release/    # gerado pelo CMake
```

Na raiz do projeto, em PowerShell, com as ferramentas no PATH:

```powershell
cmake -S port -B build_cpp_release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -Drexglue_DIR="$PWD/sdk_experimental/lib/cmake/rexglue"
cmake --build build_cpp_release --target snoopyflyingace --parallel 4
Copy-Item sdk_experimental/bin/rexruntime.dll build_cpp_release/
Copy-Item sdk_experimental/bin/rexgpu-xenos.dll build_cpp_release/
```

O build executa o codegen quando necessário. O SDK deve conter o patch experimental compatível com o manifesto e os agrupamentos deste projeto. Um SDK genérico sem esse ajuste pode voltar a produzir os erros de labels já corrigidos.

## Executar e configurar

Abra `build_cpp_release/snoopyflyingace.exe`. Mantenha ao lado dele as DLLs, `snoopyflyingace.toml` e `gamecontrollerdb.txt`.

- Edite `build_cpp_release/snoopyflyingace.toml` para ajustes pessoais.
- `port/config/snoopyflyingace.toml` é o padrão versionado, instalado somente quando o build ainda não tem configuração.
- Preserve `render_target_path_d3d12 = "rov"` e `mnk_mouse = false`.
- Consulte [CONTROLES.txt](port/config/CONTROLES.txt), cuja tabela usa o preset **FlyingAce**.
- O CMake copia o guia e a base SDL e preserva configurações pessoais existentes.

Multiplayer e funções online adicionais ainda dependem de implementação. Não há integração de conquistas com serviços de PC.

## Histórico e componentes

Cada alteração concluída deve receber um commit com descrição do comportamento e da validação, preservando o build funcional. As instruções de trabalho estão em [AGENTS.md](AGENTS.md).

A base comunitária [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) tem sua [licença zlib](port/config/gamecontrollerdb.LICENSE.txt) e [origem/hash](port/config/gamecontrollerdb.SOURCE.txt) incluídos. Os demais componentes conservam seus avisos e licenças originais.
