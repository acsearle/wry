.plan

Apple-first for convenience; don't fight the development platform.  Strive to
keep the platform dependencies localized.

- Custom metal view
- NSEvent mouse and keyboard input
- Metal sprite atlas draw

- FreeType glyphs
- Sprite atlas
- Text rendering
- PNG asset loading
- JSON asset description

- Onscreen text repl
- GUI elements?

- Stack machine arena
- Save and load
- Reified interaction queue

- Narrow/fast simulation to visualization pipe 

- Network code
- Server as append-only-log of commands
  - Resolves order of incoming commands from different player
  - Throttles rate of incoming commands per player
  - Broadcasts log to all players
  - Saves log and replays it to new players from their last checkpoint state if any
  - Mediates requests for checkpoint transfers to new join players
  - Mediates join/kick votes?
  - Announces checkpoints 
- Clients as async generators of commands and sync consumers of all player's commands
- The server does not run a simulation state and knows nothing about it except
  metadata
- Clients must each decide if other players commands are legal; there's no
  adversarial state.
- Savegames are checkpoints plus logs and are identical for all players up
  to local, pure visualization state.
- Games can easily be forked local but not reconciled, though we could make an
  offline merge tool.

