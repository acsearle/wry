.plan
=====

Apple-first for convenience; don't fight the development platform.  Strive to
keep the platform dependencies localized though

Renderloop
----------
- For MTKView and CustomMetalView, the CVDisplayLink wakes the thread and then
  the must make a blocking call to nextDrawable.  When the thread actually
  suspends here, the scheduler may not wake it in time to meet the next vsync,
  which is only 8.3 ms away.  This seems intractable at 120 Hz, though the
  method works ok at 60 Hz
- The new API is CADisplayLink, which calls into a delegate with a ready
  drawable, so all blocking happens in the OS at least
- When we render with a dedicated thread, the main thread ends up as a stub
  whose only job is to send messages to the render thread where they are
  resolved against the drawn interface and perhaps passed on to the model
- The callback also provides the targetTimestamp of the drawable which should
  be used for all timed things, which means we are lacking a lot of necessary
  information to draw the scene until relatively late
- Thus, we go full circle and return to a model where we render on the main
  thread.  We can only process changes per frame anyway.
- We can still marshal other stuff on other threads if needed, and do less
  time critical things (like bloom? shadows?) one frame behind?

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





  // Null, numbers, strings, arrays are obvious
    //
    // Stockpile of 3t haematite coarse 3t quartz fine
    //
    //
    // Pallet of discrete items
    // Shipping container of discrete items
    //
    // Barrel of liquid
    //
    // Pond of liquid
    //
    // Cylinder of gas
    //
    // Grind -> solid -> coarse -> fine
    //
    // Heat -> evaporate liquids, evaporate hydrates, reduce, pyrolosis
    
