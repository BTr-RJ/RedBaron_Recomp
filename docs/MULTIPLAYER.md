# Multiplayer

## Local play

Two native split-screen players and bots have been exercised. Co-op remains two-player. Four configurable input profiles do not create four cameras: four-player local gameplay is not implemented in v0.10.

For keyboard plus controller, choose **Exclusive player 2** for keyboard in Controls. Assign the controller to player 1, save, restart and join through the local menu. Keyboard/mouse share player 1 by default. One keyboard source is supported; mouse movement does not steer.

SDL normalizes supported controllers to Xbox positions. Xbox Series is the physical reference. Community mappings expand coverage without guaranteeing every controller/adapter.

## Online

EOS provides discovery and P2P/relay. Competitive sessions advertise up to eight participants; co-op remains two. Original game rules preserve bots. Public sessions are found in the native menus; private sessions use Epic invitations. See [EOS](EOS.md).

Two-PC Internet lobby/flight has been observed. Eight human games, every mode, full-round completion and all invitations are not validated. Report failures with redacted logs. The release flow does not need a separate WebRTC room server.
